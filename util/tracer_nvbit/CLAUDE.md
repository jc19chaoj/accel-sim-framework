[根目录](../../CLAUDE.md) > [util](../) > **tracer_nvbit**

# tracer_nvbit -- NVBit SASS 追踪工具

## 变更记录 (Changelog)

| 日期 | 变更内容 |
|------|----------|
| 2026-04-03 | 初始生成 |

---

## 模块职责

使用 NVIDIA 的 NVBit 二进制插桩框架，在真实 GPU 上运行 CUDA 应用并捕获 SASS（原生机器指令）级别的执行追踪。生成的追踪文件供 Accel-Sim 仿真引擎消费。

---

## 入口与启动

### 方式一：批量追踪基准测试套件
```bash
./run_hw_trace.py -B rodinia_2.0-ft -D 0
```
自动处理追踪生成、后处理和清理。

### 方式二：追踪单个应用
```bash
export CUDA_VISIBLE_DEVICES=0
LD_PRELOAD=./tracer_tool/tracer_tool.so <your_cuda_app>
# 手动后处理
./tracer_tool/traces-processing/post-traces-processing ./traces/kernelslist
```

### 安装步骤
```bash
./install_nvbit.sh      # 安装 NVBit
make -C ./              # 编译追踪工具
```

---

## 对外接口

### 环境变量控制
| 变量 | 说明 |
|------|------|
| `DYNAMIC_KERNEL_RANGE` | 选择性追踪指定 kernel（支持范围、名称过滤） |
| `ACTIVE_FROM_START` | 设为 0 可禁用默认追踪，配合 `cudaProfilerStart/Stop` 使用 |
| `TRACE_LINEINFO` | 设为 1 启用源码行号追踪 |

### 输出文件格式
- `kernel-*.trace` -- 原始追踪文件（每 kernel 一个）
- `kernelslist` -- kernel 列表和 CUDA memcpy 操作
- `stats.csv` -- 统计摘要
- 后处理产物：`.traceg`（按线程块分组）、`kernelslist.g`（供 Accel-Sim 使用）

### 追踪指令格式
```
[line_num] PC mask dest_num [reg_dests] opcode src_num [reg_srcs] mem_width [addresses]
```

---

## 关键依赖与配置

- **NVBit**：通过 `install_nvbit.sh` 自动下载安装到 `./nvbit_release/`
- **CUDA Toolkit**：需要与目标 GPU 兼容的版本
- **真实 GPU**：追踪必须在真实 NVIDIA GPU 上运行
- 当前追踪工具版本：**TRACER_VERSION "5"**

---

## 数据模型

追踪工具核心数据结构定义在 `tracer_tool/common.h`：
- 通过 NVBit channel 机制实现 GPU 到 CPU 的高效数据传输
- 每条指令记录：PC、活跃线程掩码、目标/源寄存器、操作码、内存地址

---

## 子工具

### tracer_tool (`tracer_tool/`)
- 核心追踪插桩工具（`tracer_tool.cu`, `inject_funcs.cu`）
- 后处理程序（`traces-processing/post-traces-processing.cpp`）

### spinlock_tool (`others/spinlock_tool/`)
- 自旋锁检测工具，用于处理包含自旋锁的应用
- 支持 fast_forward 模式跳过自旋锁迭代

### 其他工具 (`others/`)
- `bbv_tool/` -- 基本块向量工具
- `occupancy_calc_tool/` -- 占用率计算工具
- `silicon_checkpoint_tool/` -- 硅片检查点工具

---

## 测试与质量

- 代码格式化：`./tracer_tool/format-code.sh`
- CI 中通过 `Tracer-Tool` 作业进行端到端测试：生成追踪 -> 仿真 -> 验证
- 辅助追踪生成脚本：`generate-volta-traces.sh`、`generate-turing-traces.sh`

---

## 相关文件清单

| 文件 | 说明 |
|------|------|
| `run_hw_trace.py` | 批量追踪启动脚本 |
| `install_nvbit.sh` | NVBit 安装脚本 |
| `Makefile` | 顶层构建文件 |
| `tracer_tool/tracer_tool.cu` | 核心追踪插桩实现 |
| `tracer_tool/inject_funcs.cu` | 指令注入函数 |
| `tracer_tool/common.h` | 共享数据结构 |
| `tracer_tool/traces-processing/post-traces-processing.cpp` | 追踪后处理程序 |
| `others/spinlock_tool/` | 自旋锁检测工具 |
| `README.md` | 详细使用文档 |
