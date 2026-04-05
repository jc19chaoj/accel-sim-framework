# Deep Interview Spec: Perfetto Waveform Trace for Accel-Sim

## Metadata
- Interview ID: di-perfetto-waveform-001
- Rounds: 8
- Final Ambiguity Score: 17.5%
- Type: brownfield
- Generated: 2026-04-05
- Threshold: 20%
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.85 | 0.35 | 0.298 |
| Constraint Clarity | 0.85 | 0.25 | 0.213 |
| Success Criteria | 0.75 | 0.25 | 0.188 |
| Context Clarity | 0.85 | 0.15 | 0.128 |
| **Total Clarity** | | | **0.825** |
| **Ambiguity** | | | **0.175** |

## Goal
在 Accel-Sim（GPGPU-Sim 4.0）仿真器中添加 Perfetto 兼容的 trace 输出功能，采用**混合模式**：关键事件（指令流水线阶段、cache 访问、DRAM 请求）输出为 **Slice/Span events**，模块状态指标（队列深度、利用率、活跃 warp 数）输出为 **Counter Tracks**。两者在 Perfetto UI 中对齐同一时间轴显示，支持逐周期的微架构行为可视化分析。

## 追踪层级（全部层级，分阶段实现）

### 层级 1：流水线阶段级（MVP）
- **Slice Events**: 指令在 fetch → decode → issue → operand-collect → execute → writeback 各阶段的停留时间
- 粒度：per-warp instruction, per-pipeline-stage

### 层级 2：功能单元级
- **Slice Events**: SP/DP/INT/SFU/LDST/TensorCore 单元的 issue → complete 事件
- **Counter Tracks**: 各单元的 busy/idle 状态、利用率

### 层级 3：内存层次级
- **Slice Events**: L1/L2 cache 的 request → response（标注 hit/miss）、DRAM read/write 请求的发出与返回
- **Counter Tracks**: L1/L2 pending requests 数量、DRAM 队列深度、miss rate

### 层级 4：互联网络级
- **Slice Events**: core ↔ memory partition 之间 packet 的发出与到达
- **Counter Tracks**: 互联网络拥塞度、带宽利用率

## Constraints
- **输出格式（MVP）**: Chrome Trace Event JSON（零依赖，fprintf 输出），后续可升级为 Perfetto Protobuf
- **配置方式**: 融入现有 `gpgpusim.config` + `option_parser` 体系，使用 `-gpgpu_perfetto_*` 参数前缀
- **可配置过滤**:
  - SM 范围过滤（`-gpgpu_perfetto_sm_range 0:1`）
  - 周期窗口过滤（`-gpgpu_perfetto_cycle_start / _cycle_end`）
  - 模块类型过滤（`-gpgpu_perfetto_modules pipeline,memory,interconnect`）
- **性能开销**: 关闭追踪时零开销（编译期或运行期 guard）；开启追踪时可接受的减速
- **文件大小**: 通过过滤参数控制数据量，MVP 仅追踪单 SM 保持文件可管理

## Non-Goals
- 不实现 RTL 级信号波形（VCD/GTKWave 格式）——这是行为级仿真器，不是 RTL
- 不在 MVP 中实现 Protobuf 输出——先 JSON 验证方案可行性
- 不在 MVP 中实现全部四个层级——MVP 仅包含层级 1（流水线阶段级）
- 不修改仿真器核心逻辑——仅在关键位置添加追踪探针（instrumentation）
- 不实现实时可视化——仅生成离线 trace 文件

## Acceptance Criteria

### MVP（层级 1：单 SM 流水线 Slice）
- [ ] 在 `gpgpusim.config` 中配置 `-gpgpu_perfetto_enable 1` 后，仿真结束时生成合法的 Chrome Trace Event JSON 文件
- [ ] 在 Perfetto UI (ui.perfetto.dev) 中能正确打开并显示该 JSON 文件
- [ ] 能看到 SM0 的流水线阶段 Slice events：fetch/decode/issue/execute/writeback
- [ ] 每个 Slice 显示正确的开始周期和持续周期数
- [ ] Slice 的元数据（metadata）中包含 warp ID、指令 PC、opcode 等关键信息
- [ ] `-gpgpu_perfetto_sm_range` 参数能正确过滤 SM 范围
- [ ] `-gpgpu_perfetto_cycle_start/end` 参数能正确过滤周期窗口
- [ ] 关闭追踪时对仿真性能无可测量影响
- [ ] 使用 `rodinia_2.0-ft` 基准测试验证端到端功能

### Phase 2（层级 2-4：完整追踪）
- [ ] 功能单元 Slice + Counter tracks 正常显示
- [ ] 内存层次 Slice（cache hit/miss 标注）+ Counter tracks 正常显示
- [ ] 互联网络 Slice + Counter tracks 正常显示
- [ ] 多 SM 追踪时 Perfetto UI 仍可流畅浏览（性能可接受）

### Phase 3（Protobuf 升级）
- [ ] 支持 Perfetto native protobuf 输出格式
- [ ] Counter Track 在 Perfetto 中显示为折线图
- [ ] 大规模追踪（多 SM、长周期）文件大小合理

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| "每个模块的输入输出"意味着显式信号线 | GPGPU-Sim 是行为级模拟器，模块间通过共享对象传递状态，没有显式信号线 | 采用混合模式：状态快照做 Counter + 关键事件做 Slice |
| 需要追踪所有内容才有价值 | 全量追踪数据量巨大（GB级），可能不实用 | 可配置过滤 + 分阶段交付，MVP 仅追踪单 SM 流水线 |
| 必须使用 Perfetto 原生格式 | Chrome Trace JSON 更简单，对 MVP 数据量足够 | JSON 先行，后续升级 protobuf |
| 配置需要新的机制 | 现有 gpgpusim.config + option_parser 已经成熟 | 融入现有配置体系，-gpgpu_perfetto_* 参数 |

## Technical Context

### 关键代码插入点
- **主仿真循环**: `gpgpu_sim::cycle()` in `gpu-sim.cc`（CORE/ICNT/DRAM 时钟域）
- **Shader core 流水线**: `shader_core_ctx::cycle()` in `shader.cc`（fetch→decode→issue→execute→writeback 各阶段函数）
- **流水线寄存器**: `register_set` 类和 `pipeline_stage_name_t` 枚举（`shader.h`）
- **执行单元**: `sp_unit`, `dp_unit`, `int_unit`, `sfu_unit`, `ldst_unit` 类
- **内存层次**: `l1_cache`, `l2_cache`, `memory_partition_unit`, `dram_t`
- **现有追踪基础设施**: `DTRACE/DPRINTF` 宏（`trace.h`）、`trace_streams` 枚举
- **配置注册**: `option_parser` in gpgpu-sim 核心

### Chrome Trace Event JSON 格式
```json
{
  "traceEvents": [
    {"name": "fetch", "cat": "pipeline", "ph": "X", "ts": 100, "dur": 3, "pid": 0, "tid": 0, "args": {"warp_id": 0, "pc": "0x7f00", "opcode": "LDG"}},
    {"name": "SP_busy", "cat": "counter", "ph": "C", "ts": 100, "pid": 0, "tid": 0, "args": {"value": 1}}
  ]
}
```
- `ph: "X"` = Complete event (Slice)
- `ph: "C"` = Counter event
- `pid` = SM ID, `tid` = Warp ID or Unit ID
- `ts` = cycle number (作为时间戳)

## Ontology (Key Entities)

| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| ShaderCore (SM) | core domain | sid, pipeline_stages, exec_units, l1_cache | contains PipelineStage, ExecutionUnit, L1Cache |
| PipelineStage | core domain | stage_name, warp_inst, cycle_start, cycle_end | belongs to ShaderCore, processes WarpInstruction |
| ExecutionUnit | core domain | type (SP/DP/INT/SFU/LDST/TC), busy, latency | belongs to ShaderCore, executes WarpInstruction |
| WarpInstruction | core domain | pc, opcode, warp_id, active_mask | flows through PipelineStages, executed by ExecutionUnit |
| L1Cache | supporting | pending_reqs, hit_count, miss_count | belongs to ShaderCore, sends MemFetch to MemPartition |
| L2Cache | supporting | pending_reqs, hit_count, miss_count | belongs to MemPartition |
| MemPartition | core domain | partition_id, l2_cache, dram_queue | contains L2Cache, connects to DRAM |
| DRAM | supporting | queue_depth, bandwidth_util | belongs to MemPartition |
| Interconnect | supporting | congestion, packets_in_flight | connects ShaderCore to MemPartition |
| SliceEvent | core domain | name, category, start_cycle, duration, metadata | traced from PipelineStage/ExecutionUnit/Cache |
| CounterTrack | core domain | name, category, cycle, value | sampled from module state snapshots |
| TraceFile | supporting | format (JSON/protobuf), path, size | contains SliceEvents and CounterTracks |
| FilterConfig | supporting | sm_range, cycle_window, module_types | controls what goes into TraceFile |
| JSONTraceWriter | supporting | output_stream, event_buffer | writes SliceEvents/CounterTracks to TraceFile |

## Ontology Convergence

| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|-------------|-----|---------|--------|----------------|
| 1 | 6 | 6 | - | - | N/A |
| 2 | 8 | 2 | 0 | 6 | 75% |
| 3 | 10 | 2 | 0 | 8 | 80% |
| 4 | 12 | 2 | 0 | 10 | 83% |
| 5 | 13 | 2 | 1 | 10 | 85% |
| 6 | 13 | 1 | 0 | 12 | 92% |
| 7 | 14 | 1 | 0 | 13 | 93% |
| 8 | 14 | 0 | 0 | 14 | 100% |

## Interview Transcript
<details>
<summary>Full Q&A (8 rounds)</summary>

### Round 1
**Q:** 你说的「每个模块的输入输出」具体指什么粒度？（流水线阶段级/功能单元级/内存层次级/互联网络级）
**A:** 全部层级
**Ambiguity:** 76.5% (Goal: 0.40, Constraints: 0.10, Criteria: 0.10, Context: 0.30)

### Round 2
**Q:** Perfetto 支持 Slice/Span 事件和 Counter Track 两种模式，你说的「波形」更接近哪种？
**A:** 两者都要（A和B都要）
**Ambiguity:** 68.8% (Goal: 0.60, Constraints: 0.10, Criteria: 0.10, Context: 0.35)

### Round 3
**Q:** 追踪数据量约束——全部 SM 还是部分？全程还是窗口？性能开销容忍度？
**A:** 可配置过滤
**Ambiguity:** 58.8% (Goal: 0.60, Constraints: 0.50, Criteria: 0.10, Context: 0.35)

### Round 4
**Q:** 你最想用这个工具回答的第一个问题是什么？（指令流水线分析/内存瓶颈/资源利用率）
**A:** 全部场景
**Ambiguity:** 47.0% (Goal: 0.65, Constraints: 0.50, Criteria: 0.50, Context: 0.35)

### Round 5 (Contrarian Mode)
**Q:** GPGPU-Sim 不是 RTL，模块间没有显式信号线。哪种插桩策略更合适？（状态快照/事件驱动/混合）
**A:** 混合模式（推荐）
**Ambiguity:** 36.8% (Goal: 0.80, Constraints: 0.55, Criteria: 0.50, Context: 0.60)

### Round 6 (Simplifier Mode)
**Q:** 分阶段交付的 MVP 应该包含什么？
**A:** 单 SM 流水线 Slice
**Ambiguity:** 28.8% (Goal: 0.85, Constraints: 0.60, Criteria: 0.70, Context: 0.60)

### Round 7
**Q:** 输出格式选择——Chrome Trace JSON vs Perfetto Protobuf？
**A:** JSON 先行（推荐）
**Ambiguity:** 22.3% (Goal: 0.85, Constraints: 0.75, Criteria: 0.75, Context: 0.70)

### Round 8
**Q:** 配置如何接入——融入现有 gpgpusim.config 还是独立配置？
**A:** 融入现有配置，使用 -gpgpu_perfetto_* 参数
**Ambiguity:** 17.5% (Goal: 0.85, Constraints: 0.85, Criteria: 0.75, Context: 0.85)

</details>
