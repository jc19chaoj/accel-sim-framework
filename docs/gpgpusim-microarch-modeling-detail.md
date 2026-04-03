# GPGPU-Sim 4.0 微架构建模细节

> **日期**: 2026-04-04
> **范围**: 记录 GPGPU-Sim 4.0 中各子模块之间的队列/FIFO 深度、Outstanding Buffer、L2/SM 连接拓扑、NoC、仲裁路由、地址哈希等微架构仿真细节
> **参考配置**: A100 (SM80)，配置文件 `gpu-simulator/gpgpu-sim/configs/tested-cfgs/SM80_A100/gpgpusim.config`

---

## 目录

1. [整体数据通路](#1-整体数据通路)
2. [SM 核心流水线级间寄存器](#2-sm-核心流水线级间寄存器)
3. [Warp 指令缓冲](#3-warp-指令缓冲)
4. [Operand Collector 与寄存器 Bank 冲突](#4-operand-collector-与寄存器-bank-冲突)
5. [共享内存 Bank 冲突](#5-共享内存-bank-冲突)
6. [L1 缓存队列与 MSHR](#6-l1-缓存队列与-mshr)
7. [互联网络 (NoC / Crossbar)](#7-互联网络-noc--crossbar)
8. [L2 缓存与内存分区队列](#8-l2-缓存与内存分区队列)
9. [DRAM 队列与调度](#9-dram-队列与调度)
10. [地址哈希与分区映射](#10-地址哈希与分区映射)
11. [反压传播机制](#11-反压传播机制)
12. [全链路队列深度汇总表](#12-全链路队列深度汇总表)

---

## 1. 整体数据通路

一条访存请求从 SM 到 DRAM 的完整路径如下：

```
SM Core (LD/ST Unit)
  │
  ├─ L1D Cache (per-SM)
  │   ├─ Hit → 返回数据
  │   └─ Miss → miss_queue
  │
  ▼
m_icnt_L2_queue ─── Interconnect (Crossbar, REQ_NET) ──→ m_icnt_L2_queue
                                                            │
                                                            ▼
                                                    L2 Cache (per sub-partition)
                                                      ├─ Hit → m_L2_icnt_queue
                                                      └─ Miss → m_L2_dram_queue
                                                                    │
                                                                    ▼
                                                            m_dram_latency_queue (ROP 延迟)
                                                                    │
                                                                    ▼
                                                            DRAM (rwq → mrqq → bank)
                                                                    │
                                                                    ▼
                                                            returnq → m_dram_L2_queue
                                                                    │
                                                                    ▼
                                                    L2 fill → m_L2_icnt_queue
                                                                    │
                                                                    ▼
                    Interconnect (Crossbar, REPLY_NET) ──→ SM Core (writeback)
```

---

## 2. SM 核心流水线级间寄存器

### 流水线阶段枚举

定义于 `shader.h:1480-1501`：

```cpp
enum pipeline_stage_name_t {
  ID_OC_SP = 0,          // Issue/Decode → Operand Collect (SP)
  ID_OC_DP,              // Issue/Decode → Operand Collect (DP)
  ID_OC_INT,             // Issue/Decode → Operand Collect (INT)
  ID_OC_SFU,             // Issue/Decode → Operand Collect (SFU)
  ID_OC_MEM,             // Issue/Decode → Operand Collect (MEM)
  OC_EX_SP,              // Operand Collect → Execute (SP)
  OC_EX_DP,              // Operand Collect → Execute (DP)
  OC_EX_INT,             // Operand Collect → Execute (INT)
  OC_EX_SFU,             // Operand Collect → Execute (SFU)
  OC_EX_MEM,             // Operand Collect → Execute (MEM)
  EX_WB,                 // Execute → Writeback
  ID_OC_TENSOR_CORE,     // Issue/Decode → Operand Collect (Tensor Core)
  OC_EX_TENSOR_CORE,     // Operand Collect → Execute (Tensor Core)
  N_PIPELINE_STAGES       // = 13
};
```

### 流水线宽度（Pipeline Register 容量）

每两级之间的 `register_set` 容量由配置控制：

```
# A100 配置
-gpgpu_pipeline_widths 4,4,4,4,4,4,4,4,4,4,8,4,4
```

对应关系：

| 级间寄存器 | 宽度 | 含义 |
|-----------|------|------|
| ID_OC_SP | 4 | 每周期最多 4 条 SP 指令从 Issue 进入 Operand Collect |
| ID_OC_DP | 4 | 同上，DP 指令 |
| ID_OC_INT | 4 | 同上，INT 指令 |
| ID_OC_SFU | 4 | 同上，SFU 指令 |
| ID_OC_MEM | 4 | 同上，MEM 指令 |
| OC_EX_SP | 4 | 每周期最多 4 条 SP 指令从 OC 进入 Execute |
| OC_EX_DP | 4 | 同上，DP |
| OC_EX_INT | 4 | 同上，INT |
| OC_EX_SFU | 4 | 同上，SFU |
| OC_EX_MEM | 4 | 同上，MEM |
| EX_WB | 8 | 每周期最多 8 条指令从 Execute 写回 |
| ID_OC_TENSOR_CORE | 4 | Tensor Core 专用 |
| OC_EX_TENSOR_CORE | 4 | Tensor Core 专用 |

### 反压机制

`register_set::test(latency)` 检查目标级间寄存器是否有空位。当 `register_set` 满时，上游阶段 **stall**，指令无法推进。

**源码位置**: `shader.h:110-122`（`register_set` 类定义），`shader.cc:1828`（`test_res_bus()` 检查执行结果总线是否可用）

---

## 3. Warp 指令缓冲

### 结构

定义于 `shader.h:282-306`：

```cpp
class shd_warp_t {
  static const unsigned IBUFFER_SIZE = 2;  // 硬编码，2 条指令
  struct ibuffer_entry {
    const warp_inst_t *m_inst;
    bool m_valid;
  };
  ibuffer_entry m_ibuffer[IBUFFER_SIZE];
  unsigned m_next;                          // 当前读指针
};
```

### 行为

- **Fetch/Decode** 阶段每周期向 `m_ibuffer` 填入 1~2 条指令
- **Issue** 阶段从 `m_ibuffer[m_next]` 读取并发射
- `ibuffer_step()` 推进指针到下一条
- 当 `m_ibuffer` 为空时，warp 无法被调度器选中

### 配置

固定 2 条，不可配置。这意味着每个 warp 最多提前解码 2 条指令，为调度器提供有限的前瞻能力。

---

## 4. Operand Collector 与寄存器 Bank 冲突

### 架构

定义于 `shader.h:640-1039`，核心类 `opndcoll_rfu_t`：

```
Scheduler → allocate_cu() → Collector Unit → add_read_requests() → Bank Arbiter
                                                                         │
                                                                  allocate_reads()
                                                                  (二部图匹配)
                                                                         │
                                                                  dispatch_ready_cu() → Execute
```

### 关键组件

| 组件 | 类名 | 说明 |
|------|------|------|
| Collector Unit | `collector_unit_t` | 收集单条指令所需的所有操作数 |
| Bank Arbiter | `arbiter_t` | per-bank 请求队列 + 轮询匹配 |
| Operand | `op_t` | 描述一个操作数（寄存器号 → bank 编号） |

### Bank 映射函数

```cpp
unsigned register_bank(int regnum, int wid, unsigned num_banks,
                       bool sub_core_model, unsigned banks_per_sched,
                       unsigned sched_id);
```

寄存器号和 warp ID 共同决定 bank 编号。

### 冲突模型

- 每个 bank 每周期只能服务 **一个读** 或 **一个写**
- 多个 Collector Unit 请求同一 bank → 仅一个被授权，其余等待
- `arbiter_t` 使用 per-collector 轮询指针 `m_allocator_rr_head` 做公平仲裁

### A100 配置

```
-gpgpu_sub_core_model 1                        # 每个调度器有独立寄存器堆和执行单元
-gpgpu_enable_specialized_operand_collector 0   # 使用通用 Operand Collector
-gpgpu_operand_collector_num_units_gen 8        # 8 个 Collector Unit
-gpgpu_operand_collector_num_in_ports_gen 8     # 8 个输入端口
-gpgpu_operand_collector_num_out_ports_gen 8    # 8 个输出端口
-gpgpu_num_reg_banks 32                         # 32 个寄存器 bank
-gpgpu_reg_file_port_throughput 2               # 每 bank 每周期吞吐 2
```

---

## 5. 共享内存 Bank 冲突

### 配置

```
-gpgpu_shmem_size 167936              # 总共享内存 ~164KB
-gpgpu_shmem_per_block 49152          # 每 block 最大 48KB
-gpgpu_smem_latency 28                # 共享内存命中延迟 28 cycles
-gpgpu_shmem_num_banks 32             # 32 个 bank（与硬件一致）
-gpgpu_shmem_limited_broadcast 0      # 不限制广播
-gpgpu_shmem_warp_parts 1             # warp 分区数
-gpgpu_coalesce_arch 80               # 地址合并架构版本
```

### 冲突检测

同一 warp 内多个线程在同一周期访问同一 bank 的不同地址时产生冲突，访问被序列化，延迟倍增。统计计数器 `gpgpu_n_shmem_bkconflict` 记录冲突总数。

---

## 6. L1 缓存队列与 MSHR

### L1D 缓存配置

```
-gpgpu_cache:dl1 S:4:128:64,L:T:m:L:L,A:512:64,16:0,32
```

解析：

| 字段 | 值 | 含义 |
|------|---|------|
| 类型 | S (Sector) | Sector cache |
| Sets | 4 | 4 个 set |
| Line Size | 128 B | 每行 128 字节 |
| Associativity | 64 | 64 路组相联 |
| MSHR 类型 | A (Associative) | 关联式 MSHR |
| MSHR 条目数 | 512 | 最多 512 个 outstanding miss |
| MSHR 最大合并 | 64 | 每个 MSHR 条目最多合并 64 个请求 |
| Miss Queue | 16 | L1 miss → ICNT 的请求队列深度 |
| Result FIFO | 0 | 无独立结果 FIFO |
| 端口宽度 | 32 B | 每周期 32 字节数据端口 |

### MSHR 结构（`gpu-cache.h:569-680`）

```cpp
unsigned m_mshr_entries;       // 最大 MSHR 条目 = 512
unsigned m_mshr_max_merge;     // 每条目最大合并数 = 64
unsigned m_miss_queue_size;    // Miss Queue 大小 = 16
unsigned m_result_fifo_entries;// Result FIFO 大小 = 0
```

### 其他 L1 相关队列

```
-gpgpu_l1_banks 4                              # 4 个 L1 cache bank
-gpgpu_l1_latency 34                           # L1 命中延迟 34 cycles
-gpgpu_n_cluster_ejection_buffer_size 32       # SM cluster 出口缓冲 32 条目
```

L1 Latency Queue（`shader.cc:2068-2138`）：每个 L1 bank 有一条深度为 `l1_latency` 的流水线延迟队列 `l1_latency_queue[bank][stage]`，模拟 L1 访问的固定延迟。

### 反压

`miss_queue_full()` 返回 true 时，L1 cache 拒绝新的 miss 请求 → LD/ST 单元 stall → Operand Collector stall → Issue stall。

---

## 7. 互联网络 (NoC / Crossbar)

### 支持的互联模型

| 模式 | 配置值 | 说明 |
|------|--------|------|
| InterSim2 | `network_mode 1` | 外部 NoC 模拟器，支持 mesh/torus/butterfly/fat-tree 等拓扑 |
| Local Crossbar | `network_mode 2` | 内置全连接交叉开关（A100 默认） |

### Local Crossbar 架构

**源码**: `local_interconnect.h:56-113`，`local_interconnect.cc`

```
         108 个 SM (输入端口)
              │
    ┌─────────┼─────────┐
    │   xbar_router      │   ×2 (REQ_NET + REPLY_NET)
    │                    │
    │  in_buffers[108]   │   每个输入端口一个 FIFO 队列
    │       ↓            │
    │  仲裁引擎 (iSLIP)   │   每周期选择获胜者
    │       ↓            │
    │  out_buffers[160]  │   每个输出端口一个 FIFO 队列
    │                    │
    └─────────┼─────────┘
              │
         160 个 L2 子分区 (输出端口)
```

### A100 配置

```
-network_mode 2                  # Local Crossbar
-icnt_in_buffer_limit 512        # 每个输入端口缓冲上限 512 packets
-icnt_out_buffer_limit 512       # 每个输出端口缓冲上限 512 packets
-icnt_subnets 2                  # 2 个子网（请求网 + 回复网）
-icnt_flit_size 40               # flit 大小 40 字节
-icnt_arbiter_algo 1             # 1 = iSLIP
```

### 仲裁算法

定义于 `local_interconnect.h:40-44`：

```cpp
enum Arbiteration_type {
  NAIVE_RR = 0,   // 朴素轮询：全局单指针轮转
  iSLIP,          // iSLIP：每个输出端口独立 grant 指针，多轮匹配
  PERFECT          // 完美仲裁：无冲突（用于理想化测试）
};
```

#### iSLIP 算法（`local_interconnect.cc`中 `iSLIP_Advance()`）

- 基于 McKeown 的 iSLIP 调度算法（IEEE/ACM Trans. Networking, 1999）
- 每个输出端口维护独立的 grant 指针 `next_node[output_id]`
- 每周期：每个输入选择目标输出，每个输出从请求者中按 grant 指针轮转选择获胜者
- 比朴素 RR 在高负载下有更好的吞吐和公平性

#### Naive RR 算法（`RR_Advance()`）

- 全局单指针 `next_node_id` 在所有输入间轮转
- 每周期每个输入最多发送一个 flit 到目标输出
- 多个输入竞争同一输出时记录 conflict

### 统计

`xbar_router` 跟踪以下指标：
- `conflicts` — 端口冲突次数
- `out_buffer_full` — 输出缓冲满导致的阻塞
- `in_buffer_full` — 输入缓冲满导致的阻塞
- `in_buffer_util` / `out_buffer_util` — 缓冲利用率

### 路由

Local Crossbar 模式下路由是 **直接确定性** 的：
- 每个 packet 携带目标 output_deviceID
- 单级交叉开关，无多跳路由
- 目标 L2 分区由地址哈希函数在 SM 端计算确定（见第 10 节）

---

## 8. L2 缓存与内存分区队列

### 分区结构

A100 有 40 个内存控制器（`gpgpu_n_mem 40`），每个控制器 4 个子分区（`gpgpu_n_sub_partition_per_mchannel 4`），共 **160 个 L2 子分区**。

### 子分区内部队列

定义于 `l2cache.h:226-230`，使用 `fifo_pipeline<mem_fetch>` 模板：

```cpp
fifo_pipeline<mem_fetch> *m_icnt_L2_queue;    // ICNT → L2（请求入口）
fifo_pipeline<mem_fetch> *m_L2_dram_queue;    // L2 → DRAM（miss 请求）
fifo_pipeline<mem_fetch> *m_dram_L2_queue;    // DRAM → L2（返回数据）
fifo_pipeline<mem_fetch> *m_L2_icnt_queue;    // L2 → ICNT（hit 回复）
```

#### `fifo_pipeline` 模板（`delayqueue.h:46-187`）

```cpp
template<class T> class fifo_pipeline {
  unsigned m_min_len;      // 最小长度（建模流水线传播延迟）
  unsigned m_max_len;      // 最大容量（满时产生反压）
  unsigned m_length;       // 当前长度
  unsigned m_n_element;    // 非空元素数

  bool full()  { return (m_max_len && m_length >= m_max_len); }
  bool empty() { return m_head == NULL; }
};
```

### A100 配置

```
-gpgpu_dram_partition_queues 64:64:64:64
```

| 队列 | 深度 | 方向 |
|------|------|------|
| `m_icnt_L2_queue` | 64 | ICNT → L2 |
| `m_L2_dram_queue` | 64 | L2 → DRAM |
| `m_dram_L2_queue` | 64 | DRAM → L2 |
| `m_L2_icnt_queue` | 64 | L2 → ICNT（hit 回复） |

### L2 缓存配置

```
-gpgpu_cache:dl2 S:128:128:16,L:B:m:L:X,A:192:4,32:0,32
```

| 字段 | 值 | 含义 |
|------|---|------|
| Sets | 128 | 128 个 set |
| Line Size | 128 B | 每行 128 字节 |
| Associativity | 16 | 16 路组相联 |
| MSHR 条目 | 192 | 每子分区 192 个 outstanding miss |
| MSHR 合并 | 4 | 每条目最多合并 4 个请求 |
| Miss Queue | 32 | L2 miss → DRAM 的请求队列 |

### ROP 延迟队列

```
-gpgpu_l2_rop_latency 200    # ROP 固定延迟 200 cycles
```

`memory_sub_partition` 中的 `m_rop` 队列（`l2cache.h:224`）模拟 ROP 单元的固定延迟：请求进入 L2 前先在此队列中等待 200 cycles。

### Sub-partition 间 DRAM 仲裁

定义于 `l2cache.h:120-147`：

```cpp
class arbitration_metadata {
  int m_last_borrower;              // 上次获得 DRAM 访问权的子分区
  int m_shared_credit_limit;        // 共享信用池上限
  int m_private_credit_limit;       // 每子分区私有信用上限
  std::vector<int> m_private_credit;// 各子分区已借信用
  int m_shared_credit;              // 共享池已借信用
};
```

同一 memory partition 下的 4 个子分区共享一个 DRAM 通道，通过 **信用制（credit-based）** 仲裁：
- 每个子分区有私有信用额度
- 超出私有额度后从共享池借用
- 共享池耗尽时，子分区无法向 DRAM 发请求
- 防止某个子分区独占 DRAM 带宽

### DRAM 延迟队列

```cpp
// l2cache.h:153-157
struct dram_delay_t {
  unsigned long long ready_cycle;
  class mem_fetch *req;
};
std::list<dram_delay_t> m_dram_latency_queue;
```

模拟 L2 到 DRAM 之间的固定延迟：

```
-dram_latency 190    # DRAM 固定延迟 190 cycles
```

---

## 9. DRAM 队列与调度

### DRAM 内部队列

定义于 `dram.h:169-173`：

```cpp
fifo_pipeline<dram_req_t> *rwq;       // 读写请求队列（入口）
fifo_pipeline<dram_req_t> *mrqq;      // 内存请求队列（调度队列）
fifo_pipeline<mem_fetch>  *returnq;   // 返回队列（已完成请求）
```

### A100 配置

```
-gpgpu_dram_scheduler 1                       # 1 = FR-FCFS, 0 = FIFO
-gpgpu_frfcfs_dram_sched_queue_size 64        # FR-FCFS 调度队列深度
-gpgpu_dram_return_queue_size 192             # 返回队列深度
```

### DRAM 请求结构（`dram.h:53-71`）

```cpp
class dram_req_t {
  unsigned int row, col, bk;    // DRAM 行/列/bank 地址
  unsigned int nbytes, txbytes; // 请求大小和已传输字节
  unsigned char rw;             // 'R' 读 / 'W' 写
  unsigned int insertion_time;  // 入队时间（用于 FCFS 排序）
  class mem_fetch *data;        // 关联的内存请求
};
```

### Bank 与 Bank Group 时序模型

定义于 `dram.h:73-98`：

```cpp
struct bankgrp_t {
  unsigned int CCDLc;    // 同 bank group 内列到列延迟计数器
  unsigned int RTPLc;    // 同 bank group 内读到预充电延迟计数器
};

struct bank_t {
  unsigned int RCDc;     // Row-to-Column Delay 计数器
  unsigned int RCDWRc;   // Row-to-Column Delay (Write) 计数器
  unsigned int RASc;     // Row Active Strobe 计数器
  unsigned int RPc;      // Row Precharge 计数器
  unsigned int RCc;      // Row Cycle 计数器
  unsigned int WTPc;     // Write-to-Precharge 计数器
  unsigned int RTPc;     // Read-to-Precharge 计数器
  unsigned char rw;      // 当前读/写状态
  unsigned char state;   // BANK_IDLE / BANK_ACTIVE
  unsigned int curr_row; // 当前激活行
  dram_req_t *mrq;       // 当前正在服务的请求
};
```

### A100 DRAM 时序参数

```
-gpgpu_dram_timing_opt nbk=16:CCD=1:RRD=7:RCD=22:RAS=50:RP=22:RC=72:CL=22:WL=4:CDLR=5:WR=19:nbkgrp=4:CCDL=4:RTPL=7
```

| 参数 | 值 | 含义 |
|------|---|------|
| nbk | 16 | 每通道 16 个 bank |
| nbkgrp | 4 | 4 个 bank group |
| CCD | 1 | Column-to-Column Delay（跨 bank group） |
| CCDL | 4 | Column-to-Column Delay（同 bank group 内） |
| RRD | 7 | Row-to-Row Delay（不同 bank 的 activate 间隔） |
| RCD | 22 | Row-to-Column Delay（activate → read/write） |
| RAS | 50 | Row Active Strobe（activate → precharge 最小间隔） |
| RP | 22 | Row Precharge 延迟 |
| RC | 72 | Row Cycle（activate → 下次 activate） |
| CL | 22 | CAS Latency（读延迟） |
| WL | 4 | Write Latency |
| CDLR | 5 | CAS-to-Data Latency for Read |
| WR | 19 | Write Recovery |
| RTPL | 7 | Read-to-Precharge Latency（同 bank group） |

### 调度算法

#### FIFO（`dram_t::scheduler_fifo()`）
- 简单 FCFS，按入队时间顺序服务

#### FR-FCFS（`dram_t::scheduler_frfcfs()`，A100 默认）
- **First-Ready**：优先服务目标行已激活（row buffer hit）的请求
- **First-Come-First-Served**：在同等优先级下按入队顺序服务
- 最大化 row buffer 利用率，减少 row activate/precharge 开销

### 跨 Bank 时序约束

`dram_t` 类维护全局计数器（`dram.h:159-163`）：

```cpp
unsigned int RRDc;    // Row-to-Row Delay 计数器（跨 bank）
unsigned int CCDc;    // Column-to-Column Delay 计数器（跨 bank）
unsigned int RTWc;    // Read-to-Write 切换惩罚
unsigned int WTRc;    // Write-to-Read 切换惩罚
```

### Bank 索引函数

```
-dram_bnk_indexing_policy 0        # 0 = LINEAR, 1 = BITWISE_XOR, 2 = IPOLY
-dram_bnkgrp_indexing_policy 1     # Bank group: 1 = 低位选择（增加 bank group 并行度）
```

定义于 `dram.h:100-107`：

```cpp
enum bank_index_function {
  LINEAR_BK_INDEX = 0,
  BITWISE_XORING_BK_INDEX,
  IPOLY_BK_INDEX,
  CUSTOM_BK_INDEX
};
```

### DRAM 带宽利用率统计

`dram_t` 跟踪详细的性能统计（`dram.h:175-230`）：
- `wasted_bw_row` / `wasted_bw_col` — 由于 row/col 冲突浪费的带宽
- `util_bw` / `idle_bw` — 有效带宽 / 空闲带宽
- `RCDc_limit` / `CCDLc_limit` / `WTRc_limit` / `RTWc_limit` — 各时序约束导致的 stall 次数
- `hits_num` / `hits_read_num` / `hits_write_num` — row buffer 命中统计
- `banks_acess_total` — bank 级并行度（BLP）统计

---

## 10. 地址哈希与分区映射

### 地址解码结构

定义于 `addrdec.h:48-58`：

```cpp
struct addrdec_t {
  unsigned chip;           // DRAM 通道 ID
  unsigned bk;             // DRAM bank
  unsigned row;            // DRAM 行
  unsigned col;            // DRAM 列
  unsigned burst;          // Burst 偏移
  unsigned sub_partition;  // L2 子分区 ID（由 chip 和 bk 计算）
};
```

### 地址比特位映射

```
-gpgpu_mem_addr_mapping dramid@8;00000000.00000000.00000000.00000000.0000RRRR.RRRRRRRR.RBBBCCCC.BCCSSSSS
```

- `dramid@8` — chip/channel ID 从第 8 位开始提取
- `R` — row 位
- `B` — bank 位
- `C` — column 位
- `S` — burst（sub-block）位

### 分区索引函数

定义于 `addrdec.h:39-46`：

```cpp
enum partition_index_function {
  CONSECUTIVE = 0,         // 线性连续分配
  BITWISE_PERMUTATION,     // XOR 位置换
  IPOLY,                   // 多项式哈希（GF(2) 域）
  PAE,                     // 页地址熵哈希
  RANDOM,                  // 随机哈希
  CUSTOM
};
```

A100 配置：`-gpgpu_memory_partition_indexing 0`（CONSECUTIVE）

### IPOLY 哈希算法详解

**源码**: `hashing.cc:9-97`

基于 Rau 等人 "Pseudo-randomly interleaved memory"（ISCA 1991）的 IPOLY 方案。在 GF(2) 域上进行多项式运算，保证所有 2^n 步长的访问 **完全无冲突**。

以 32 分区为例（使用 IPOLY(37)）：

```cpp
// hashing.cc:52-67
std::bitset<64> a(higher_bits);  // 高位地址
std::bitset<5> b(index);         // 原始分区索引
std::bitset<5> new_index(index);

new_index[0] = a[13]^a[12]^a[11]^a[10]^a[9]^a[6]^a[5]^a[3]^a[0]^b[0];
new_index[1] = a[14]^a[13]^a[12]^a[11]^a[10]^a[7]^a[6]^a[4]^a[1]^b[1];
new_index[2] = a[14]^a[10]^a[9]^a[8]^a[7]^a[6]^a[3]^a[2]^a[0]^b[2];
new_index[3] = a[11]^a[10]^a[9]^a[8]^a[7]^a[4]^a[3]^a[1]^b[3];
new_index[4] = a[12]^a[11]^a[10]^a[9]^a[8]^a[5]^a[4]^a[2]^b[4];
```

支持的分区数：16（IPOLY(5)）、32（IPOLY(37)）、64（IPOLY(67)）。

### 其他哈希函数

**Bitwise XOR**（`hashing.cc:99-102`）：
```cpp
return (index) ^ (higher_bits & (bank_set_num - 1));
```

**PAE（Page Address Entropy）**（`hashing.cc:104-125`）：
- 基于 Liu et al. "Get Out of the Valley: Power-Efficient Address..."
- 从页地址和 bank 位中随机选取若干位做 XOR
- 仅支持 32 分区

---

## 11. 反压传播机制

整个系统的反压链如下（从 DRAM 到 SM）：

```
DRAM rwq full
  → memory_partition_unit::can_issue_to_dram() = false
    → m_L2_dram_queue 积压
      → L2interface::full() = true
        → L2 cache 拒绝 miss
          → m_icnt_L2_queue 积压
            → icnt_out_buffer full
              → icnt_in_buffer 积压（Has_Buffer_Out() = false）
                → SM: miss_queue_full() = true
                  → L1 cache access stall
                    → LD/ST unit stall
                      → Operand Collector stall
                        → scheduler_unit 无法发射 MEM 指令
                          → warp stall (等待访存完成)
```

每一级都有明确的 `full()` 检查，满时上游停止注入，形成逐级反压。

---

## 12. 全链路队列深度汇总表

以 A100 (SM80) 配置为准：

| 组件 | 队列/缓冲 | 深度 | 配置参数 | 源码位置 |
|------|----------|------|---------|---------|
| **SM Pipeline** | | | | |
| Warp Instruction Buffer | `m_ibuffer` | 2 条（硬编码） | 不可配置 | `shader.h:282` |
| Pipeline Registers | `register_set` | 4~8 条/级 | `-gpgpu_pipeline_widths` | `shader.h:110` |
| Operand Collector Units | `collector_unit_t` | 8 个 | `-gpgpu_operand_collector_num_units_gen` | `shader.h:932` |
| Register File Banks | `arbiter_t` | 32 个 bank | `-gpgpu_num_reg_banks` | `shader.h:824` |
| Shared Memory Banks | — | 32 个 bank | `-gpgpu_shmem_num_banks` | config |
| **L1D Cache** | | | | |
| L1D MSHR | `mshr_table` | 512 条，merge=64 | `-gpgpu_cache:dl1 ...A:512:64` | `gpu-cache.h:569` |
| L1D Miss Queue | `m_miss_queue` | 16 | `-gpgpu_cache:dl1 ...16:0` | `gpu-cache.h:1396` |
| L1 Latency Pipeline | `l1_latency_queue` | 34 级 | `-gpgpu_l1_latency` | `shader.cc:2068` |
| L1 Banks | — | 4 个 | `-gpgpu_l1_banks` | config |
| Cluster Ejection Buffer | — | 32 | `-gpgpu_n_cluster_ejection_buffer_size` | config |
| **Interconnect** | | | | |
| Input Buffer (per port) | `in_buffers` | 512 packets | `-icnt_in_buffer_limit` | `local_interconnect.h:97` |
| Output Buffer (per port) | `out_buffers` | 512 packets | `-icnt_out_buffer_limit` | `local_interconnect.h:98` |
| Subnets | `net` | 2（req+reply） | `-icnt_subnets` | `local_interconnect.h:141` |
| Flit Size | — | 40 B | `-icnt_flit_size` | config |
| **L2 / Memory Partition** | | | | |
| ICNT → L2 Queue | `m_icnt_L2_queue` | 64 | `-gpgpu_dram_partition_queues 64:...` | `l2cache.h:227` |
| L2 → DRAM Queue | `m_L2_dram_queue` | 64 | `-gpgpu_dram_partition_queues ..:64:..` | `l2cache.h:228` |
| DRAM → L2 Queue | `m_dram_L2_queue` | 64 | `-gpgpu_dram_partition_queues ..:64:.` | `l2cache.h:229` |
| L2 → ICNT Queue | `m_L2_icnt_queue` | 64 | `-gpgpu_dram_partition_queues ..:..:64` | `l2cache.h:230` |
| L2 MSHR | `mshr_table` | 192 条，merge=4 | `-gpgpu_cache:dl2 ...A:192:4` | `gpu-cache.h:569` |
| L2 Miss Queue | `m_miss_queue` | 32 | `-gpgpu_cache:dl2 ...32:0` | `gpu-cache.h:1396` |
| ROP Delay Queue | `m_rop` | 无上限（延迟 200 cyc） | `-gpgpu_l2_rop_latency` | `l2cache.h:224` |
| DRAM Delay Queue | `m_dram_latency_queue` | 无上限（延迟 190 cyc） | `-dram_latency` | `l2cache.h:157` |
| Sub-partition Arbitration | `arbitration_metadata` | 信用制 | 内部计算 | `l2cache.h:120` |
| **DRAM** | | | | |
| Read/Write Queue | `rwq` | — | 内部 FIFO | `dram.h:169` |
| Scheduling Queue (FR-FCFS) | `mrqq` | 64 | `-gpgpu_frfcfs_dram_sched_queue_size` | `dram.h:170` |
| Return Queue | `returnq` | 192 | `-gpgpu_dram_return_queue_size` | `dram.h:173` |
| Banks | `bk` | 16 per channel | `nbk=16` | `dram.h:148` |
| Bank Groups | `bkgrp` | 4 per channel | `nbkgrp=4` | `dram.h:146` |

---

## 附录 A：关键源文件索引

| 文件路径（相对 `gpu-simulator/gpgpu-sim/`） | 内容 |
|--------------------------------------------|------|
| `src/gpgpu-sim/shader.h` | SM 核心、流水线、调度器、Operand Collector |
| `src/gpgpu-sim/shader.cc` | SM 核心周期函数、各流水线阶段实现 |
| `src/gpgpu-sim/gpu-sim.cc` | 顶层 `cycle()` 函数、多时钟域驱动 |
| `src/gpgpu-sim/gpu-cache.h` | Cache 模型、MSHR、Miss Queue、Result FIFO |
| `src/gpgpu-sim/l2cache.h` | L2 cache、内存分区队列、子分区仲裁 |
| `src/gpgpu-sim/dram.h` / `dram.cc` | DRAM 模型、Bank 时序、FR-FCFS 调度 |
| `src/gpgpu-sim/local_interconnect.h` / `.cc` | Local Crossbar、iSLIP 仲裁 |
| `src/gpgpu-sim/icnt_wrapper.h` | 互联网络封装层 |
| `src/gpgpu-sim/addrdec.h` / `addrdec.cc` | 地址解码、分区映射 |
| `src/gpgpu-sim/hashing.h` / `hashing.cc` | IPOLY / Bitwise / PAE 哈希函数 |
| `src/gpgpu-sim/delayqueue.h` | `fifo_pipeline` 模板（队列/延迟建模基础设施） |

## 附录 B：A100 时钟域配置

```
-gpgpu_clock_domains 1410:1410:1410:1512
```

| 时钟域 | 频率 (MHz) | 驱动的组件 |
|--------|-----------|-----------|
| Core | 1410 | SM pipeline、L1 cache |
| ICNT | 1410 | 互联网络 |
| L2 | 1410 | L2 cache、内存分区队列 |
| DRAM | 1512 | DRAM 控制器、bank 时序 |

各时钟域独立推进，在 `gpgpu_sim::cycle()` 中通过 `next_clock_domain()` 返回的 `clock_mask` 决定每个仿真周期应推进哪些域。
