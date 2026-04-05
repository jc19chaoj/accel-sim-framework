# Implementation Plan: Perfetto Waveform Trace for Accel-Sim

## Source Spec
- `.omc/specs/deep-interview-perfetto-waveform-trace.md`
- Ambiguity: 17.5% (PASSED)

---

## RALPLAN-DR Summary

### Principles
1. **Zero-overhead when disabled** — tracing code must have no measurable performance impact when `-gpgpu_perfetto_enable 0`; enforced via cached `bool m_perfetto_active` in `shader_core_ctx` (mirrors `g_power_simulation_enabled` pattern)
2. **Minimal invasiveness** — instrumentation inserts at well-defined boundaries (pipeline stage transitions), no modifications to core simulation logic
3. **Chrome Trace Event JSON first** — simplest format, zero dependencies, validates the concept before investing in protobuf
4. **Configurable granularity** — SM range, cycle window, and module type filters prevent data explosion
5. **Phased delivery** — MVP (pipeline slices) → Phase 2 (all layers) → Phase 3 (protobuf), each independently valuable

### Decision Drivers
1. **Implementation complexity vs. value** — MVP must deliver visible results quickly with minimal code changes
2. **Data volume management** — full-system trace at cycle granularity generates massive output; filtering is essential
3. **Codebase integration friction** — must fit naturally into existing config/build/option_parser patterns

### Viable Options

#### Option A: Centralized Trace Writer (Chosen)
A single `PerfettoTraceWriter` class owns the JSON output file. Pipeline stages and modules call `trace_slice()` and `trace_counter()` methods on a writer owned by `gpgpu_sim` (accessible via `m_gpu->perfetto_writer()` from `shader_core_ctx`, which already holds `m_gpu` pointer at `shader.h:2060`). The writer checks filters (SM range, cycle window) before writing.

**Pros:**
- Single point of control for output format, filtering, and file management
- Easy to swap JSON backend for protobuf later (one class to change)
- Consistent output format guaranteed
- Simple to test in isolation
- Fits `gpgpu_sim` ownership model (`m_cluster`, `m_memory_partition_unit`, etc. at `gpu-sim.h:690-727`)

**Cons:**
- All instrumentation points depend on writer API (acceptable: API is small and stable)
- In Phase 2+, filter checks run on every hot path (mitigated: cached bool guard + inline cycle comparison)

#### Option B: Distributed Per-Module Tracers
Each module (shader_core_ctx, memory_partition, etc.) owns its own tracer instance, writing to separate files merged post-hoc.

**Pros:**
- No global dependency; each module is self-contained
- Parallel file writes (potential performance benefit)

**Cons:**
- Merging multiple JSON files is complex and error-prone
- Inconsistent filtering logic across modules
- More code to maintain (N tracer instances vs. 1)
- Perfetto expects a single trace file

**Invalidation rationale:** Perfetto consumes a single trace file; distributing output across modules adds merge complexity with no architectural benefit for a behavioral simulator that runs single-threaded.

#### Option C: Extend Existing DTRACE/DPRINTF Infrastructure
Reuse the existing `DTRACE`/`DPRINTF` macros (`trace.h`, `trace_streams.tup`) by adding a new trace stream and post-processing the text output into Perfetto format.

**Pros:**
- Reuses existing tracing infrastructure
- No new writer class needed

**Cons:**
- DPRINTF outputs unstructured text — would require a complex parser to convert to Chrome Trace JSON
- No structured metadata (warp_id, PC, opcode) in DPRINTF output without significant format changes
- Post-processing adds a separate tooling step and latency
- Cannot emit incremental Perfetto events during simulation

**Invalidation rationale:** DPRINTF is designed for human-readable debug logs, not machine-consumable structured events. Converting to Perfetto format requires either changing all DPRINTF format strings (high invasiveness) or building a fragile text parser (unreliable). A purpose-built writer is cleaner and more maintainable.

---

## Requirements Summary

### MVP Scope (Pipeline Slice Events for Single SM)
- Instrument `shader_core_ctx::cycle()` pipeline stages: **fetch → decode → issue → operand-collect → execute → writeback** (6 stages)
- Track per-instruction flow through pipeline stages as Slice events (keyed by `inst_uid`, not per-warp)
- Output Chrome Trace Event JSON format
- Configurable via `-gpgpu_perfetto_*` options in `gpgpusim.config`
- Filterable by SM ID range and cycle window

---

## Acceptance Criteria
- [ ] AC1: `-gpgpu_perfetto_enable 1` causes a valid Chrome Trace JSON file to be written at simulation end
- [ ] AC2: The JSON file opens correctly in Perfetto UI (ui.perfetto.dev) and displays timeline
- [ ] AC3: SM0 pipeline stages (fetch/decode/issue/operand-collect/execute/writeback) appear as Slice events with correct start cycle and duration
- [ ] AC4: Each Slice event contains metadata: warp_id, instruction uid, PC (hex), opcode name
- [ ] AC5: `-gpgpu_perfetto_sm_range 0:1` filters output to SM0 only
- [ ] AC6: `-gpgpu_perfetto_cycle_start 100 -gpgpu_perfetto_cycle_end 500` filters output to cycles 100–500
- [ ] AC7: With `-gpgpu_perfetto_enable 0`, simulation runtime overhead is **< 1%** compared to baseline (measured over 3 runs, same machine, same config)
- [ ] AC8: End-to-end test with `rodinia_2.0-ft` benchmark generates valid trace file (validated with `python3 -c "import json; json.load(open('trace.json'))"`)

---

## Implementation Steps

### Step 1: Create `perfetto_trace_config` struct and `PerfettoTraceWriter` class

**New file:** `gpu-simulator/gpgpu-sim/src/gpgpu-sim/perfetto_trace_writer.h`
**New file:** `gpu-simulator/gpgpu-sim/src/gpgpu-sim/perfetto_trace_writer.cc`

#### 1a. Config struct (in header, mirroring `power_config` at `gpu-sim.h:138-211`)

```cpp
struct perfetto_trace_config {
  bool  gpgpu_perfetto_enable;        // -gpgpu_perfetto_enable (default: 0)
  char* gpgpu_perfetto_output;        // -gpgpu_perfetto_output (default: "perfetto_trace.json")
  char* gpgpu_perfetto_sm_range;      // -gpgpu_perfetto_sm_range (default: "0:-1" = all)
  char* gpgpu_perfetto_cycle_start;   // -gpgpu_perfetto_cycle_start (default: "0") — OPT_CSTR, parsed to ull
  char* gpgpu_perfetto_cycle_end;     // -gpgpu_perfetto_cycle_end (default: "0") — OPT_CSTR, 0 = unlimited
  char* gpgpu_perfetto_modules;       // -gpgpu_perfetto_modules (default: "all")

  void reg_options(class OptionParser *opp);
  void init();  // parse sm_range string, convert cycle strings to ull
};
```

**Note on option types:** `gpgpu_perfetto_cycle_start/end` use `OPT_CSTR` (not `OPT_INT32`) because GPGPU-Sim's `option_parser` does not support `unsigned long long`. The `init()` method parses them via `strtoull()`.

#### 1b. Writer class

```cpp
class PerfettoTraceWriter {
public:
  PerfettoTraceWriter(const char* output_path);
  ~PerfettoTraceWriter();  // writes closing ']}' via atexit-safe pattern, fclose

  // Filter configuration
  void set_sm_range(unsigned start, unsigned end);
  void set_cycle_window(unsigned long long start, unsigned long long end);

  // Inline filter checks (for hot-path performance)
  inline bool should_trace_sm(unsigned sid) const {
    return sid >= m_sm_start && sid < m_sm_end;
  }
  inline bool should_trace_cycle(unsigned long long cycle) const {
    return cycle >= m_cycle_start && (m_cycle_end == 0 || cycle < m_cycle_end);
  }

  // Slice events (Chrome Trace "X" = complete event)
  void trace_slice(const char* name, const char* category,
                   unsigned long long start_cycle, unsigned long long duration,
                   unsigned pid, unsigned tid,
                   unsigned inst_uid, unsigned warp_id,
                   address_type pc, const char* opcode_name);

  // Counter events (Chrome Trace "C") — Phase 2
  void trace_counter(const char* name, const char* category,
                     unsigned long long cycle, unsigned pid, int value);

  bool is_enabled() const { return m_enabled; }
  void finalize();  // write closing ']}', flush, called from destructor AND registered via atexit()

private:
  FILE* m_file;
  bool m_enabled;
  bool m_first_event;    // for JSON comma handling
  bool m_finalized;      // prevent double-finalize
  unsigned m_sm_start, m_sm_end;
  unsigned long long m_cycle_start, m_cycle_end;
};
```

**Constructor must call:**
```cpp
setvbuf(m_file, NULL, _IOFBF, 64 * 1024);  // 64KB write buffer to prevent I/O bottleneck
```

**Crash safety:** Register `finalize()` via `atexit()` callback (using a file-scope pointer to the singleton writer) so that `]}` is written even on abnormal exit. The `m_finalized` flag prevents double-close.

**Output format (Chrome Trace Event JSON):**
```json
{"traceEvents":[
{"ph":"X","name":"fetch","cat":"pipeline","ts":100,"dur":3,"pid":0,"tid":0,"args":{"warp_id":0,"uid":42,"pc":"0x7f00","opcode":"LDG"}},
{"ph":"X","name":"decode","cat":"pipeline","ts":103,"dur":1,"pid":0,"tid":0,"args":{"warp_id":0,"uid":42,"pc":"0x7f00","opcode":"LDG"}}
]}
```

**Key design decisions:**
- `fprintf` for output (zero dependency)
- `pid` = SM ID (shows as "Process" in Perfetto, allows collapsing per-SM)
- `tid` = Warp ID (shows as "Thread" in Perfetto, stacks within SM)
- **`ts` = `gpu_sim_cycle + gpu_tot_sim_cycle`** (absolute cycle count, monotonically increasing across kernel launches; `gpu_sim_cycle` resets per kernel at `gpu-sim.cc:1192,1248`, so using it alone would produce non-monotonic timestamps)
- Write events incrementally (not buffered in memory) to handle large traces
- JSON comma handling: first event has no leading comma, subsequent events prepend `,\n`

### Step 2: Register configuration options

**Modify:** `gpu-simulator/gpgpu-sim/src/gpgpu-sim/gpu-sim.h` (~line 409)
- Add `perfetto_trace_config` to the inheritance chain of `gpgpu_sim_config` (mirrors `power_config` inheritance pattern)

**Modify:** `gpu-simulator/gpgpu-sim/src/gpgpu-sim/gpu-sim.cc` (~line 670, in `gpgpu_sim_config::reg_options()`)
- Call `perfetto_trace_config::reg_options(opp)` to register the 6 options
- In `gpgpu_sim_config::init()`: call `perfetto_trace_config::init()` to parse config strings

### Step 3: Initialize writer in gpgpu_sim

**Modify:** `gpu-simulator/gpgpu-sim/src/gpgpu-sim/gpu-sim.h` (~line 690 area, `gpgpu_sim` class)
- Add `PerfettoTraceWriter* m_perfetto_writer;` member
- Add `PerfettoTraceWriter* perfetto_writer() { return m_perfetto_writer; }`

**Modify:** `gpu-simulator/gpgpu-sim/src/gpgpu-sim/gpu-sim.cc`
- In `gpgpu_sim` constructor (or `init()`): create writer if `gpgpu_perfetto_enable` is true, configure filters from parsed config
- In destructor / `print_stats()`: call `m_perfetto_writer->finalize()` then delete

**Compatibility note:** `trace_gpgpu_sim` (`trace_driven.h:165`) inherits from `gpgpu_sim`. Since we add the writer to the base class and don't override any virtual methods, trace-driven mode works without changes.

### Step 4: Add per-instruction stage tracker to shader_core_ctx

**Modify:** `gpu-simulator/gpgpu-sim/src/gpgpu-sim/shader.h` (in `shader_core_ctx` class, ~line 2060)

```cpp
// Per-instruction pipeline stage tracking for Perfetto trace
struct perfetto_stage_entry {
  unsigned long long start_cycle;  // absolute cycle when instruction entered current stage
  unsigned warp_id;
  address_type pc;
  const char* opcode_name;
  const char* stage_name;
};

// In shader_core_ctx:
bool m_perfetto_active;  // cached from gpgpu_sim::perfetto_writer()->is_enabled() && should_trace_sm(m_sid)
std::unordered_map<unsigned, perfetto_stage_entry> m_perfetto_inst_tracker;  // keyed by inst_uid
```

**Why per-instruction, not per-warp:** Multiple instructions from the same warp can be in-flight simultaneously in different pipeline stages. For example, warp 0's instruction A may be in execute while instruction B is in decode. A per-warp tracker (`stage_start_cycle[warp_id]`) would overwrite A's state when B enters the pipeline. Using `inst_uid` (from `warp_inst_t::get_uid()` at `abstract_hardware_model.h:1236`, globally incrementing) as the map key correctly handles concurrent in-flight instructions.

**`m_perfetto_active` initialization:** Set once in `shader_core_ctx` constructor (or `init()`):
```cpp
m_perfetto_active = m_gpu->perfetto_writer() != nullptr
                    && m_gpu->perfetto_writer()->is_enabled()
                    && m_gpu->perfetto_writer()->should_trace_sm(m_sid);
```

### Step 5: Instrument pipeline stages in shader_core_ctx

**Modify:** `gpu-simulator/gpgpu-sim/src/gpgpu-sim/shader.cc`

**IMPORTANT: Pipeline execution order.** `shader_core_ctx::cycle()` at line 3673 calls stages in **reverse** order (drain before fill):
```
writeback()       // line 3677 — EX_WB → commit
execute()         // line 3678 — OC_EX_* → EX_WB
read_operands()   // line 3679 — ID_OC_* → OC_EX_* (operand collection)
issue()           // line 3680 — scheduler picks warp → ID_OC_*
decode()          // line 3682 — ibuffer fill
fetch()           // line 3683 — instruction cache access
```

Instrumentation detects stage transitions by observing when an instruction appears in a new stage function. The helper pattern at each stage:

```cpp
// Helper: call at each stage entry point when an instruction is found
inline void perfetto_record_stage_transition(
    unsigned inst_uid, unsigned warp_id, address_type pc,
    const char* opcode_name, const char* new_stage,
    unsigned long long current_cycle)
{
  if (!m_perfetto_active) return;
  auto writer = m_gpu->perfetto_writer();
  if (!writer->should_trace_cycle(current_cycle)) return;

  auto it = m_perfetto_inst_tracker.find(inst_uid);
  if (it != m_perfetto_inst_tracker.end()) {
    // Emit slice for previous stage
    auto& entry = it->second;
    unsigned long long dur = current_cycle - entry.start_cycle;
    if (dur > 0) {
      writer->trace_slice(entry.stage_name, "pipeline",
                          entry.start_cycle, dur,
                          m_sid, entry.warp_id,
                          inst_uid, entry.warp_id, entry.pc, entry.opcode_name);
    }
  }
  // Update tracker for new stage
  m_perfetto_inst_tracker[inst_uid] = {current_cycle, warp_id, pc, opcode_name, new_stage};
}

// Helper: call when instruction completes (writeback exit)
inline void perfetto_complete_instruction(unsigned inst_uid, unsigned long long current_cycle)
{
  if (!m_perfetto_active) return;
  auto it = m_perfetto_inst_tracker.find(inst_uid);
  if (it != m_perfetto_inst_tracker.end()) {
    auto& entry = it->second;
    unsigned long long dur = current_cycle - entry.start_cycle;
    if (dur > 0) {
      auto writer = m_gpu->perfetto_writer();
      writer->trace_slice(entry.stage_name, "pipeline",
                          entry.start_cycle, dur,
                          m_sid, entry.warp_id,
                          inst_uid, entry.warp_id, entry.pc, entry.opcode_name);
    }
    m_perfetto_inst_tracker.erase(it);  // cleanup completed instruction
  }
}
```

**Instrumentation points (6 stages):**

**5a. Fetch** (`shader_core_ctx::fetch()`, shader.cc ~line 925)
- When instruction enters the I-buffer: `perfetto_record_stage_transition(uid, warp_id, pc, opcode, "fetch", cycle)`

**5b. Decode** (`shader_core_ctx::decode()`, shader.cc ~line 890)
- When instruction moves to decode pipeline register: `perfetto_record_stage_transition(uid, warp_id, pc, opcode, "decode", cycle)`

**5c. Issue** (`shader_core_ctx::issue()` → scheduler, shader.cc ~line 1129)
- When scheduler selects and issues instruction: `perfetto_record_stage_transition(uid, warp_id, pc, opcode, "issue", cycle)`

**5d. Operand-Collect** (`shader_core_ctx::read_operands()`, shader.cc ~line 1713, delegates to `m_operand_collector.step()` at line 1715)
- When instruction enters operand collection: `perfetto_record_stage_transition(uid, warp_id, pc, opcode, "operand_collect", cycle)`
- **This stage was missing from the initial plan.** It is where register bank conflicts stall instructions — a prime debugging target.

**5e. Execute** (in functional unit `::cycle()` methods — `sp_unit::cycle()`, etc., shader.cc ~line 1806)
- When instruction begins execution: `perfetto_record_stage_transition(uid, warp_id, pc, opcode, "execute", cycle)`

**5f. Writeback** (`shader_core_ctx::writeback()`, shader.cc ~line 1931)
- When instruction completes: `perfetto_complete_instruction(uid, cycle + 1)` (the +1 gives writeback a 1-cycle minimum duration)

**`current_cycle` source:** Always use `m_gpu->gpu_sim_cycle + m_gpu->gpu_tot_sim_cycle` for absolute monotonic timestamps (verified at `gpu-sim.cc:2096` for `gpu_sim_cycle` increment, and `gpu-sim.cc:1192` for `gpu_tot_sim_cycle` accumulation across kernels).

**Compatibility with trace-driven mode:** `trace_shader_core_ctx` (`trace_driven.h:189`) overrides `get_next_inst()` but NOT the pipeline stage functions (`fetch`, `decode`, `issue`, `read_operands`, `execute`, `writeback`). All instrumentation is in base class methods and will be inherited correctly.

### Step 6: Build system integration

**No Makefile change needed:** `gpu-simulator/gpgpu-sim/src/gpgpu-sim/Makefile:71` uses `SRCS = $(shell ls *.cc)` which auto-discovers all `.cc` files. Creating `perfetto_trace_writer.cc` in the directory is sufficient.

**Modify:** `gpu-simulator/gpgpu-sim/src/gpgpu-sim/CMakeLists.txt` (lines 2-22)
- Add `perfetto_trace_writer.cc` to the `list(APPEND gpgpusim_SRC ...)` block

**No new external dependencies required.**

### Step 7: End-to-end verification

1. **Build:** `make -j -C ./gpu-simulator/` (or CMake equivalent)
2. **Configure test run** in gpgpusim.config:
   ```
   -gpgpu_perfetto_enable 1
   -gpgpu_perfetto_output test_trace.json
   -gpgpu_perfetto_sm_range 0:1
   -gpgpu_perfetto_cycle_end 1000
   ```
3. **Run** with `rodinia_2.0-ft` benchmark
4. **Validate JSON structure:** `python3 -c "import json; data=json.load(open('test_trace.json')); print(f'{len(data[\"traceEvents\"])} events'); assert len(data['traceEvents']) > 0"`
5. **Validate in Perfetto UI:** Open `test_trace.json` at ui.perfetto.dev, confirm:
   - SM0 appears as a process with warp lanes
   - Pipeline stage slices (fetch/decode/issue/operand_collect/execute/writeback) are visible
   - Slices have correct metadata (warp_id, uid, pc, opcode)
6. **Performance test:** Run benchmark 3 times with `-gpgpu_perfetto_enable 0`, 3 times with enable=1. Compare average wall-clock time. Overhead must be < 1%.
7. **Filter test:** Run with `-gpgpu_perfetto_sm_range 0:1` — verify only SM0 events in output. Run with `-gpgpu_perfetto_cycle_start 100 -gpgpu_perfetto_cycle_end 500` — verify all `ts` values are in range.

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Pipeline stage boundaries not clearly identifiable in code | High — wrong instrumentation points produce incorrect trace | Use stage function entry points as canonical boundaries; validate by comparing slice durations against known instruction latencies from config |
| JSON output performance for large traces | Medium — slow file I/O could bottleneck | `setvbuf(m_file, NULL, _IOFBF, 64*1024)` in constructor; SM range and cycle window filters limit data; Phase 3 upgrades to protobuf |
| Warp instructions may skip stages (barrier, NOP) | Medium — missing or incorrect slices | Check `warp_inst_t::m_empty` and instruction type before tracking; skip non-pipeline instructions |
| `warp_inst_t` pointer reuse across cycles | High — tracking by pointer is unreliable | Track by `inst_uid` (`warp_inst_t::get_uid()` at `abstract_hardware_model.h:1236`), globally unique per instruction |
| Multiple instructions per warp in-flight simultaneously | High — per-warp tracker overwrites data | Per-instruction tracker keyed by `inst_uid`, not `warp_id`. Verified: pipeline has 6 stages, a warp can have instructions in multiple stages |
| Simulation crash leaves malformed JSON | Low — missing `]}` makes file unreadable | `atexit()` callback calls `finalize()` to write closing bracket; `m_finalized` flag prevents double-write |
| `gpu_sim_cycle` resets across kernel launches | Medium — non-monotonic timestamps | Always use `gpu_sim_cycle + gpu_tot_sim_cycle` as absolute timestamp (verified: `gpu_sim_cycle` resets at `gpu-sim.cc:1192,1248`) |
| `unordered_map` allocation overhead on hot path | Low — map grows gradually | Pre-reserve capacity for `MAX_WARPS_PER_SM * pipeline_depth`; entries are erased on writeback, keeping map size bounded by in-flight instructions |

---

## Verification Steps

1. **JSON unit test:** `python3 -c "import json; json.load(open('trace.json'))"` — validates structure. Check `traceEvents` array is non-empty.
2. **Integration test:** Run `rodinia_2.0-ft` with tracing enabled for SM0, cycles 0-1000. Verify file exists, is valid JSON, contains events with `pid=0` only.
3. **Perfetto UI validation:** Open trace in ui.perfetto.dev. Confirm 6 pipeline stage names appear. Confirm slices show duration > 0. Confirm metadata (warp_id, uid, pc, opcode) is visible on click.
4. **Performance test:** 3 runs disabled vs. 3 runs enabled, same machine/config. Wall-clock delta < 1%.
5. **Filter tests:**
   - SM filter: trace with `sm_range 0:1`, verify all events have `pid == 0`
   - Cycle filter: trace with `cycle_start 100 cycle_end 500`, verify all events have `100 <= ts < 500`
6. **Monotonic timestamp test:** Parse JSON, verify `ts` values across all events are non-decreasing within each `(pid, tid)` pair.

---

## File Change Summary

| File | Action | Description |
|------|--------|-------------|
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/perfetto_trace_writer.h` | **CREATE** | `perfetto_trace_config` struct + `PerfettoTraceWriter` class header |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/perfetto_trace_writer.cc` | **CREATE** | Implementation (~300 lines) |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/gpu-sim.h` | MODIFY | Inherit `perfetto_trace_config` in `gpgpu_sim_config` (~line 409); add writer pointer to `gpgpu_sim` (~line 690) |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/gpu-sim.cc` | MODIFY | Call `perfetto_trace_config::reg_options()` (~line 670); init/destroy writer (~30 lines) |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/shader.h` | MODIFY | Add `m_perfetto_active`, `perfetto_stage_entry`, `m_perfetto_inst_tracker`, helper methods to `shader_core_ctx` (~20 lines) |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/shader.cc` | MODIFY | Instrument 6 pipeline stages (fetch/decode/issue/operand_collect/execute/writeback) + helpers (~60 lines) |
| `gpu-simulator/gpgpu-sim/src/gpgpu-sim/CMakeLists.txt` | MODIFY | Add `perfetto_trace_writer.cc` to `gpgpusim_SRC` list (1 line) |

**Total estimated new code:** ~300 lines (writer class + config) + ~80 lines (instrumentation helpers) = ~380 lines
**Total estimated modifications:** ~55 lines across existing files

---

## ADR (Architecture Decision Record)

### Decision
Implement a centralized `PerfettoTraceWriter` class that outputs Chrome Trace Event JSON, instrumented at 6 pipeline stage boundaries in `shader_core_ctx`, with per-instruction tracking keyed by `inst_uid`, configured via a `perfetto_trace_config` struct registered through existing `option_parser` infrastructure.

### Drivers
1. Need cycle-accurate pipeline visualization for debugging/analysis
2. Must integrate with existing GPGPU-Sim codebase patterns (`power_config` struct pattern, `option_parser`, `gpgpu_sim` ownership)
3. MVP must be deliverable with minimal dependencies and code changes

### Alternatives Considered
- **Distributed per-module tracers** — rejected: Perfetto expects single file; merge complexity unjustified for single-threaded simulator
- **Perfetto SDK/protobuf first** — rejected: adds build dependency, higher complexity for MVP validation; reserved for Phase 3
- **Extend existing DTRACE/DPRINTF** — rejected: unstructured text output; would require fragile parser or invasive format string changes; not designed for machine-consumable structured events

### Why Chosen
Option A (centralized writer) provides the simplest path to a working MVP with zero external dependencies, clean API for future extension (protobuf backend swap), and natural fit with GPGPU-Sim's existing architecture where `gpgpu_sim` owns all subsystem pointers. The `perfetto_trace_config` struct mirrors the established `power_config` pattern for consistent codebase integration.

### Consequences
- New source files added to GPGPU-Sim build (auto-discovered by Make, manually added to CMake)
- Small runtime overhead when tracing is enabled (fprintf per event, mitigated by 64KB write buffer)
- `std::unordered_map` in hot path for instruction tracking (bounded by in-flight instruction count, typically < 100 entries)
- Future protobuf upgrade requires changing only `PerfettoTraceWriter` internals
- Instrumentation in `shader.cc` creates coupling between trace writer API and pipeline code

### Follow-ups
- Phase 2: Add execution unit (SP/DP/INT/SFU/LDST) Slice + Counter, memory hierarchy (L1/L2/DRAM) Slice + Counter, interconnect Slice + Counter
- Phase 3: Implement protobuf backend for Counter Track support and smaller file sizes
- Consider adding `--perfetto-trace` convenience flag to `run_simulations.py`
- Consider file size warning/auto-truncation for large traces

---

## Revision Log

### Revision 1 (Architect + Critic feedback)
1. **[CRITICAL] Fixed per-warp → per-instruction tracker** — now keyed by `inst_uid` via `unordered_map`, not `warp_id` array
2. **[MAJOR] Added `read_operands` / operand-collect stage** — 6th pipeline stage between issue and execute
3. **[MAJOR] Fixed build system references** — removed Makefile from MODIFY list (auto-discovers), corrected CMakeLists.txt path to `gpu-simulator/gpgpu-sim/src/gpgpu-sim/CMakeLists.txt`
4. **[MAJOR] Specified `ts` semantics** — `gpu_sim_cycle + gpu_tot_sim_cycle` for absolute monotonic timestamps
5. **[MAJOR] Added reverse pipeline order note** — documented that `cycle()` calls stages in reverse (writeback first, fetch last)
6. **[MINOR] Created `perfetto_trace_config` struct** — mirrors `power_config` pattern at `gpu-sim.h:138`
7. **[MINOR] Added `setvbuf` in constructor** — 64KB buffer to prevent I/O bottleneck
8. **[MINOR] Added `m_perfetto_active` cached bool** — zero-overhead guard in `shader_core_ctx`
9. **[MINOR] Added `atexit()` crash safety** — `finalize()` writes `]}` even on abnormal exit
10. **[MINOR] Harmonized AC7 threshold** — specified "< 1%" in acceptance criteria
11. **[MINOR] Added Option C (DTRACE extension)** — formally evaluated and invalidated
12. **[MINOR] Used OPT_CSTR for cycle values** — parsed via `strtoull()` since option_parser lacks `unsigned long long` support
