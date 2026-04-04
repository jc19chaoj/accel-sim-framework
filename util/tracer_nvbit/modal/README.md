# Modal H100 远程追踪生成

在没有本地 H100 GPU 的情况下，通过 [Modal](https://modal.com) 云平台远程生成 Accel-Sim SASS 追踪。

## 背景

Accel-Sim 的追踪工具（NVBit tracer）必须在**真实 GPU** 上运行才能捕获 SASS 指令流。对于 H100 (SM90) 等新架构，本地可能没有对应硬件。Modal 提供按需的 H100 GPU 实例，可以用来生成追踪后下载到本地。

## 前置条件

- Python 3.11+
- Modal 账号及 CLI（`pip install modal && modal token new`）
- 推荐使用 conda 环境：

```bash
conda create -n modal python=3.11
conda activate modal
pip install modal
modal token new   # 首次使用需要在浏览器授权
```

## 使用方法

```bash
conda activate modal
cd util/tracer_nvbit/modal/
modal run trace_h100.py
```

追踪文件将下载到 `./traces_output/`。

## 工作流程

`trace_h100.py` 在 Modal H100 容器内执行以下步骤：

```
┌─────────────────────────────────────────────────────┐
│  Modal H100 Container (CUDA 12.8, Ubuntu 22.04)     │
│                                                      │
│  1. install_nvbit.sh  → 下载 NVBit 1.7.6            │
│  2. make tracer_tool  → 编译追踪插桩工具              │
│  3. make post-traces-processing → 编译后处理程序      │
│  4. nvcc vector_add.cu → 编译目标 CUDA 应用           │
│  5. LD_PRELOAD=tracer_tool.so ./vector_add           │
│     → 运行应用并捕获 SASS 追踪                       │
│  6. post-traces-processing kernelslist_ctx_*          │
│     → 按线程块分组，生成 .traceg 和 kernelslist.g     │
│                                                      │
│  输出文件通过 Modal 返回值传回本地                     │
└─────────────────────────────────────────────────────┘
```

## 输出文件

| 文件 | 说明 |
|------|------|
| `kernelslist.g` | Accel-Sim 仿真入口，记录 memcpy 操作和 kernel 追踪路径 |
| `kernel-*.traceg.xz` | 后处理追踪（按线程块分组），供仿真器消费 |
| `kernel-*.trace.xz` | 原始追踪数据 |
| `kernelslist_ctx_*` | 原始 kernel 列表 |
| `stats_ctx_*` | 追踪统计信息 |

## 追踪自定义应用

编辑 `trace_h100.py` 中的 `VECADD_CU` 变量，替换为你的 CUDA 源码。如果应用有多个源文件或依赖库，需要修改步骤 4 的编译命令。

对于更复杂的场景，可以通过环境变量控制追踪行为：

```python
run(
    f"DYNAMIC_KERNEL_RANGE='1-3' "          # 只追踪 kernel 1~3
    f"LD_PRELOAD={workdir}/tracer_tool/tracer_tool.so "
    f"./your_app",
    cwd=app_dir,
)
```

详见 [tracer_nvbit 文档](../CLAUDE.md) 中的环境变量说明。

## 注意事项

- Modal 可能分配 H200（同为 SM90 架构）代替 H100，追踪完全兼容
- NVBit 1.7.6 支持到 SM90，更新架构需检查 NVBit 版本兼容性
- 追踪文件默认启用 xz 压缩（`TOOL_COMPRESS=1`）
- 目前 Accel-Sim 尚无官方 SM90 配置文件，追踪已就绪待配置支持
