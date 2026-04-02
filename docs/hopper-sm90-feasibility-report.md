# Accel-Sim 支持 NVIDIA Hopper (SM90) 架构：可行性分析与实施计划

> **作者**: Auto-generated analysis
> **日期**: 2026-04-03
> **目标**: 评估在 Accel-Sim 框架中添加 NVIDIA Hopper (SM90) GPU 架构支持的可行性，并提供详细实施计划

---

## 目录

1. [执行摘要](#1-执行摘要)
2. [现有架构分析](#2-现有架构分析)
3. [Hopper 新特性分析](#3-hopper-新特性分析)
4. [可行性评估](#4-可行性评估)
5. [实施计划](#5-实施计划)
6. [风险分析与缓解措施](#6-风险分析与缓解措施)
7. [验证策略](#7-验证策略)
8. [附录：关键文件索引](#8-附录关键文件索引)

---

## 1. 执行摘要

Accel-Sim 当前支持 Kepler (SM3.x) 到 Ampere (SM8.x) 架构。NVIDIA Hopper (SM90) 引入了多项全新硬件特性——TMA、mbarrier、Thread Block Clusters、Distributed Shared Memory (DSMEM) 和 DPX 指令——这些特性超出了现有模拟器配置驱动扩展的能力范围。

**核心结论**：

| 类别 | 结论 |
|------|------|
| 基础支持（ISA 映射 + 配置） | **完全可行**，工作量约 1-2 周 |
| DPX 指令 | **完全可行**，纯配置层改动 |
| TMA (Tensor Memory Accelerator) | **可行但需大量新代码**，需新增执行单元和异步传输建模 |
| mbarrier | **可行**，需新增同步原语数据结构 |
| Thread Block Clusters | **有条件可行**，需修改 CTA 调度和跨 SM 同步逻辑 |
| Distributed Shared Memory | **难度最高**，需重构内存子系统地址解析和跨 SM 互连建模 |

建议采用**分阶段实施策略**，从基础配置支持开始，逐步添加高级特性。

---

## 2. 现有架构分析

### 2.1 Accel-Sim 架构扩展机制

Accel-Sim 的 GPU 架构支持由三层组成：

**第一层：ISA 指令映射（配置驱动）**

每个架构在 `gpu-simulator/ISA_Def/` 下有一个 opcode 头文件（如 `ampere_opcode.h`），定义 SASS 指令到执行单元类别的映射：

```
gpu-simulator/ISA_Def/
├── kepler_opcode.h      # SM3.x
├── pascal_opcode.h       # SM6.x
├── volta_opcode.h        # SM7.0
├── turing_opcode.h       # SM7.5
├── ampere_opcode.h       # SM8.0/8.6
└── trace_opcode.h        # 全局 opcode 枚举
```

映射示例（`ampere_opcode.h`）：
```cpp
{"FADD", OpcodeChar(OP_FADD, SP_OP)},      // → 单精度浮点单元
{"HMMA", OpcodeChar(OP_HMMA, SPECIALIZED_UNIT_3_OP)},  // → Tensor Core
{"R2UR", OpcodeChar(OP_R2UR, SPECIALIZED_UNIT_4_OP)},  // → UDP 单元
```

**第二层：硬件配置参数（配置驱动）**

每个 GPU 卡型在 `gpu-simulator/gpgpu-sim/configs/tested-cfgs/` 下有独立配置目录：

```
configs/tested-cfgs/
├── SM80_A100/
│   ├── gpgpusim.config    # 硬件参数（SM 数量、缓存、DRAM 等）
│   └── trace.config        # 指令延迟和发射间隔
└── SM86_RTX3070/
    ├── gpgpusim.config
    └── trace.config
```

**第三层：模拟器核心 C++ 代码（需编程）**

当新架构引入全新硬件模块时，需要修改核心模拟引擎：

- `shader.h/cc` — 流水线、执行单元、调度器
- `abstract_hardware_model.h/cc` — 指令类型、内存合并
- `gpu-sim.cc` — 集群/SM 配置、CTA 调度
- `trace_driven.cc` — Trace 解析和架构选择

### 2.2 现有执行单元框架

Accel-Sim 支持两类执行单元：

**固定单元**（C++ 类硬编码）：

| 单元 | C++ 类 | 流水线阶段 |
|------|--------|-----------|
| SP（单精度） | `sp_unit` | `ID_OC_SP → OC_EX_SP` |
| DP（双精度） | `dp_unit` | `ID_OC_DP → OC_EX_DP` |
| INT（整数） | `int_unit` | `ID_OC_INT → OC_EX_INT` |
| SFU（特殊函数） | `sfu` | `ID_OC_SFU → OC_EX_SFU` |
| Tensor Core | `tensor_core` | `ID_OC_TENSOR_CORE → OC_EX_TENSOR_CORE` |
| LD/ST（访存） | `ldst_unit` | `ID_OC_MEM → OC_EX_MEM` |

**可配置专用单元**（最多 8 个，`SPECIALIZED_UNIT_NUM=8`）：

通过 `gpgpusim.config` 中的 `-specialized_unit_N` 配置行定义：
```
-specialized_unit_1 1,4,4,4,4,BRA     # 分支单元
-specialized_unit_2 1,4,200,4,4,TEX   # 纹理单元
-specialized_unit_3 1,4,32,4,4,TENSOR # Tensor Core
-specialized_unit_4 1,4,4,4,4,UDP     # Uniform Data Path
```

格式：`<enabled>,<num_units>,<latency>,<id_oc_width>,<oc_ex_width>,<name>`

ISA 中 `op >= SPEC_UNIT_START_ID (100)` 的指令自动路由到对应的 specialized unit。

### 2.3 现有异步拷贝机制 (Ampere cp.async)

Ampere 的 `cp.async`（LDGSTS）是理解 TMA 扩展的基础。当前实现：

```
LDGSTS → 异步 Global→Shared 拷贝，warp 粒度，每 lane 一个地址
LDGDEPBAR → 建立 barrier 组，将前面未分组的 LDGSTS 归入一组
DEPBAR → 等待指定的 barrier 组完成
```

关键数据结构：
- `warp_inst_t::m_is_ldgsts` — 标记异步拷贝指令
- `shd_warp_t::m_ldgdepbar_buf` — 按 barrier 组存储待完成的 LDGSTS
- `ldst_unit::m_pending_ldgsts` — 跟踪在途请求（`warp_id → pc → addr → count`）
- Cache 命中/响应时递减计数，全部完成后释放 DEPBAR

### 2.4 现有同步机制

| 机制 | 实现位置 | 语义 |
|------|---------|------|
| `BAR.SYNC` | `barrier_set_t`（`shader.h:1058`） | CTA 内 warp 集合同步 |
| `MEMBAR` | `shd_warp_t::m_membar`（`shader.cc:1078`） | 等待所有 pending writes 完成 |
| `DEPBAR` | `shd_warp_t::m_waiting_ldgsts`（`shader.cc:1088`） | 等待 LDGSTS 组完成 |

---

## 3. Hopper 新特性分析

### 3.1 TMA (Tensor Memory Accelerator)

**硬件描述**：

TMA 是 Hopper 引入的独立硬件引擎，负责多维、批量、异步的数据移动。与 Ampere 的 `cp.async` 本质不同：

| 特性 | Ampere cp.async (LDGSTS) | Hopper TMA (CPASYNCBULK) |
|------|--------------------------|--------------------------|
| 粒度 | Warp 级（每 lane 一个地址） | CTA 级（单线程代表整个 CTA） |
| 地址 | 每线程显式地址 | Tensor Map 描述符（常量内存中） |
| 维度 | 1D 线性拷贝 | 支持 1D-5D tensor 切片 |
| 数据流 | Global → Shared | Global ↔ Shared（双向） |
| 完成通知 | DEPBAR 计数器 | mbarrier arrive |
| 发起方 | 所有活跃线程参与 | 单线程发起（TMA 硬件自动搬运） |

**相关 SASS 指令**：

| 指令 | 功能 |
|------|------|
| `CPASYNCBULK` | TMA 异步 bulk 拷贝（对应 `cp.async.bulk.tensor.*`） |
| `TENSORMAP` | Tensor Map 描述符操作 |
| `CPASYNCBULKCOMMIT` | 提交 TMA 操作组 |
| `CPASYNCBULKWAITGROUP` | 等待 TMA 操作组完成 |

### 3.2 mbarrier (异步内存屏障)

**硬件描述**：

mbarrier 是 Hopper 的全新同步原语，存储在共享内存中，基于**计数器**而非 warp 集合。核心特性：

- 支持异步操作（如 TMA 完成）直接对 mbarrier 做 arrive
- 基于到达计数器的 wait 语义（而非传统 BAR 的 warp 集合判断）
- 支持 phaseParity 机制实现流水线式同步
- 与 TMA 深度集成：TMA 完成时自动 arrive

**相关 SASS 指令**：

| 指令 | 功能 |
|------|------|
| `MBARRIER.INIT` | 初始化 mbarrier 对象，设置期望到达计数 |
| `MBARRIER.ARRIVE` | 线程到达 mbarrier |
| `MBARRIER.ARRIVE.EXPECT_TX` | 到达并设置期望的 TMA 字节数 |
| `MBARRIER.WAIT` | 等待 mbarrier 完成（所有到达 + TMA 完成） |
| `MBARRIER.ARRIVE_DROP` | 到达并减少后续阶段的期望计数 |
| `MBARRIER.PHASEPARITY` | 查询当前阶段奇偶性 |

### 3.3 DPX 指令 (Dynamic Programming Accelerator)

**硬件描述**：

DPX 是用于加速动态规划算法（如 Smith-Waterman、Floyd-Warshall）的 INT 单元扩展指令。在整数单元上执行，三操作数比较/选择操作。

**相关 SASS 指令**：

| 指令 | 功能 |
|------|------|
| `VIMNMX` | 三操作数整数 min/max |
| `VIADD3` | 三操作数整数加法 + 钳位/饱和 |

**建模难度**：极低。这些是纯计算指令，可直接映射到 `INTP_OP` 或新的 specialized unit。

### 3.4 Thread Block Clusters

**硬件描述**：

Thread Block Cluster 是 SM90 引入的新层次结构，位于 CTA 和 Grid 之间：

```
Grid → Clusters → Thread Blocks (CTAs) → Warps → Threads
```

- 一个 Cluster 包含 1-8 个 CTA
- Cluster 内 CTA 保证被调度到物理相邻的 SM 上
- Cluster 内 CTA 共享 Distributed Shared Memory
- 支持 `cluster.arrive` / `cluster.wait` 跨 SM 同步

**注意**：Accel-Sim 中现有的 `simt_core_cluster` 概念（物理 SM 分组）**不等于** Hopper 的 Thread Block Cluster（软件定义的 CTA 分组）。

### 3.5 Distributed Shared Memory (DSMEM)

**硬件描述**：

DSMEM 允许 Cluster 内任何 CTA 的线程访问其他 CTA（位于不同 SM 上）的共享内存：

- 本地共享内存：延迟 ~28 cycles（与现有相同）
- 远程共享内存：额外的跨 SM 互连延迟（估计 ~100-200 cycles）
- 地址空间需包含目标 SM 标识

**当前 Accel-Sim 的限制**：`ldst_unit::shared_cycle()` 将所有共享内存访问视为纯本地操作，无跨 SM 通信建模。

---

## 4. 可行性评估

### 4.1 改动性质分类

| 特性 | 配置改动 | 新 C++ 代码 | 核心架构改动 | 预估代码量 |
|------|---------|------------|-------------|-----------|
| 基础 ISA + Config | `hopper_opcode.h` + configs | ~50 行（trace_driven.cc 注册） | 无 | ~500 行 |
| DPX 指令 | opcode 枚举 + 映射 | 极少 | 无 | ~50 行 |
| mbarrier | 无 | `mbarrier_set_t` 新类 + 等待逻辑 | 中等（shader.cc） | ~400-600 行 |
| TMA | specialized unit 配置 | `tma_unit` 新类 + 异步计数 | 中等（ldst_unit 扩展） | ~800-1200 行 |
| Thread Block Clusters | kernel_info 扩展 | Cluster 感知调度 + 跨 SM barrier | 较大（gpu-sim.cc） | ~600-1000 行 |
| DSMEM | memory_space 枚举扩展 | 跨 SM 地址解析 + 互连延迟 | 大（memory subsystem） | ~1000-1500 行 |

### 4.2 关键技术挑战

**挑战 1：TMA 的 CTA 级语义 vs 现有 Warp 级粒度**

现有的 `ldst_unit` 以 warp 为粒度处理内存请求。TMA 是 CTA 级别的单次 bulk 传输，由单个线程发起。需要新的机制来：
- 识别 TMA 指令并跳过 per-lane 地址处理
- 建模 bulk transfer 的带宽和延迟（不是 32 个独立的内存请求，而是一个大的连续传输）
- 将完成通知路由到 mbarrier 而非 warp scoreboard

**挑战 2：mbarrier 与现有 barrier 机制的共存**

当前 `barrier_set_t` 基于 warp 集合（`m_bar_id_to_warps`），mbarrier 基于计数器。两者需要共存，因为 Hopper 同时支持传统 `BAR.SYNC` 和新的 `MBARRIER.*`。

**挑战 3：Cluster CTA 调度约束**

当前 CTA 调度是贪心的（`distribute_cta_to_shader()`），不感知 Cluster 约束。需要确保同一 Cluster 的所有 CTA 被分配到物理相邻的 SM 上。这涉及调度器状态追踪和可能的调度延迟。

**挑战 4：DSMEM 的跨 SM 通信建模**

当前共享内存是 SM 本地的，不经过互连网络。DSMEM 需要：
- 在 `memory_space_t` 中区分本地/远程共享内存
- 远程访问需经过 SM 间互连（类似 `icnt_wrapper` 的方式）
- 需要新的地址解析逻辑：从线性 DSMEM 地址计算目标 SM ID

### 4.3 对 Tracer 工具的影响

Accel-Sim 使用 NVBit-based tracer 收集 GPU 执行 trace。Hopper 支持需要：

- Tracer 能在 SM90 硬件上运行（需要 NVBit 支持 SM90）
- Tracer 能正确捕获 TMA、mbarrier 等新指令的操作数和地址
- Trace 格式可能需要扩展以包含 TMA 描述符信息和 mbarrier 状态

**注意**：如果 NVBit 尚不支持 SM90，可以先使用手工构造的 trace 进行验证。

---

## 5. 实施计划

### Phase 0：基础架构支持（预计 1-2 周）

**目标**：使 Accel-Sim 能够加载和模拟 Hopper 的基础计算指令（不含 TMA/mbarrier/Cluster/DSMEM）。

**Step 0.1：创建 Hopper ISA 定义文件**

新建 `gpu-simulator/ISA_Def/hopper_opcode.h`：

```cpp
#ifndef HOPPER_OPCODE_H
#define HOPPER_OPCODE_H

#include "trace_opcode.h"
#include "abstract_hardware_model.h"

#define HOPPER_H100_BINARY_VERSION 90

static const std::unordered_map<std::string, OpcodeChar> Hopper_OpcodeMap = {
    // 继承 Ampere 所有指令映射
    // 新增 Hopper 特有指令
    {"VIMNMX", OpcodeChar(OP_VIMNMX, INTP_OP)},     // DPX
    {"VIADD3", OpcodeChar(OP_VIADD3, INTP_OP)},      // DPX
    // ... 完整的 SM90 SASS 指令集
};

#endif
```

**Step 0.2：扩展 trace_opcode.h**

在 `enum TraceInstrOpcode` 末尾（`SASS_NUM_OPCODES` 之前）添加：

```cpp
// unique insts for hopper
OP_VIMNMX,         // DPX min/max
OP_VIADD3,         // DPX 3-op add
OP_CPASYNCBULK,    // TMA bulk copy (Phase 2)
OP_CPASYNCBULKCOMMIT,   // TMA commit
OP_CPASYNCBULKWAITGROUP, // TMA wait
OP_MBARRIER_INIT,   // mbarrier init (Phase 1)
OP_MBARRIER_ARRIVE, // mbarrier arrive
OP_MBARRIER_WAIT,   // mbarrier wait
OP_MBARRIER_ARRIVE_DROP,
OP_MBARRIER_ARRIVE_EXPECT_TX,
OP_TENSORMAP,       // Tensor map operations
OP_CLUSTER_ARRIVE,  // Cluster sync (Phase 3)
OP_CLUSTER_WAIT,
```

**Step 0.3：注册 Hopper 架构到 trace parser**

修改 `gpu-simulator/trace-driven/trace_driven.cc`：

```cpp
#include "../ISA_Def/hopper_opcode.h"

// 在 trace_kernel_info_t 构造函数中添加：
else if (kernel_trace_info->binary_verion == HOPPER_H100_BINARY_VERSION)
    OpcodeMap = &Hopper_OpcodeMap;
```

**Step 0.4：创建 H100 硬件配置**

新建目录 `gpu-simulator/gpgpu-sim/configs/tested-cfgs/SM90_H100/`，以 SM80_A100 为基础修改：

```ini
# H100 SXM5 关键参数
-gpgpu_compute_capability_major 9
-gpgpu_compute_capability_minor 0
-gpgpu_ptx_force_max_capability 90
-gpgpu_n_clusters 132         # 132 SMs
-gpgpu_n_mem 40               # 40 memory controllers (HBM3)
-gpgpu_n_sub_partition_per_mchannel 4
-gpgpu_clock_domains 1830:1830:1830:2619  # Core:ICNT:L2:HBM3

# SM 配置
-gpgpu_shader_registers 65536
-gpgpu_shader_core_pipeline 2048:32
-gpgpu_shader_cta 32
-gpgpu_sub_core_model 1
-gpgpu_num_sched_per_core 4

# 执行单元
-gpgpu_pipeline_widths 4,4,4,4,4,4,4,4,4,4,8,4,4
-gpgpu_num_sp_units 4
-gpgpu_num_dp_units 4
-gpgpu_num_int_units 4
-gpgpu_num_sfu_units 4
-gpgpu_tensor_core_avail 1
-gpgpu_num_tensor_core_units 4

# L1/共享内存（256 KB unified）
-gpgpu_adaptive_cache_config 1
-gpgpu_shmem_option 0,8,16,32,64,100,132,164,228
-gpgpu_unified_l1d_size 256
-gpgpu_shmem_size 232448      # 228 KB max shared memory
-gpgpu_shmem_sizeDefault 232448
-gpgpu_shmem_per_block 49152
-gpgpu_coalesce_arch 90

# L2 缓存（50 MB total）
-gpgpu_cache:dl2 S:256:128:24,L:B:m:L:X,A:192:4,32:0,32

# HBM3 timing
-gpgpu_dram_buswidth 32
-gpgpu_dram_burst_length 2
-gpgpu_dram_timing_opt nbk=16:CCD=1:RRD=4:RCD=14:RAS=32:RP=14:RC=46:CL=14:WL=4:CDLR=5:WR=12:nbkgrp=4:CCDL=4:RTPL=4
```

**Step 0.5：创建 trace.config**

```ini
-trace_opcode_latency_initiation_int 4,1
-trace_opcode_latency_initiation_sp 4,1
-trace_opcode_latency_initiation_dp 8,4
-trace_opcode_latency_initiation_sfu 21,8
-trace_opcode_latency_initiation_tensor 16,4

-specialized_unit_1 1,4,4,4,4,BRA
-specialized_unit_2 1,4,200,4,4,TEX
-specialized_unit_3 1,4,32,4,4,TENSOR
-specialized_unit_4 1,4,4,4,4,UDP
# Phase 2: TMA unit
# -specialized_unit_5 1,1,100,4,4,TMA
```

**交付物**：能够运行基础 Hopper trace（纯计算 + 标准 load/store），DPX 指令正常工作。

---

### Phase 1：mbarrier 支持（预计 2-3 周）

**目标**：实现 mbarrier 同步原语，为 TMA 集成打基础。

**Step 1.1：定义 mbarrier 数据结构**

在 `shader.h` 中新增 `mbarrier_t` 和 `mbarrier_set_t`：

```cpp
// 单个 mbarrier 对象
struct mbarrier_t {
    unsigned smem_addr;         // mbarrier 在共享内存中的地址
    unsigned expected_count;    // 期望到达计数
    unsigned arrived_count;     // 已到达计数
    unsigned expected_tx_bytes; // 期望的 TMA 传输字节数（Phase 2 用）
    unsigned completed_tx_bytes;// 已完成的 TMA 传输字节数
    unsigned phase;             // 当前阶段（用于流水线同步）
    bool is_valid;

    bool is_complete() const {
        return arrived_count >= expected_count &&
               completed_tx_bytes >= expected_tx_bytes;
    }
};

// SM 级别的 mbarrier 管理器
class mbarrier_set_t {
public:
    void init(unsigned smem_addr, unsigned expected_count);
    void arrive(unsigned smem_addr);
    void arrive_expect_tx(unsigned smem_addr, unsigned tx_bytes);
    void arrive_drop(unsigned smem_addr);
    void complete_tx(unsigned smem_addr, unsigned bytes);  // TMA 完成回调
    bool test_wait(unsigned smem_addr, unsigned phase_parity) const;
    void invalidate_cta(unsigned cta_id);  // CTA 退出时清理

private:
    std::unordered_map<unsigned, mbarrier_t> m_mbarriers;  // addr → mbarrier
};
```

**Step 1.2：在 shader_core_ctx 中集成**

```cpp
// shader.h - shader_core_ctx 类中添加：
mbarrier_set_t m_mbarrier_set;

// shader.cc - issue_warp() 中添加 mbarrier 指令处理：
case OP_MBARRIER_INIT:
    m_mbarrier_set.init(/* smem_addr */, /* count */);
    break;
case OP_MBARRIER_ARRIVE:
    m_mbarrier_set.arrive(/* smem_addr */);
    break;
case OP_MBARRIER_WAIT:
    // 设置 warp 等待状态
    m_warp[warp_id]->set_waiting_mbarrier(smem_addr, phase_parity);
    break;
```

**Step 1.3：实现 warp 等待逻辑**

在 `shd_warp_t` 中添加：
```cpp
bool m_waiting_mbarrier;
unsigned m_mbarrier_addr;
unsigned m_mbarrier_phase;
```

在 `shd_warp_t::waiting()` 中检查 mbarrier 状态，若 `m_mbarrier_set.test_wait()` 为真则释放 warp。

**Step 1.4：处理 trace 解析**

在 `trace_driven.cc` 的 `parse_from_trace_struct()` 中添加 mbarrier 指令的 case 分支，需要从 trace 中提取 `smem_addr` 和 `count` 参数。

**交付物**：mbarrier init/arrive/wait 语义正确，可通过单元测试验证。

---

### Phase 2：TMA 支持（预计 3-4 周）

**目标**：建模 TMA 的异步 bulk 数据传输。

**Step 2.1：定义 TMA 操作数据结构**

```cpp
// abstract_hardware_model.h 或新文件 tma_unit.h
struct tma_request_t {
    unsigned cta_id;           // 发起 CTA
    unsigned warp_id;          // 发起 warp
    new_addr_type global_addr; // 全局内存起始地址
    unsigned smem_addr;        // 共享内存目标地址
    unsigned bytes;            // 传输字节数
    unsigned mbarrier_addr;    // 关联的 mbarrier 地址
    unsigned long long issue_cycle;  // 发射时间
    unsigned dimensions;       // tensor 维度（1-5D）
    // tensor shape info for multi-dimensional transfers
    unsigned dim_sizes[5];
    unsigned dim_strides[5];
};
```

**Step 2.2：实现 tma_unit 类**

有两种设计选择：

**选项 A：作为 specialized_unit 的子类**（推荐，改动较小）

```cpp
class tma_unit : public specialized_unit {
public:
    void issue(register_set &source_reg) override;
    void cycle() override;  // 推进在途 TMA 请求

private:
    std::queue<tma_request_t> m_pending_tma;  // 在途 TMA 请求队列
    unsigned m_bandwidth_bytes_per_cycle;       // TMA 带宽模型
};
```

**选项 B：作为 ldst_unit 的扩展**（更精确，改动更大）

在 `ldst_unit` 中新增 TMA 请求队列和处理逻辑，复用现有的内存系统接口。

**建议采用选项 A**，原因：
1. 通过 specialized unit 框架注册，不需要修改流水线阶段枚举
2. TMA 的带宽模型相对独立，不需要与现有 cache 系统深度耦合
3. 后续可以迁移到选项 B 以获得更高精度

**Step 2.3：TMA 发射逻辑**

在 `issue_warp()` 或 `tma_unit::issue()` 中：
1. 检测 `CPASYNCBULK` 指令
2. 跳过 per-lane 地址处理（TMA 是 CTA 级操作）
3. 从指令操作数中提取 tensor map 描述符信息
4. 创建 `tma_request_t` 并入队
5. 对关联的 mbarrier 调用 `arrive_expect_tx()`

**Step 2.4：TMA 完成回调**

在 `tma_unit::cycle()` 中：
1. 根据带宽模型推进在途请求
2. 当传输完成时，调用 `m_mbarrier_set.complete_tx(mbarrier_addr, bytes)`
3. mbarrier 检测到所有 TX 完成后，释放等待的 warp

**Step 2.5：TMA 带宽建模**

简化模型：
```
transfer_cycles = bytes / min(l2_bandwidth, dram_bandwidth) + latency_overhead
```

更精确的模型需要考虑：
- L2 cache 命中率
- DRAM 行缓冲命中
- TMA 请求之间的流水线重叠
- 与其他内存请求的带宽竞争

**交付物**：`cp.async.bulk` 操作的基础建模，TMA → mbarrier 完成通知链路正常工作。

---

### Phase 3：Thread Block Clusters（预计 3-4 周）

**目标**：实现 Cluster 感知的 CTA 调度和跨 SM 同步。

**Step 3.1：扩展 kernel 信息**

```cpp
// abstract_hardware_model.h - kernel_info_t 类
unsigned m_cluster_dim_x, m_cluster_dim_y, m_cluster_dim_z;
unsigned get_cluster_size() const {
    return m_cluster_dim_x * m_cluster_dim_y * m_cluster_dim_z;
}
unsigned get_cluster_id(dim3 cta_id) const;  // CTA → Cluster 映射
```

**Step 3.2：修改 CTA 调度器**

当前调度逻辑（`gpu-sim.cc` 中的 `distribute_cta_to_shader()`）需要：

1. 在分配 CTA 之前，检查 Cluster 约束
2. 同一 Cluster 的所有 CTA 必须分配到同一 `simt_core_cluster` 的 SM 上
3. 如果当前 cluster 没有足够的空闲 SM 来容纳整个 Cluster，则延迟调度

```cpp
// 伪代码
bool can_schedule_cluster(unsigned cluster_id, kernel_info_t &kernel) {
    unsigned cluster_size = kernel.get_cluster_size();
    unsigned hw_cluster_id = find_hw_cluster_for(cluster_id);
    unsigned free_sms = count_free_sms_in_hw_cluster(hw_cluster_id);
    return free_sms >= cluster_size;
}
```

**Step 3.3：实现 Cluster Barrier**

```cpp
class cluster_barrier_t {
public:
    void arrive(unsigned sm_id, unsigned cta_id);
    bool test_wait(unsigned cluster_id) const;

private:
    // cluster_id → {expected CTAs, arrived CTAs}
    std::unordered_map<unsigned, std::pair<unsigned, unsigned>> m_barriers;
};
```

Cluster barrier 需要在 `simt_core_cluster` 级别维护，因为它涉及跨 SM 的同步。

**Step 3.4：处理 Cluster 同步指令**

`CLUSTER.ARRIVE` 和 `CLUSTER.WAIT` 需要：
1. 在 `issue_warp()` 中捕获
2. 通过 `simt_core_cluster` 的接口传播到 cluster barrier
3. 所有 CTA 到达后释放

**交付物**：Cluster 感知的 CTA 调度，跨 SM 的 cluster barrier 同步。

---

### Phase 4：Distributed Shared Memory（预计 4-6 周）

**目标**：建模 Cluster 内跨 SM 的共享内存访问。

**Step 4.1：扩展 memory_space_t**

```cpp
// abstract_hardware_model.h
enum _memory_space_t {
    // ... 现有类型
    shared_space,           // 本地共享内存（不变）
    distributed_shared_space, // 远程共享内存（新增）
};
```

**Step 4.2：DSMEM 地址解析**

```cpp
// 新函数：从 DSMEM 地址中提取目标 SM
struct dsmem_addr_info {
    unsigned target_sm_id;    // 目标 SM
    unsigned local_smem_addr; // 在目标 SM 上的本地偏移
    bool is_remote;           // 是否跨 SM
};

dsmem_addr_info resolve_dsmem_addr(new_addr_type addr, unsigned src_sm_id,
                                     unsigned cluster_size);
```

**Step 4.3：修改 shared_cycle()**

```cpp
void ldst_unit::shared_cycle(warp_inst_t &inst, mem_stage_stall_type &rc_fail,
                              mem_stage_access_type &fail_type) {
    if (inst.space.get_type() == distributed_shared_space) {
        dsmem_addr_info info = resolve_dsmem_addr(inst.get_addr(0), ...);
        if (info.is_remote) {
            // 远程 DSMEM 访问：通过互连网络发送请求
            // 建模跨 SM 延迟
            inject_dsmem_request_to_icnt(inst, info);
            return;
        }
    }
    // 本地共享内存：走现有逻辑
    // ...
}
```

**Step 4.4：跨 SM 互连建模**

选项：
1. **简化模型**：固定延迟（如 100 cycles）+ 带宽限制
2. **精确模型**：复用现有 `icnt_wrapper` 互连网络，在 SM 间添加 DSMEM 流量

建议先采用简化模型，验证功能正确后再优化精度。

**Step 4.5：DSMEM 写回路径**

远程 DSMEM 写操作需要通过互连网络将数据发送到目标 SM 的共享内存，需要在目标 SM 上添加接收逻辑。

**交付物**：Cluster 内跨 SM 共享内存读写，简化延迟模型。

---

## 6. 风险分析与缓解措施

### 6.1 高风险

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|---------|
| NVBit 不支持 SM90 tracer | 无法采集真实 Hopper trace | 高 | 使用 NVBit 2.0 或 NVIDIA Nsight Compute 的 SASS dump；或手工构造 micro-benchmark trace |
| TMA tensor map 描述符信息在 trace 中不可见 | 无法准确建模 TMA 行为 | 中 | 在 trace 格式中扩展 TMA 元信息字段；或使用启发式推断 |
| DSMEM 实现引入仿真性能退化 | 大规模仿真变慢 | 中 | 简化模型优先；仅在检测到 DSMEM 指令时启用跨 SM 建模 |

### 6.2 中风险

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|---------|
| Cluster 调度约束导致 SM 利用率下降 | 仿真结果不准确 | 中 | 参考 H100 实际调度策略；从小 Cluster（2 CTA）开始验证 |
| mbarrier + TMA 的竞态条件 | 仿真死锁或结果错误 | 中 | 完善单元测试；添加 assertion 检查 mbarrier 状态一致性 |
| H100 硬件参数不准确 | 仿真与实际不符 | 低 | 使用 micro-benchmark 标定参数；参考 NVIDIA 白皮书和 GH200 技术手册 |

### 6.3 低风险

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|---------|
| DPX 指令延迟不准确 | 轻微影响模拟精度 | 低 | 使用 micro-benchmark 标定 |
| 与上游 Accel-Sim 合并冲突 | 维护成本增加 | 低 | 保持模块化设计，最小化对核心代码的侵入 |

---

## 7. 验证策略

### 7.1 单元测试

| 测试目标 | 方法 |
|---------|------|
| ISA 映射正确性 | 对每条 Hopper 指令验证 opcode → 执行单元映射 |
| mbarrier 语义 | 构造 init→arrive→wait 序列，验证 warp 阻塞/释放 |
| TMA 完成通知 | 验证 TMA 完成后 mbarrier.complete_tx 被正确调用 |
| Cluster 调度 | 验证同一 Cluster 的 CTA 被分配到同一 hw_cluster |

### 7.2 Micro-benchmark 验证

设计一组小型 CUDA kernel 来验证每个特性：

1. **DPX**：Smith-Waterman 动态规划 kernel
2. **mbarrier**：Producer-consumer 流水线 kernel
3. **TMA**：矩阵 tile 加载 kernel（`cp.async.bulk.tensor.2d`）
4. **Cluster**：Cluster 内规约 kernel
5. **DSMEM**：跨 CTA 数据交换 kernel

在 H100 硬件上运行并收集 trace，然后在仿真器中回放，比较：
- IPC（Instructions Per Cycle）
- 内存带宽利用率
- 共享内存 bank conflict 率
- 内核执行时间（cycle 级别）

### 7.3 应用级验证

使用真实应用验证端到端准确性：

- **FlashAttention-3**（重度使用 TMA + mbarrier）
- **cuBLAS GEMM**（使用 TMA + Tensor Core + Cluster）
- **CUTLASS 3.x**（全面使用 Hopper 特性）

---

## 8. 附录：关键文件索引

### 8.1 需要新建的文件

| 文件 | 内容 |
|------|------|
| `gpu-simulator/ISA_Def/hopper_opcode.h` | Hopper SASS 指令 → 执行单元映射 |
| `gpu-simulator/gpgpu-sim/configs/tested-cfgs/SM90_H100/gpgpusim.config` | H100 硬件配置 |
| `gpu-simulator/configs/tested-cfgs/SM90_H100/trace.config` | H100 指令延迟配置 |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/mbarrier.h` | mbarrier 数据结构（Phase 1） |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/tma_unit.h/cc` | TMA 执行单元（Phase 2） |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/cluster_barrier.h` | Cluster barrier（Phase 3） |

### 8.2 需要修改的文件

| 文件 | 修改内容 | Phase |
|------|---------|-------|
| `gpu-simulator/ISA_Def/trace_opcode.h` | 新增 Hopper opcode 枚举 | 0 |
| `gpu-simulator/trace-driven/trace_driven.cc` | 注册 Hopper 架构 + 解析新指令 | 0, 1, 2 |
| `gpu-simulator/gpgpu-sim/src/abstract_hardware_model.h` | 新增 op 类型、memory space | 0, 4 |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/shader.h` | mbarrier 集成、warp 等待状态 | 1 |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/shader.cc` | issue_warp() 扩展、mbarrier 逻辑、DSMEM shared_cycle | 1, 2, 4 |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/gpu-sim.cc` | Cluster 感知调度、specialized unit 注册 | 0, 3 |
| `gpu-simulator/gpgpu-sim/src/abstract_hardware_model.cc` | SM90 内存合并规则 | 0 |

### 8.3 参考文档

- [NVIDIA H100 Whitepaper](https://resources.nvidia.com/en-us-tensor-core)
- [NVIDIA CUDA Binary Utilities - Hopper Instruction Set](https://docs.nvidia.com/cuda/cuda-binary-utilities/)
- [PTX ISA 8.x - TMA and mbarrier](https://docs.nvidia.com/cuda/parallel-thread-execution/)
- [CUTLASS 3.x CuTe Documentation](https://github.com/NVIDIA/cutlass/blob/main/media/docs/cute/)
- Accel-Sim: [ISCA 2020 Paper](https://doi.org/10.1109/ISCA45697.2020.00047)

---

## 时间线总结

```
Phase 0: 基础 ISA + Config          [Week 1-2]    ████
Phase 1: mbarrier                    [Week 3-5]    ██████
Phase 2: TMA                         [Week 5-9]    ████████
Phase 3: Thread Block Clusters       [Week 8-12]   ████████
Phase 4: DSMEM                       [Week 11-17]  ████████████
                                                    ↑ Phase 2&3 可部分并行
```

**总预估工期**：约 14-17 周（单人全职），Phase 0-2 为核心里程碑。
