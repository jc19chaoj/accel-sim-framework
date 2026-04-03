[根目录](../CLAUDE.md) > **gpu-simulator**

# gpu-simulator -- 核心仿真引擎

## 变更记录 (Changelog)

| 日期 | 变更内容 |
|------|----------|
| 2026-04-03 | 初始生成 |

---

## 模块职责

gpu-simulator 是 Accel-Sim 的核心模块，负责：
- 将 SASS/PTX 指令追踪解析为内部中间表示
- 驱动 GPGPU-Sim 4.0 性能模型进行周期精确仿真
- 管理多 kernel 并发执行与 CUDA stream 调度
- 提供 Python 绑定以支持脚本化仿真

---

## 入口与启动

- **C++ 入口**：`main.cc` -- 创建 `accel_sim_framework` 实例并调用 `simulation_loop()`
- **Python 入口**：`main.py` -- 通过 pybind11 绑定调用仿真引擎
- **环境初始化**：`setup_environment.sh` -- 克隆 gpgpu-sim、设置环境变量、准备构建环境

### 启动流程

1. `source setup_environment.sh` -- 拉取 gpgpu-sim，设置 `GPGPUSIM_ROOT`/`ACCELSIM_ROOT`
2. `make -j` 或 CMake 构建
3. `accel-sim.out -trace <kernelslist.g> -config <gpgpusim.config> -config <trace.config>`

---

## 对外接口

### 命令行接口
```
accel-sim.out -trace <追踪文件列表> -config <GPU配置> -config <追踪配置>
```

### Python 接口（pybind11）
```python
import accel_sim
sim = accel_sim.accel_sim_framework(config_file, trace_file)
sim.simulation_loop()
```

暴露的方法：`init()`, `simulation_loop()`, `parse_commandlist()`, `cleanup()`, `simulate()`

---

## 关键依赖与配置

### 子模块
- **gpgpu-sim**（`./gpgpu-sim/`）-- GPGPU-Sim 4.0 性能模型（独立 git 仓库，通过 `setup_environment.sh` 自动拉取）
- **pybind11**（`./extern/pybind11/`）-- Python 绑定库

### 环境变量
| 变量 | 说明 |
|------|------|
| `CUDA_INSTALL_PATH` | CUDA Toolkit 安装路径 |
| `GPGPUSIM_ROOT` | gpgpu-sim 根目录（自动设置） |
| `ACCELSIM_ROOT` | accel-sim 根目录（自动设置） |
| `ACCELSIM_CONFIG` | 构建配置：`release`（默认）或 `debug` |

### GPU 配置文件
- **GPGPU-Sim 配置**：`gpgpu-sim/configs/tested-cfgs/<GPU>/gpgpusim.config` -- 微架构参数
- **追踪配置**：`configs/tested-cfgs/<GPU>/trace.config` -- 指令延迟与流水线参数
- 支持的 GPU 型号：SM3_KEPLER_TITAN, SM6_TITANX, SM7_QV100, SM7_GV100, SM7_TITANV, SM75_RTX2060, SM80_A100, SM86_RTX3070 等

---

## 数据模型

### 核心类层次

- `accel_sim_framework` -- 顶层仿真框架类（`accel-sim.h`/`accel-sim.cc`）
  - 管理仿真主循环、命令列表解析、kernel 生命周期
- `trace_parser` -- 追踪文件解析器（`trace-parser/`）
  - 解析 kernelslist 命令文件、kernel 追踪头信息、线程块追踪数据
- `trace_driven` -- 追踪驱动仿真前端（`trace-driven/`）
  - `trace_gpgpu_sim` -- 继承 `gpgpu_sim`，创建追踪驱动的 SIMT 集群
  - `trace_shader_core_ctx` -- 继承 `shader_core_ctx`，从追踪获取下一条指令
  - `trace_kernel_info_t` -- 继承 `kernel_info_t`，管理 kernel 追踪信息
  - `trace_warp_inst_t` -- 继承 `warp_inst_t`，从追踪结构解析指令

### ISA 定义（`ISA_Def/`）
- `trace_opcode.h` -- 指令操作码枚举与分类映射
- `<arch>_opcode.h` -- 各架构特定的 SASS 指令映射（kepler/pascal/volta/turing/ampere）
- `accelwattch_component_mapping.h` -- 指令到功耗组件的映射

---

## 测试与质量

- 代码格式化：`./format-code.sh`（clang-format）
- 集成测试通过上层 CI 流水线运行（见根 CLAUDE.md）
- gpgpu-sim 自带测试：`./gpgpu-sim/short-tests.sh`

---

## 常见问题 (FAQ)

**Q: 构建时报错 "ACCELSIM_SETUP_ENVIRONMENT_WAS_RUN" 未定义？**
A: 必须先 `source setup_environment.sh`，不能直接 `make`。

**Q: 如何添加新 GPU 架构支持？**
A: 需要在 `ISA_Def/` 新增 opcode 头文件，在 `configs/tested-cfgs/` 添加配置，并在 `util/job_launching/configs/define-standard-cfgs.yml` 注册。

**Q: gpgpu-sim 仓库在哪里？**
A: `setup_environment.sh` 会自动克隆到 `./gpgpu-sim/` 目录。默认仓库：`https://github.com/accel-sim/gpgpu-sim_distribution.git`，分支：`dev`。

---

## 相关文件清单

| 文件 | 说明 |
|------|------|
| `main.cc` | C++ 入口 |
| `accel-sim.h` / `accel-sim.cc` | 核心仿真框架类 |
| `main.py` | Python 入口 |
| `python_wrapper/python_wrapper.cc` | pybind11 绑定 |
| `setup_environment.sh` | 环境初始化脚本 |
| `Makefile` / `CMakeLists.txt` | 构建文件 |
| `trace-parser/trace_parser.h` | 追踪解析器 |
| `trace-driven/trace_driven.h` | 追踪驱动前端 |
| `ISA_Def/trace_opcode.h` | 指令操作码定义 |
| `configs/tested-cfgs/` | 各 GPU 的追踪配置 |
| `gpgpu-sim4.md` | GPGPU-Sim 4.0 变更说明 |
| `README.md` | 模块原始文档 |
