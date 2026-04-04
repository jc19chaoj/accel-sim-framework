[根目录](../../CLAUDE.md) > [util](../) > **tracer_nvbit**

# tracer_nvbit -- NVBit SASS 追踪工具

## 变更记录 (Changelog)

| 日期 | 变更内容 |
|------|----------|
| 2026-04-04 | 增量更新：新增 modal/ 云端追踪工具文档；补全环境变量列表；扩展 spinlock_tool 详情（两阶段检测、KernelInstructionHistogram）；补充 CUDA Graph 支持、地址压缩格式、CUkernel 句柄回退机制、NVBit 1.7.4 CALL.REL.NOINC 规避、post-traces-processing 目录输入与 WarpInstLUT 优化；更新文件清单 |
| 2026-04-03 | 初始生成 |

---

## 模块职责

使用 NVIDIA 的 NVBit (v1.7.6) 二进制插桩框架，在真实 GPU 上运行 CUDA 应用并捕获 SASS（原生机器指令）级别的执行追踪。生成的追踪文件供 Accel-Sim 仿真引擎消费。支持从 Volta (SM7.0) 到 Hopper (SM9.0) 的 GPU 架构。

---

## 入口与启动

### 方式一：批量追踪基准测试套件
```bash
./run_hw_trace.py -B rodinia_2.0-ft -D 0
```
自动处理追踪生成、后处理和清理。支持可选的自旋锁检测与快进：
```bash
./run_hw_trace.py -B rodinia_2.0-ft -D 0 \
    --spinlock_handling fast_forward \
    --spinlock_fast_forward_iterations 1
```

### 方式二：追踪单个应用
```bash
export CUDA_VISIBLE_DEVICES=0
LD_PRELOAD=./tracer_tool/tracer_tool.so <your_cuda_app>
# 手动后处理（支持传入目录或单个 kernelslist 文件）
./tracer_tool/traces-processing/post-traces-processing ./traces/kernelslist
# 或传入 traces 目录，自动查找所有 kernelslist* 文件
./tracer_tool/traces-processing/post-traces-processing ./traces/
```

### 方式三：Modal 云端追踪（H100/SM90）
在没有本地 H100 GPU 时，通过 Modal 云平台远程生成追踪：
```bash
cd modal/
pip install modal && modal token new
modal run trace_h100.py
# 追踪文件下载到 ./traces_output/
```
详见 `modal/README.md`。

### 安装步骤
```bash
./install_nvbit.sh      # 下载 NVBit 1.7.6 到 ./nvbit_release/
make -C ./              # 编译 tracer_tool、post-traces-processing、spinlock_tool
```

---

## 对外接口

### 环境变量控制

#### 核心追踪控制
| 变量 | 默认值 | 说明 |
|------|--------|------|
| `DYNAMIC_KERNEL_RANGE` | (空=追踪全部) | 选择性追踪指定 kernel。格式：`"2"` 单个、`"5-8"` 范围、`"10-"` 开放式、`"5-8@kernel_a.*,kernel_b.*"` 带正则名称过滤，空格分隔多组 |
| `ACTIVE_FROM_START` | 1 | 设为 0 禁用默认追踪，配合 `cudaProfilerStart/Stop` 使用。设为 0 时 `DYNAMIC_KERNEL_RANGE` 无效 |
| `TERMINATE_UPON_LIMIT` | 0 | 设为 1 时，当 kernel ID 超过 `DYNAMIC_KERNEL_RANGE` 上限后自动终止进程 |

#### 追踪输出控制
| 变量 | 默认值 | 说明 |
|------|--------|------|
| `TOOL_COMPRESS` | 1 | 启用内存地址压缩（base+stride 或 base+delta 格式） |
| `TRACE_FILE_COMPRESS` | 1 | 使用 xz 压缩 `.trace` 文件（生成 `.trace.xz`） |
| `TRACE_LINEINFO` | 0 | 设为 1 启用源码行号追踪（需 `-lineinfo` 编译目标） |
| `TOOL_TRACE_CORE` | 0 | 设为 1 在追踪中写入 SM ID 和 warp ID |
| `TRACES_FOLDER` | (当前目录) | 自定义追踪输出根目录 |
| `USER_DEFINED_FOLDERS` | 0 | 设为 1 使用 `TRACES_FOLDER` 指定的路径 |

#### 调试与限制
| 变量 | 默认值 | 说明 |
|------|--------|------|
| `TOOL_VERBOSE` | 0 | 详细日志级别（0=关闭，1=基本信息，2=指令解码） |
| `INSTR_BEGIN` | 0 | 插桩指令起始偏移 |
| `INSTR_END` | UINT32_MAX | 插桩指令结束偏移 |
| `EXCLUDE_PRED_OFF` | 1 | 排除谓词关闭的指令计数 |

#### 自旋锁快进
| 变量 | 默认值 | 说明 |
|------|--------|------|
| `ENABLE_SPINLOCK_FAST_FORWARD` | 0 | 启用自旋锁快进（需先运行 spinlock_tool 生成检测结果） |
| `SPINLOCK_ITER_TO_KEEP` | 1 | 保留的自旋锁迭代次数 |

### 输出文件格式
- `kernel-<id>-ctx_<addr>.trace[.xz]` -- 原始追踪文件（每 kernel 一个，可选 xz 压缩）
- `kernelslist_ctx_<addr>` -- 按 CUDA context 分开的 kernel 列表和 memcpy 操作
- `stats_ctx_<addr>` -- 统计摘要（CSV 格式：kernel 名称、grid/block 维度、指令计数）
- 后处理产物：
  - `.traceg[.xz]` -- 按线程块分组的追踪文件
  - `kernelslist.g` -- 供 Accel-Sim 仿真器消费的统一入口

### 追踪文件头格式
每个 `.trace` 文件包含如下元信息头：
```
-kernel name = <mangled_name>
-kernel id = <id>
-grid dim = (X,Y,Z)
-block dim = (X,Y,Z)
-shmem = <bytes>
-nregs = <count>
-binary version = <sm_version>
-cuda stream id = <id>
-shmem base_addr = 0x...
-local mem base_addr = 0x...
-nvbit version = <version>
-accelsim tracer version = 5
-enable lineinfo = 0|1
```

### 追踪指令格式
```
[tb_id_x] [tb_id_y] [tb_id_z] [warp_id] [line_num?] PC mask dest_num [reg_dests] opcode src_num [reg_srcs] mem_width [address_format] [addresses] immediate
```

#### 地址压缩格式
| 格式 ID | 名称 | 内容 |
|---------|------|------|
| 0 | `list_all` | 列出所有活跃线程的完整地址 |
| 1 | `base_stride` | `base_addr stride`（连续等间距访问） |
| 2 | `base_delta` | `base_addr delta1 delta2 ...`（非等间距但可增量编码） |

---

## 关键依赖与配置

- **NVBit 1.7.6**：通过 `install_nvbit.sh` 自动下载安装到 `./nvbit_release/`，支持到 SM90 (Hopper)
- **CUDA Toolkit**：nvcc >= 10.1（编译工具），`-arch=all` 需要 nvcc >= 11.7
- **真实 GPU**：追踪必须在真实 NVIDIA GPU 上运行
- **C++ 标准**：C++17（tracer_tool 和 post-traces-processing）
- **当前追踪工具版本**：TRACER_VERSION "5"
- **构建产物**：`tracer_tool/tracer_tool.so`（LD_PRELOAD 注入库）、`tracer_tool/traces-processing/post-traces-processing`（后处理可执行文件）

### 已知限制与规避
- **NVBit 1.7.4 CALL.REL.NOINC bug**：`tracer_tool.cu` 中跳过 `CALL.REL.NOINC` 指令的插桩，避免非法内存访问（参见 NVBit issue #142）
- **CUkernel 句柄兼容**：`get_attr_with_kernel_fallback()` 函数处理 `cuLaunchKernel` 使用 CUkernel 句柄时 `cuFuncGetAttribute` 返回 `CUDA_ERROR_INVALID_HANDLE` 的情况，自动回退到 `cuKernelGetAttribute`

---

## 数据模型

### inst_trace_t（`tracer_tool/common.h`）
追踪工具核心数据结构，通过 NVBit channel 机制实现 GPU 到 CPU 的高效数据传输：

| 字段 | 类型 | 说明 |
|------|------|------|
| `cta_id_x/y/z` | int | 线程块 ID |
| `warpid_tb` | int | 线程块内 warp ID |
| `warpid_sm` | int | SM 内 warp ID |
| `sm_id` | int | SM ID |
| `opcode_id` | int | 操作码映射 ID |
| `addrs[32]` | uint64_t[] | 32 个线程的内存地址 |
| `vpc` | uint32_t | 虚拟 PC |
| `is_mem` | bool | 是否为内存指令 |
| `GPRDst` | int32_t | 目标寄存器 |
| `GPRSrcs[5]` | int32_t[] | 最多 5 个源寄存器 (MAX_SRC=5) |
| `active_mask` | uint32_t | 活跃线程掩码 |
| `predicate_mask` | uint32_t | 谓词掩码 |
| `imm` | uint64_t | 立即数（用于 DEPBAR 等指令） |
| `line_num` | uint32_t | 源码行号（需 lineinfo） |
| `instr_idx` | uint32_t | 指令索引（用于自旋锁检测） |

### instr_count_t（`others/spinlock_tool/common.h`）
自旋锁工具的通道数据包：
- `instr_idx` (uint32_t) -- 指令索引
- `count` (uint32_t) -- 执行次数

### KernelInstructionHistogram（`others/spinlock_tool/common.h`）
内核指令直方图类，用于自旋锁检测：
- 支持序列化/反序列化到文件
- `merge()` -- 合并多个 context 的直方图（支持哈希取模防溢出）
- `findSpinlock()` -- 比对两次运行的直方图，找出执行次数不同的指令（即非确定性/自旋锁指令）

---

## CUDA Graph 支持

tracer_tool 完整支持 CUDA Graph 的追踪，覆盖三种场景：
1. **常规 kernel 启动**：`cuLaunchKernel`、`cuLaunchKernelEx` 等，在启动前插桩、启动后同步并读取计数
2. **Stream Capture**：检测 `cudaStreamIsCapturing`，捕获期间仅插桩不同步，在 `cuGraphLaunch` 退出后同步
3. **手动 Graph 构建**：拦截 `cuGraphAddKernelNode` 事件进行插桩

---

## 子工具

### tracer_tool (`tracer_tool/`)
- 核心追踪插桩工具
  - `tracer_tool.cu` -- 主逻辑：NVBit 回调、kernel 启动/退出处理、CUDA 事件拦截、接收线程
  - `inject_funcs.cu` -- GPU 端指令注入函数 `instrument_inst()`，收集每条指令的操作码、寄存器、内存地址等
  - `common.h` -- `inst_trace_t` 结构定义
- 后处理程序 (`traces-processing/`)
  - `post-traces-processing.cpp` -- 将原始追踪按线程块/warp 重组为 `.traceg` 格式
  - 支持传入目录路径（自动扫描所有 `kernelslist*`）或单个文件路径
  - 使用 `WarpInstLUT` 哈希表对重复指令字符串去重，降低内存占用
  - 通过 fork + pipe 实现流式 xz 压缩/解压
  - 处理 LDGSTS 双内存引用指令（保留全局内存引用，过滤共享内存引用）

### spinlock_tool (`others/spinlock_tool/`)
自旋锁检测工具，通过两阶段运行识别非确定性指令段：

**工作流程**：
1. **Phase 0**：运行目标程序，记录每条指令的执行次数直方图
2. **Phase 1**：再次运行，记录新的直方图，并在终止时比对两次结果
3. **输出**：`spinlock_detection/spinlock_instructions.txt`（每行：`kernel_id, kernel_name: instr_idx1 instr_idx2 ...`）

**使用方式**：
```bash
# 检测
SPINLOCK_PHASE=0 CUDA_INJECTION64_PATH=./others/spinlock_tool/spinlock_tool.so ./your_app
SPINLOCK_PHASE=1 CUDA_INJECTION64_PATH=./others/spinlock_tool/spinlock_tool.so ./your_app
# 快进追踪
ENABLE_SPINLOCK_FAST_FORWARD=1 LD_PRELOAD=./tracer_tool/tracer_tool.so ./your_app
```

**spinlock_tool 环境变量**：
| 变量 | 说明 |
|------|------|
| `SPINLOCK_PHASE` | 0 或 1，两阶段检测 |
| `TRACES_FOLDER` | 检测结果输出根目录 |
| `SPINLOCK_KEEP_INTERMEDIATE_FILES` | 保留中间文件（ctx 目录和合并目录） |
| `DYNAMIC_KERNEL_RANGE` | 同 tracer_tool，支持范围+正则过滤 |

### modal/ (云端追踪)
通过 Modal 云平台在远程 H100 GPU 上生成追踪，适用于无本地 H100 硬件的场景：
- `trace_h100.py` -- Modal 脚本，在 CUDA 12.8 + Ubuntu 22.04 容器中执行完整追踪流水线
- 内置 VectorAdd 示例应用，可替换为自定义 CUDA 源码
- 输出下载到 `traces_output/`
- Modal 可能分配 H200（同为 SM90），追踪完全兼容

### 其他工具 (`others/`)
- `bbv_tool/` -- 基本块向量工具（含 `bbv_count` 和 `bbv_count_tb` 两个变体，分别按 warp 和线程块粒度统计）
- `occupancy_calc_tool/` -- GPU 占用率计算工具
- `silicon_checkpoint_tool/` -- 硅片检查点工具（构建已在顶层 Makefile 中注释掉）

---

## 测试与质量

- 代码格式化：`./tracer_tool/format-code.sh`（clang-format，覆盖 `*.cu`、`*.h`、`traces-processing/*.cpp`）
- CI 中通过 `Tracer-Tool` 作业进行端到端测试：生成追踪 -> 仿真 -> 验证
- 辅助追踪生成脚本：
  - `generate-volta-traces.sh` -- DGX-1 上并行生成多套 Volta 追踪（ubench、rodinia、parboil、polybench、cutlass、deepbench）
  - `generate-turing-traces.sh` -- Turing GPU 上串行生成追踪并 rsync 到远程存储

---

## 常见问题 (FAQ)

**Q: 追踪时遇到 `CUDA_ERROR_INVALID_HANDLE` 错误？**
A: 这通常发生在使用 CUkernel 句柄调用 `cuLaunchKernel` 时。tracer_tool 已内置 `get_attr_with_kernel_fallback()` 回退机制自动处理。

**Q: 追踪文件很大怎么办？**
A: 默认已启用 xz 压缩（`TRACE_FILE_COMPRESS=1`）和地址压缩（`TOOL_COMPRESS=1`）。可通过 `DYNAMIC_KERNEL_RANGE` 限制追踪的 kernel 范围，或设置 `TERMINATE_UPON_LIMIT=1` 在达到限制后自动终止。

**Q: 如何追踪包含自旋锁的应用（如 GEMM with mutex）？**
A: 先运行 spinlock_tool 两次（Phase 0 和 Phase 1）检测自旋锁指令，然后在追踪时设置 `ENABLE_SPINLOCK_FAST_FORWARD=1` 跳过重复的自旋锁迭代。`run_hw_trace.py` 的 `--spinlock_handling fast_forward` 选项可自动化整个流程。

**Q: 如何在没有本地 GPU 的情况下生成 H100 追踪？**
A: 使用 `modal/trace_h100.py` 通过 Modal 云平台远程生成。详见 `modal/README.md`。

---

## 相关文件清单

| 文件/目录 | 说明 |
|-----------|------|
| `run_hw_trace.py` | 批量追踪启动脚本（支持 spinlock 选项） |
| `install_nvbit.sh` | NVBit 1.7.6 安装脚本 |
| `Makefile` | 顶层构建文件（tracer_tool + post-traces-processing + spinlock_tool） |
| `tracer_tool/tracer_tool.cu` | 核心追踪插桩实现（含 CUDA Graph 支持） |
| `tracer_tool/inject_funcs.cu` | GPU 端指令注入函数 |
| `tracer_tool/common.h` | `inst_trace_t` 结构定义 (MAX_SRC=5) |
| `tracer_tool/traces-processing/post-traces-processing.cpp` | 追踪后处理程序（含 WarpInstLUT 优化） |
| `tracer_tool/format-code.sh` | clang-format 格式化脚本 |
| `others/spinlock_tool/spinlock_tool.cu` | 自旋锁检测工具（两阶段直方图比对） |
| `others/spinlock_tool/common.h` | `instr_count_t` 和 `KernelInstructionHistogram` 定义 |
| `others/spinlock_tool/inject_funcs.cu` | spinlock_tool GPU 端注入函数 |
| `others/spinlock_tool/README.md` | 自旋锁工具使用说明 |
| `others/bbv_tool/` | 基本块向量工具（bbv_count、bbv_count_tb） |
| `others/occupancy_calc_tool/` | 占用率计算工具 |
| `others/silicon_checkpoint_tool/` | 硅片检查点工具 |
| `modal/trace_h100.py` | Modal 云端 H100 追踪脚本 |
| `modal/README.md` | Modal 云端追踪使用说明 |
| `generate-volta-traces.sh` | Volta DGX-1 批量追踪生成脚本 |
| `generate-turing-traces.sh` | Turing 批量追踪生成脚本 |
| `README.md` | 详细使用文档 |
| `.gitignore` | 忽略 nvbit_release/、*.o、*.so、post-traces-processing |
