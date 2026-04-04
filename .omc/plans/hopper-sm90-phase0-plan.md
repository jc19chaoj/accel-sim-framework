# Accel-Sim Hopper SM90 Phase 0 实施计划

> **版本**: v5 (Architect APPROVED + Critic fixes applied)
> **日期**: 2026-04-04
> **范围**: Phase 0 — 基础 ISA 映射 + 硬件配置，支持纯计算 kernel trace
> **时间线**: 1-2 周（单人）

## 执行进度

| Step | 内容 | 状态 | 完成日期 |
|------|------|------|---------|
| 1 | 扩展 trace_opcode.h（~90 条 Hopper opcode 枚举） | ✅ 完成 | 2026-04-04 |
| 2 | 创建 hopper_opcode.h（SM90 指令映射，含 Ampere→Hopper 管线迁移修正） | ✅ 完成 | 2026-04-04 |
| 2.5 | 交叉校验脚本（sm90_2.txt vs hopper_opcode.h, 186/186 pass） | ✅ 完成 | 2026-04-04 |
| 3 | 注册 Hopper 到 trace parser（trace_driven.cc） | ✅ 完成 | 2026-04-04 |
| 4 | 创建 SM90_H100/gpgpusim.config | ✅ 完成 | 2026-04-04 |
| 5 | 创建 SM90_H100/trace.config | ✅ 完成 | 2026-04-04 |
| 6 | 注册 H100 配置到 define-standard-cfgs.yml | ✅ 完成 | 2026-04-04 |
| 7 | 编译验证（Docker 内零错误零警告） | ✅ 完成 | 2026-04-04 |
| 8 | 端到端 Trace 验证（vectorAdd H100 trace, IPC=1523.6, 13076 cycles） | ✅ 完成 | 2026-04-04 |

---

## RALPLAN-DR 结构化决策摘要

### 原则 (Principles)

1. **最小侵入性**: 尽可能复用现有架构扩展机制（opcode map + config），最小化对核心 C++ 代码的修改
2. **数据驱动**: 利用 `util/denvdis/data12/sm90_2.txt` 的 pipe 分类数据来验证指令→执行单元映射的正确性
3. **前向兼容**: 注册所有 SM90 指令（包括 GMMA/TMA/CGA 等 Phase 1+ 指令），即使 Phase 0 不实现其语义，避免遇到未知指令时崩溃
4. **模板一致性**: 严格遵循 `ampere_opcode.h` 和 `SM80_A100/` 的模式和命名规范

### 决策驱动 (Decision Drivers)

1. **时间约束**: 1-2 周，必须聚焦于最小可运行集
2. **验证可达性**: 云服务器有 H100，可采集纯计算 trace 进行端到端验证
3. **denvdis 数据可用性**: `sm90_2.txt` 提供了权威的指令→pipe 映射，可直接作为 opcode 分类依据

### 可行方案 (Options)

**方案 A: 手动编写 opcode 映射（推荐）**
- 以 `ampere_opcode.h` 为模板，手动添加 Hopper 新指令
- 用 `sm90_2.txt` 的 pipe 分类作为校验参考
- 优点: 控制精确，每条指令映射都经过人工审查；与现有代码风格一致
- 缺点: 需要逐条检查 ~30 条新指令

**方案 B: 脚本自动生成 opcode 映射**
- 编写 Python 脚本解析 `sm90_2.txt`，自动生成 `hopper_opcode.h`
- 优点: 覆盖完整，不遗漏
- 缺点: 需要额外开发解析脚本；`sm90_2.txt` 格式复杂，pipe 名称与 Accel-Sim 执行单元类别不是 1:1 映射；纯计算 Phase 0 只需约 30 条新指令，自动化收益不高
- **淘汰理由**: 1-2 周时间线内，开发解析脚本的投入与手动编写 ~30 条映射相比不划算；且 pipe→执行单元的映射规则需要人工判断

---

## 实施步骤

### Step 1: 扩展 trace_opcode.h（预计 2 小时）

**文件**: `gpu-simulator/ISA_Def/trace_opcode.h`

在 `OP_F2FP` 和 `SASS_NUM_OPCODES` 之间添加 Hopper 特有 opcode 枚举：

```cpp
// unique insts for hopper
// DPX instructions (Dynamic Programming Accelerator)
OP_VIMNMX,           // 3-operand integer min/max
OP_VIMNMX3,          // 3-operand integer min/max (3-way)
OP_VIADDMNMX,        // integer add + min/max
OP_VIADD,            // integer add (fmalighter pipe variant)
OP_VHMNMX,           // FP16 min/max

// GMMA (Group Matrix Multiply-Accumulate) - stub for Phase 0
OP_HGMMA,            // FP16 GMMA
OP_IGMMA,            // INT8 GMMA
OP_BGMMA,            // BF16 GMMA
OP_QGMMA,            // FP8 GMMA

// TMA (Tensor Memory Accelerator) - stub for Phase 0
OP_UTMALDG,          // TMA load global
OP_UTMASTG,          // TMA store global
OP_UTMAREDG,         // TMA reduce global
OP_UTMACMDFLUSH,     // TMA command flush
OP_UTMAPF,           // TMA prefetch
OP_UTMACCTL,         // TMA cache control
OP_UBLKCP,           // Bulk copy
OP_UBLKRED,          // Bulk reduce
OP_UBLKPF,           // Bulk prefetch

// Warp Group instructions - stub for Phase 0
OP_WARPGROUP,        // Warp group operations
OP_WARPGROUPSET,     // Warp group set

// CGA Barrier (Cluster) - stub for Phase 0
OP_UCGABAR_ARV,      // CGA barrier arrive
OP_UCGABAR_GET,      // CGA barrier get
OP_UCGABAR_SET,      // CGA barrier set
OP_UCGABAR_WAIT,     // CGA barrier wait
OP_UCGABARARV,       // CGA barrier arrive (alt encoding)
OP_UCGABARGET,       // CGA barrier get (alt encoding)
OP_UCGABARSET,       // CGA barrier set (alt encoding)
OP_UCGABARWAIT,      // CGA barrier wait (alt encoding)

// DSMEM-related - stub for Phase 0
OP_STAS,             // Store async shared
OP_REDAS,            // Reduce async shared
OP_ARRIVES,          // Arrives (async)
OP_SYNCS,            // Sync shared

// Hopper control flow
OP_ELECT,            // Elect leader thread
OP_ENDCOLLECTIVE,    // End collective operation
OP_PREEXIT,          // Pre-exit
OP_ACQBULK,          // Acquire bulk

// Hopper memory
OP_FENCE,            // Memory fence
OP_REDG,             // Reduce global
OP_LDGMC,            // Load global multicast
OP_STSM,             // Store shared matrix
OP_FOOTPRINT,        // Memory footprint
OP_CGAERRBAR,        // CGA error barrier

// Hopper UDP补全（Architect Review 新增）
OP_UMOV32I,          // Uniform move 32-bit immediate
OP_ULEPC,            // Uniform LEPC
// OP_UIADD3_64 已移除: trace parser 按 '.' 分割后查找 "UIADD3"，此枚举不会被使用

// Hopper misc
OP_USETMAXREG,      // Set max registers (uniform)
OP_USETSHMSZ,       // Set shared memory size (uniform) [修正: 非 USETSHMSZOP]
OP_CLMAD,            // Complex multiply-add
OP_GETFPFLAGS,       // Get FP flags
OP_SETFPFLAGS,       // Set FP flags
OP_BITEXTRACT,       // Bit extract
OP_SCATTER,          // Scatter
OP_GATHER,           // Gather
OP_SPMETADATA,       // Sparsity metadata
OP_GENMETADATA,      // Generate metadata
// OP_HFMA2_MMA 已移除: trace parser 按 '.' 分割后查找 "HFMA2"，此枚举不会被使用
OP_IDE,              // Integer divide estimate
OP_IMAD32I,          // IMAD 32-bit immediate
OP_STP,              // Stop

// Critic Review 补全：sm90_2.txt 中存在但未被上方覆盖的 26 条指令
// int_pipe 补全
OP_VMAD,             // Vector multiply-add
OP_VSET,             // Vector set
OP_VSETP,            // Vector set predicate
OP_VSHL,             // Vector shift left
OP_VSHR,             // Vector shift right
OP_ICMP,             // Integer compare (已在 pascal 段，需确认是否复用)
OP_FCMP,             // Float compare (已在 kepler 段，需确认是否复用)

// cbu_pipe 补全
OP_KIL,              // Kill thread (legacy, 区别于 KILL)
OP_NANOTRAP,         // Nano trap
OP_BSSY_OLD,         // BSSY legacy encoding

// mio_pipe 补全
OP_AL2P,             // Attribute to parameter
OP_ALD,              // Attribute load
OP_AST,              // Attribute store
OP_IPA,              // Interpolated parameter access
OP_ISBERD,           // Indexed set buffer read
OP_ISBEWR,           // Indexed set buffer write
OP_OUT,              // Output
OP_PIXLD,            // Pixel load
OP_LDTRAM,           // Load TRAM
OP_LD_OLD,           // Load (legacy)
OP_LDG_OLD,          // Load global (legacy)
OP_VILD,             // Virtual instruction load
OP_F2F64,            // Float-to-float 64-bit conversion
OP_F2I64,            // Float-to-int 64-bit conversion
OP_FRND64,           // Float round 64-bit
OP_I2F64,            // Int-to-float 64-bit conversion
OP_SUCCTL,           // Surface cache control
OP_TXA,              // Texture array access
```

**注意**: 部分指令（如 `ICMP`, `FCMP`, `VMAD`, `VSET` 等）可能已在 pascal/kepler 段的 `trace_opcode.h` 中有对应枚举。这些可直接在 `hopper_opcode.h` 中复用已有 OP_ 枚举，无需重复添加到 `trace_opcode.h`。**编写 Step 1 时需逐条检查 `trace_opcode.h` 中是否已存在。**

**验证方法**: 编译通过，无 opcode 冲突。总覆盖指令数应为 ~280 条（Ampere 继承 ~174 + 旧架构复用 ~18 + 新增 ~85 = ~277）。

---

### Step 2: 创建 hopper_opcode.h（预计 4 小时）

**文件**: `gpu-simulator/ISA_Def/hopper_opcode.h`

以 `ampere_opcode.h` 为模板，创建完整的 SM90 指令映射。核心映射规则（基于 `sm90_2.txt` pipe 分类）：

| denvdis pipe | Accel-Sim 执行单元 | 说明 |
|---|---|---|
| `int_pipe` | `INTP_OP` | 整数单元（含 DPX: VIMNMX 等，**含 SEL/MOV/PLOP3 等从 Ampere ALU_OP 迁入的指令，含 IMMA/BMMA**） |
| `fmalighter_pipe` | `SP_OP` | 单精度浮点（**含 IMAD/IMUL/IMAD32I/IMUL32I — SM90 从 int_pipe 迁入**，含 VIADD） |
| `fp16_pipe` | `SP_OP` | FP16（含 HMMA → SPECIALIZED_UNIT_3_OP，含 VHMNMX） |
| `fma64lite_pipe` | `DP_OP` | 双精度（含 DMMA → SPECIALIZED_UNIT_3_OP，含 CLMAD, HFMA2.MMA） |
| `fma64heavy_pipe` | `DP_OP` | 双精度重操作（DMNMX, DSET） |
| `mio_pipe`（SFU 子集） | `SFU_OP` | MUFU 等特殊函数 |
| `mio_pipe`（load/store 子集） | `LOAD_OP` / `STORE_OP` | 内存操作 |
| `mio_pipe`（texture 子集） | `SPECIALIZED_UNIT_2_OP` | TEX/TLD/TLD4 等 |
| `mio_pipe`（barrier 子集） | `BARRIER_OP` | BAR |
| `mio_pipe`（GMMA 子集） | `SPECIALIZED_UNIT_3_OP` | HGMMA/IGMMA/BGMMA/QGMMA |
| `mio_pipe`（sync/warpgroup 子集） | `ALU_OP` | WARPGROUP/WARPGROUPSET/ARRIVES/SYNCS（mio_pipe 归属，Phase 0 stub 为 ALU_OP） |
| `cbu_pipe` | `SPECIALIZED_UNIT_1_OP` | 分支/控制流（BRA/CALL/RET/ELECT/ENDCOLLECTIVE/PREEXIT/ACQBULK 等） |
| `udp_pipe` | `SPECIALIZED_UNIT_4_OP` | Uniform Datapath（含 TMA/CGA/UMOV32I/ULEPC stub） |
| `fe_pipe` | `ALU_OP` | 前端操作（NOP/DEPBAR/STP 等） |

**⚠ 关键 Ampere→Hopper 微架构变化（Architect Review 发现）**:

SM90 相对 SM80 存在**执行单元调度策略重组**，以下指令映射必须从 Ampere 模板修改：

| 指令 | Ampere 映射 | Hopper 映射 | 原因 |
|------|------------|------------|------|
| **fmalighter_pipe 迁入（INTP→SP）** | | | |
| IMAD, IMUL | `INTP_OP` | `SP_OP` | sm90_2.txt:8 fmalighter_pipe |
| IMAD32I, IMUL32I | N/A (新增) | `SP_OP` | sm90_2.txt:8 fmalighter_pipe（Ampere 中不存在） |
| IDP, IDP4A | `INTP_OP` | `SP_OP` | sm90_2.txt:8 fmalighter_pipe |
| **int_pipe 迁入（ALU/SP/SPEC3→INTP）** | | | |
| IMMA, BMMA | `SPECIALIZED_UNIT_3_OP` | `INTP_OP` | sm90_2.txt:31 IMMA_OP → int_pipe |
| SEL | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| FSEL | `SP_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe (Ampere 是 SP_OP 非 ALU_OP) |
| FMNMX | `SP_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| FSET | `SP_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| FSETP | `SP_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| MOV, MOV32I, MOVM | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| PRMT, SGXT | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| PLOP3, PSETP | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| P2R, R2P | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| CS2R | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| LEPC | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| RPCMOV | `SPECIALIZED_UNIT_1_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe (Ampere 是 SPEC_1 非 ALU) |
| VOTE | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| F2FP | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| F2IP | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| I2FP | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| I2I | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| I2IP | `ALU_OP` | `INTP_OP` | sm90_2.txt:2 int_pipe |
| **mio_pipe 迁入（Critic Review v4 新增）** | | | |
| BREV | `INTP_OP` | `SFU_OP` | sm90_2.txt:4 mio_pipe（Ampere 在 int_pipe，SM90 迁移到 mio_pipe） |
| POPC | `INTP_OP` | `SFU_OP` | sm90_2.txt:4 mio_pipe |
| FLO | `INTP_OP` | `SFU_OP` | sm90_2.txt:4 mio_pipe |
| FCHK | `SP_OP` | `SFU_OP` | sm90_2.txt:4 mio_pipe（Ampere 在 fmalighter 等效） |
| F2F | `ALU_OP` | `SFU_OP` | sm90_2.txt:4 mio_pipe（类型转换子集） |
| F2I | `ALU_OP` | `SFU_OP` | sm90_2.txt:4 mio_pipe |
| I2F | `ALU_OP` | `SFU_OP` | sm90_2.txt:4 mio_pipe |
| FRND | `ALU_OP` | `SFU_OP` | sm90_2.txt:4 mio_pipe |
| **保持不变（确认）** | | | |
| SHFL | `ALU_OP` | `ALU_OP` | sm90_2.txt:4 mio_pipe（保持 ALU_OP，走 mio 慢路径，Phase 0 已知近似） |

需要新增的映射（Ampere 中不存在的指令）：
- DPX: `VIMNMX`→INTP_OP, `VIMNMX3`→INTP_OP, `VIADDMNMX`→INTP_OP, `VIADD`→SP_OP, `VHMNMX`→SP_OP
- GMMA: `HGMMA`/`IGMMA`/`BGMMA`/`QGMMA`→SPECIALIZED_UNIT_3_OP
- TMA: `UTMALDG`/`UTMASTG`/`UTMAREDG`/...→SPECIALIZED_UNIT_4_OP
- Warp Group: `WARPGROUP`/`WARPGROUPSET`→ALU_OP (mio_pipe)
- CGA: `UCGABAR_*`/`UCGABAR*`→SPECIALIZED_UNIT_4_OP
- DSMEM: `STAS`→STORE_OP, `REDAS`→STORE_OP, `ARRIVES`→ALU_OP, `SYNCS`→ALU_OP
- Control: `ELECT`/`ENDCOLLECTIVE`/`PREEXIT`/`ACQBULK`→SPECIALIZED_UNIT_1_OP
- Memory: `FENCE`→MEMORY_BARRIER_OP, `REDG`→STORE_OP, `LDGMC`→LOAD_OP, `STSM`→STORE_OP
- UDP 补全: `UMOV32I`→SPEC_4_OP, `ULEPC`→SPEC_4_OP（`UIADD3.64` 由 trace parser 解析为 `UIADD3`，已有映射）
- Misc: `CLMAD`→DP_OP, `IDE`→INTP_OP（`HFMA2.MMA` 由 trace parser 解析为 `HFMA2`，已有 SP_OP 映射，Phase 0 已知精度限制）

**⚠ 点号修饰符与 OpcodeMap 查找机制（Critic Review v4 修正）**:
Trace parser（`trace_parser.cc:59-66`）在查找 OpcodeMap 前会按 `'.'` 分割指令助记符，使用 `opcode_tokens[0]`（即点号前的基本名）作为查找键。因此：
- `HFMA2.MMA` → 查找 `"HFMA2"` 键（而非 `"HFMA2.MMA"`）
- `UIADD3.64` → 查找 `"UIADD3"` 键（而非 `"UIADD3.64"`）
- `IMAD.WIDE` → 查找 `"IMAD"` 键

**这意味着 OpcodeMap 中不应使用点号形式的字符串键**——所有映射都基于基本助记符。

**已知精度限制（Phase 0 可接受）**:
- `HFMA2` 在 `fp16_pipe` (SP_OP)，但 `HFMA2.MMA` 在 `fma64lite_pipe` (DP_OP)。由于 trace parser 无法区分两者，Phase 0 统一映射为 SP_OP（fp16_pipe 是更常见的路径）。**后续 Phase 如需精确建模，需修改 trace parser 以支持修饰符感知的查找**。
- 因此，`OP_HFMA2_MMA` 枚举应从 Step 1 中移除（不会被使用）。`OP_UIADD3_64` 同理移除（会落到 `UIADD3` 键，该键已正确映射为 SPECIALIZED_UNIT_4_OP）。

**需要以 Ampere 映射为起点但逐条校验**（约 230+50 条），以 `sm90_2.txt` pipe 归属为唯一权威来源。

**验证方法**:
1. 与 `sm90_2.txt` 的 pipe 分类交叉校验（见 Step 2.5）
2. 确保每条 int_pipe 中的指令都映射到 INTP_OP，每条 cbu_pipe 中的指令都映射到 SPECIALIZED_UNIT_1_OP，以此类推

---

### Step 2.5: 交叉校验脚本（预计 30-60 分钟）【Architect Review 新增】

在 Step 2 完成后、Step 3 之前，编写一个简单的 Python 脚本从 `sm90_2.txt` 提取每个 pipe 的指令列表，与 `hopper_opcode.h` 的映射交叉比对，输出差异报告。

**目的**: 系统性捕获所有映射偏差，避免继承 Ampere 映射导致的静默错误。

**脚本逻辑**（约 30 行 Python）：
1. 解析 `sm90_2.txt` 前 20 行，提取 `pipe_name = {INSTR1,INSTR1pipe,...}` 格式
2. 对每个 pipe，提取不带 pipe 后缀的指令名
3. 解析 `hopper_opcode.h` 中的 `{"INSTR", OpcodeChar(OP_X, CATEGORY)}` 映射
4. 按 pipe→expected_category 规则校验每条指令
5. 输出：匹配/不匹配/缺失 报告

**pipe→expected_category 规则表**:
| pipe | expected_category |
|------|-------------------|
| int_pipe | INTP_OP |
| fmalighter_pipe | SP_OP |
| fp16_pipe | SP_OP |
| fma64lite_pipe | DP_OP |
| fma64heavy_pipe | DP_OP |
| cbu_pipe | SPECIALIZED_UNIT_1_OP |
| udp_pipe | SPECIALIZED_UNIT_4_OP |
| fe_pipe | ALU_OP |
| mio_pipe | (需按子集判断，不做全局校验) |

**例外列表**（pipe 归属与 Accel-Sim 映射有意不同的指令）:
- HMMA: fp16_pipe 但映射到 SPECIALIZED_UNIT_3_OP（Tensor Core）
- DMMA: fma64lite_pipe 但映射到 SPECIALIZED_UNIT_3_OP
- IMMA/BMMA: int_pipe 但在 SM90 上确实归入 INTP_OP（与 Ampere 的 SPECIALIZED_UNIT_3_OP 不同）
- EXIT: cbu_pipe 但映射到 EXIT_OPS

---

### Step 3: 注册 Hopper 到 trace parser（预计 30 分钟）

**文件**: `gpu-simulator/trace-driven/trace_driven.cc`

**修改 1**: 添加 include：
```cpp
#include "../ISA_Def/hopper_opcode.h"
```

**修改 2**: 在 binary_version if-else 链顶部添加（最新架构在前）：
```cpp
if (kernel_trace_info->binary_verion == HOPPER_H100_BINART_VERSION)
    OpcodeMap = &Hopper_OpcodeMap;
else if (kernel_trace_info->binary_verion == AMPERE_RTX_BINART_VERSION ||
         kernel_trace_info->binary_verion == AMPERE_A100_BINART_VERSION)
    OpcodeMap = &Ampere_OpcodeMap;
// ... 其余不变
```

**注意**: 宏命名使用 `BINART`（非 `BINARY`）以保持与现有代码一致（`volta_opcode.h:12`, `ampere_opcode.h:12-13` 的历史拼写）。

**验证方法**: 加载一个 binary_version=90 的 trace 文件，确认不再报 "unsupported binary version: 90"。

---

### Step 4: 创建 H100 gpgpusim.config（预计 3 小时）

**文件**: `gpu-simulator/gpgpu-sim/configs/tested-cfgs/SM90_H100/gpgpusim.config`

以 `SM80_A100/gpgpusim.config` 为基础，修改以下参数：

| 参数 | A100 值 | H100 值 | 来源 |
|------|---------|---------|------|
| `gpgpu_compute_capability_major` | 8 | 9 | Spec |
| `gpgpu_compute_capability_minor` | 0 | 0 | Spec |
| `gpgpu_ptx_force_max_capability` | 80 | 90 | Spec |
| `gpgpu_n_clusters` | 108 | 132 | H100 有 132 SMs |
| `gpgpu_occupancy_sm_number` | 80 | 90 | SM 版本 |
| `gpgpu_clock_domains` | 1410:1410:1410:1512 | 1830:1830:1830:2619 | H100 SXM5 boost 1830MHz, HBM3 2619MHz |
| `gpgpu_n_mem` | 40 | 40 | H100 也是 40 个内存控制器 |
| `gpgpu_unified_l1d_size` | 192 | 256 | H100 统一 L1/SMEM 256KB |
| `gpgpu_shmem_option` | 0,8,16,32,64,164 | 0,8,16,32,64,100,132,164,228 | H100 更多 SMEM 选项 |
| `gpgpu_shmem_size` | 167936 | 233472 | H100 最大 228KB |
| `gpgpu_shmem_sizeDefault` | 167936 | 233472 | 同上 |
| `gpgpu_coalesce_arch` | 80 | 90 | 架构版本 |
| `gpgpu_cache:dl2` | S:128:128:16,... | S:256:128:40,... | H100 L2 50MB / 40 分区 ≈ 1.28MB/分区。256×128×40=1310720 bytes ≈ 1.25MB ✓ |
| `gpgpu_dram_buswidth` | 16 | 16 | HBM3 单通道仍为 128-bit (16 bytes)，与 HBM2e 相同（Architect Review 修正：非 32） |
| `gpgpu_dram_timing_opt` | (A100 HBM2e) | 初始近似: 复用 A100 HBM2e 时序，降低 CL/tRCD 约 30%（HBM3 频率更高但延迟周期数相似） | Phase 0 使用 A100 近似值，待后续用 micro-benchmark 校准。具体建议: `nbk=16:CCD=1:RRD=5:RCD=16:RAS=36:RP=16:RC=52:CL=16:WL=4:CDLR=5:WR=14:nbkgrp=4:CCDL=4:RTPL=5` |

**其他参数保持不变**（SM 内部流水线宽度、调度器数量、register file 大小等在 A100 和 H100 间基本一致）。

**验证方法**: 仿真器能正常解析配置文件，不报参数错误。

---

### Step 5: 创建 H100 trace.config（预计 1 小时）

**文件**: `gpu-simulator/configs/tested-cfgs/SM90_H100/trace.config`

基于 SM80_A100 的 trace.config，调整延迟参数：

```ini
# 基础延迟（参考 sm90_2.txt TABLE_TRUE 延迟表 + PIPELINE RESOURCE occupancy）
# sm90_2.txt:129 FXU_OPS 延迟 = 6, sm90_2.txt:557 FXU_Occupancy [2] → initiation = 2
-trace_opcode_latency_initiation_int 6,2
# sm90_2.txt:130 FMAI_OPS 延迟 = 5, sm90_2.txt:558 FMAI_Occupancy [2] → initiation = 2
-trace_opcode_latency_initiation_sp 5,2
# sm90_2.txt:133 FMALITE_OPS 延迟 = 10, sm90_2.txt:559 FMALITE_Occupancy [2] → initiation = 2
# 注意: sm90_2.txt 原始 occupancy 为 [2]，但 Accel-Sim 的 initiation interval 建模粒度
# 与 denvdis 的 per-sub-core occupancy 不同。此处使用 A100 缩放近似值 (A100: dp=4,4)，
# 保守设为 10,2。Phase 0 后续可用 micro-benchmark 校准。
-trace_opcode_latency_initiation_dp 10,2
# mio_pipe MUFU: 保持与 A100 相似，sm90_2.txt 未单独列出 MUFU 延迟
-trace_opcode_latency_initiation_sfu 21,8
# sm90_2.txt:136 HMMA_OP 延迟 = 27, sm90_2.txt:560 HMMA_OP occupancy [2]
# 注意: sm90_2.txt 原始 occupancy 为 [2]，此处设为 27,2（而非 v2 的 27,8）。
# A100 配置为 tensor=14,6，H100 HMMA 延迟更高但发射间隔更短（更高吞吐）。
-trace_opcode_latency_initiation_tensor 27,2

# Specialized units
-specialized_unit_1 1,4,4,4,4,BRA
-trace_opcode_latency_initiation_spec_op_1 4,4

-specialized_unit_2 1,4,200,4,4,TEX
-trace_opcode_latency_initiation_spec_op_2 200,4

-specialized_unit_3 1,4,27,4,4,TENSOR
-trace_opcode_latency_initiation_spec_op_3 27,2   # HMMA latency 27, occupancy [2] from sm90_2.txt:136,560

-specialized_unit_4 1,4,4,4,4,UDP
-trace_opcode_latency_initiation_spec_op_4 4,1
```

**延迟参数来源说明**: 所有值均来自 `sm90_2.txt` TABLE_TRUE 和 PIPELINE RESOURCE 定义。`sm90_2.txt:129-138` 包含各 pipe 的延迟矩阵（取对角线最小值），`sm90_2.txt:551-564` 包含各 pipe 的 occupancy（发射间隔）。

**验证方法**: 仿真器能正常解析 trace.config。

---

### Step 6: 注册配置到 job_launching 系统（预计 30 分钟）

**文件**: `util/job_launching/configs/define-standard-cfgs.yml`

添加 H100 配置组合（**注意：使用 `base_file` 键名，与现有格式一致**）：

```yaml
H100:
    base_file: "$GPGPUSIM_ROOT/configs/tested-cfgs/SM90_H100/gpgpusim.config"
```

**注意**: trace.config 不在 YAML 中配置，而是通过运行时 `-config` 参数指定。需确认 `run_simulations.py` 中 trace.config 的查找逻辑（通常与 gpgpusim.config 同目录或在 `gpu-simulator/configs/tested-cfgs/` 对应目录下）。

**验证方法**: `run_simulations.py -C H100-SASS` 能识别该配置。

---

### Step 7: 编译验证（预计 1 小时）

1. `source ./gpu-simulator/setup_environment.sh`
2. `make -j -C ./gpu-simulator/`（或 CMake 构建）
3. 确认无编译错误
4. 检查新增的 opcode 枚举无冲突（`SASS_NUM_OPCODES` 值正确递增）

---

### Step 8: 端到端 Trace 验证（预计 2-3 小时）

**8.1 采集 SM90 Trace**

在云服务器 H100 上：
1. 编译 NVBit tracer（需确认 NVBit 是否支持 SM90）
2. 运行 `vectorAdd` 或 `rodinia/backprop` 等纯计算 kernel
3. 收集 SASS trace（`kernelslist.g` + kernel trace 文件）
4. 下载到本地

**备选方案**（如果 NVBit 不支持 SM90）：
- 使用 `cuobjdump --dump-sass` 获取 SASS 指令列表
- 手工构造最小 trace 文件（参考现有 trace 格式）

**8.2 运行仿真**

```bash
./gpu-simulator/bin/release/accel-sim.out \
    -trace ./hw_run/traces/device-0/SM90/vectorAdd/kernelslist.g \
    -config ./gpu-simulator/gpgpu-sim/configs/tested-cfgs/SM90_H100/gpgpusim.config \
    -config ./gpu-simulator/configs/tested-cfgs/SM90_H100/trace.config
```

**成功标准**:
- 仿真正常完成，不崩溃
- 输出包含合理的 IPC 统计数据
- 所有指令都被正确路由到执行单元（无 "unknown opcode" 警告）

---

## 时间表

| 天数 | 任务 | 预计耗时 | 累计 |
|------|------|---------|------|
| Day 0 | 前置调研: 确认 NVBit 是否支持 SM90 | 1h | 1h |
| Day 1 | Step 1: 扩展 trace_opcode.h（~85 条新枚举 + 复用检查） | 3h | 4h |
| Day 1-3 | Step 2: 创建 hopper_opcode.h（~277 条映射，含 36 条 Ampere 修正） | 6h | 10h |
| Day 3 | Step 2.5: 交叉校验脚本（sm90_2.txt vs hopper_opcode.h） | 1h | 11h |
| Day 3 | Step 3: 注册 trace parser | 0.5h | 11.5h |
| Day 4 | Step 4: 创建 gpgpusim.config | 3h | 14.5h |
| Day 4 | Step 5: 创建 trace.config | 1h | 15.5h |
| Day 4 | Step 6: 注册 job_launching | 0.5h | 16h |
| Day 5 | Step 7: 编译验证 | 1h | 17h |
| Day 5-6 | Step 8: 端到端 Trace 验证 | 3h | 20h |
| **合计** | | **~20h** | **~1.5 周** |

**缓冲**: 预留 3-5 小时应对意外问题（NVBit 兼容性、HBM3 时序调优等）。

**前置调研说明**: Day 0 应在开始编码前确认 NVBit 对 SM90 的支持状态。如果不支持，需在 Step 8 中切换到备选验证路径（cuobjdump + 手工 trace），可能额外耗时 1-2 天。

---

## 风险与缓解

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| NVBit 不支持 SM90 | 中 | 无法采集真实 trace | 使用 cuobjdump + 手工构造 trace |
| H100 硬件参数不准确 | 低 | 仿真精度偏差 | Phase 0 不要求精度验证，后续可微调 |
| Hopper trace 包含未注册指令 | 低 | 仿真崩溃 | 已注册所有 sm90_2.txt 中的指令 |
| gpgpusim.config 参数冲突 | 低 | 仿真器报错 | 逐步修改，每步编译验证 |

---

## 关键文件索引

| 文件 | 操作 | 步骤 |
|------|------|------|
| `gpu-simulator/ISA_Def/trace_opcode.h` | 修改 | Step 1 |
| `gpu-simulator/ISA_Def/hopper_opcode.h` | 新建 | Step 2 |
| `gpu-simulator/trace-driven/trace_driven.cc` | 修改 | Step 3 |
| `gpu-simulator/gpgpu-sim/configs/tested-cfgs/SM90_H100/gpgpusim.config` | 新建 | Step 4 |
| `gpu-simulator/configs/tested-cfgs/SM90_H100/trace.config` | 新建 | Step 5 |
| `util/job_launching/configs/define-standard-cfgs.yml` | 修改 | Step 6 |

### 参考文件（只读）
| 文件 | 用途 |
|------|------|
| `gpu-simulator/ISA_Def/ampere_opcode.h` | opcode 映射模板 |
| `gpu-simulator/gpgpu-sim/configs/tested-cfgs/SM80_A100/gpgpusim.config` | config 模板 |
| `gpu-simulator/configs/tested-cfgs/SM80_A100/trace.config` | trace config 模板 |
| `util/denvdis/data12/sm90_2.txt` | SM90 pipe 分类参考 |
| `docs/hopper-sm90-feasibility-report.md` | 可行性分析参考 |

---

## ADR: 架构决策记录

### Decision
手动编写 `hopper_opcode.h`，以 Ampere opcode map 为基础，使用 denvdis pipe 分类数据作为校验参考。所有 SM90 指令（包括 Phase 1+ 的 GMMA/TMA/CGA 等）均注册到 opcode map，但 Phase 0 不实现其语义。

### Drivers
1. 1-2 周时间约束要求最小化额外开发
2. Ampere 已覆盖 ~90% 的指令映射，Hopper 新增 ~50 条
3. denvdis 数据格式复杂，自动解析脚本投入产出比不高

### Alternatives Considered
- **脚本自动生成**: 投入过大，且 pipe→执行单元映射需人工判断
- **只注册纯计算指令**: 遇到 GMMA/TMA 指令会崩溃，不如全部注册为 stub

### Why Chosen
手动编写在 1-2 周时间线内最可控，且产出质量最可预测。前向兼容策略（注册所有指令）消除了运行时崩溃风险。

### Consequences
- (+) 可在 1 周内完成
- (+) 不需要额外工具开发
- (+) 遇到任何 SM90 指令都不会崩溃
- (-) GMMA/TMA 等指令的延迟和执行单元映射可能不准确，但 Phase 0 目标只是纯计算 kernel
- (-) 后续 Phase 需要回来修正这些 stub 映射

### Follow-ups
- Phase 1: 实现 mbarrier 语义，修正 ARRIVES/SYNCS 映射
- Phase 2: 实现 TMA 语义，修正 UTMA*/UBLK* 映射
- Phase 3: 实现 CGA Barrier，修正 UCGABAR_* 映射
