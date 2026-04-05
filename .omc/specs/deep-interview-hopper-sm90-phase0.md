# Deep Interview Spec: Accel-Sim Hopper SM90 Phase 0 Support

## Metadata
- Interview ID: hopper-sm90-support
- Rounds: 6
- Final Ambiguity Score: 15.3%
- Type: brownfield
- Generated: 2026-04-04
- Threshold: 20%
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.9 | 0.35 | 0.315 |
| Constraint Clarity | 0.8 | 0.25 | 0.2 |
| Success Criteria | 0.85 | 0.25 | 0.213 |
| Context Clarity | 0.8 | 0.15 | 0.12 |
| **Total Clarity** | | | **0.848** |
| **Ambiguity** | | | **15.3%** |

## Goal
在 Accel-Sim 框架中添加 NVIDIA Hopper SM90 架构的基础支持（Phase 0），使仿真器能够加载并模拟纯计算类型的 Hopper SASS trace（标准算术/整数/load-store 指令），产出 IPC 等统计数据。不包含 TMA、mbarrier、Cluster、DSMEM 等高级特性的语义实现。

## Constraints
- 时间线：1-2 周内完成
- 团队：单人开发
- 硬件：本地无 H100/H200，但有云服务器访问权限可采集 trace
- 交付物：仅需详细的 step-by-step 实施计划文档（不需要代码实现）
- 工作负载：仅需支持纯计算 kernel（vectorAdd、rodinia 基准等），不需要支持 GEMM/Tensor Core 工作负载

## Non-Goals
- Phase 1-4 的实现（mbarrier、TMA、Thread Block Clusters、DSMEM）
- GMMA/WARPGROUP 等新 Tensor Core 指令的语义建模
- CGA Barrier 跨 SM 同步
- 与硬件性能计数器的精度对比验证
- H200 独立配置（H200 与 H100 共享 SM90 架构，仅 HBM 不同）

## Acceptance Criteria
- [ ] `hopper_opcode.h` 创建完成，包含 SM90 所有 SASS 指令到执行单元的映射
- [ ] `trace_opcode.h` 扩展，包含 Hopper 特有 opcode 枚举
- [ ] `trace_driven.cc` 注册 SM90 binary version (90)，能识别 Hopper trace
- [ ] `SM90_H100/gpgpusim.config` 创建，包含 H100 硬件参数
- [ ] `SM90_H100/trace.config` 创建，包含指令延迟和 specialized unit 配置
- [ ] 仿真器能编译通过
- [ ] 能加载并运行纯计算类 SM90 trace，产出 IPC 统计

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| Phase 0 足以运行真实 trace | 真实应用几乎都用 GMMA/TMA | 用户确认只需纯计算 kernel，不需要 Tensor Core 工作负载 |
| 需要手动编写 opcode 映射 | denvdis 有完整 SM90 pipe 分类 | 可利用 denvdis/data12/sm90_2.txt 的 pipe→执行单元映射辅助生成 |
| 需要 H100 硬件 | 本地无硬件 | 用户有云服务器 H100 访问权限，可远程采集 trace |

## Technical Context
- 现有架构：Kepler(SM3.x) → Ampere(SM8.6)，通过 ISA_Def/*.opcode.h + tested-cfgs/ 配置体系扩展
- 架构注册入口：`trace_driven.cc` 的 binary_version if-else 链
- denvdis/data12/sm90_2.txt 包含完整的 SM90 执行管线分类（int_pipe, mio_pipe, fe_pipe, fmalighter_pipe, fp16_pipe, cbu_pipe, fma64lite_pipe, fma64heavy_pipe, udp_pipe）
- Ampere opcode.h (230+ 映射) 是最近的参考模板
- SM90 新指令（GMMA, WARPGROUP, UCGABAR_*, UTMA*, STAS, REDAS 等）需注册到 opcode map 但 Phase 0 不需要实现其语义

## Ontology (Key Entities)

| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| Hopper ISA | Core domain | opcode map, binary_version=90, instruction set | Maps to execution units via hopper_opcode.h |
| H100 Config | Core domain | SM count=132, HBM3, clock domains, cache sizes | Consumed by gpgpusim.config |
| DPX Instructions | Supporting | VIMNMX, VIMNMX3, VIADDMNMX, VIADD | Subset of Hopper ISA, maps to int_pipe |
| Trace Parser | System | binary_version check, OpcodeMap selection | Registers Hopper ISA into simulator |
| Pure Compute Kernel | Core domain | vectorAdd, rodinia benchmarks, no Tensor Core | Target workload for Phase 0 validation |
| Plan Document | Deliverable | step-by-step, schedule, file index | Output of this interview |
| denvdis SM90 Data | External system | sm90_1.txt, sm90_2.txt, pipe classifications | Source for auto-generating opcode mappings |

## Ontology Convergence

| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|-------------|-----|---------|--------|----------------|
| 1 | 4 | 4 | - | - | N/A |
| 2 | 5 | 1 | 0 | 4 | 80% |
| 3 | 5 | 0 | 0 | 5 | 100% |
| 4 | 6 | 1 | 0 | 5 | 83% |
| 5 | 6 | 0 | 0 | 6 | 100% |
| 6 | 7 | 1 | 0 | 6 | 86% |

## Interview Transcript
<details>
<summary>Full Q&A (6 rounds)</summary>

### Round 1
**Q:** 你的可行性报告列出了 5 个阶段（Phase 0-4），总计 14-17 周。这次你打算实施到哪个阶段？
**A:** 仅 Phase 0（基础支持）
**Ambiguity:** 65% (Goal: 0.6, Constraints: 0.1, Criteria: 0.1, Context: 0.6)

### Round 2
**Q:** Phase 0 的成功标准是什么？
**A:** 能跑基础 trace
**Ambiguity:** 49% (Goal: 0.7, Constraints: 0.1, Criteria: 0.6, Context: 0.6)

### Round 3
**Q:** Phase 0 的时间约束是什么？
**A:** 1-2 周内完成
**Ambiguity:** 39% (Goal: 0.7, Constraints: 0.5, Criteria: 0.6, Context: 0.6)

### Round 4 (Contrarian Mode)
**Q:** denvdis 数据显示 SM90 新指令已深度嵌入到标准执行管线中。你确定"仅 Phase 0"就够了吗？
**A:** 纯计算 kernel
**Ambiguity:** 28% (Goal: 0.85, Constraints: 0.5, Criteria: 0.75, Context: 0.75)

### Round 5
**Q:** 你有 H100/H200 硬件访问权限吗？
**A:** 本地没有，但是有云服务器的资源
**Ambiguity:** 21.5% (Goal: 0.85, Constraints: 0.7, Criteria: 0.8, Context: 0.75)

### Round 6
**Q:** 你希望我交付什么？
**A:** 只要计划文档
**Ambiguity:** 15.3% (Goal: 0.9, Constraints: 0.8, Criteria: 0.85, Context: 0.8)
</details>
