[根目录](../../CLAUDE.md) > [util](../) > **tuner**

# tuner -- 自动化配置调优工具

## 变更记录 (Changelog)

| 日期 | 变更内容 |
|------|----------|
| 2026-04-03 | 初始生成 |

---

## 模块职责

通过微基准测试自动探测 GPU 硬件参数（缓存层次、执行单元配置、内存带宽等），生成匹配目标硬件的 GPGPU-Sim 和 Accel-Sim 配置文件。

---

## 入口与启动

### 调优流程
```bash
# 0. 获取微基准测试
./get_ubench.sh

# 1. 编译并运行微基准测试
make -C ./GPU_Microbenchmark/
export CUDA_VISIBLE_DEVICES=0
./run_all.sh | tee stats.txt

# 2. 运行调优器生成配置
./tuner.py -s stats.txt

# 3. 复制生成的配置到正确位置
cp -r <DEVICE_NAME> ../../gpu-simulator/gpgpu-sim/configs/tested-cfgs/
cp -r <DEVICE_NAME> ../../gpu-simulator/configs/tested-cfgs/
```

---

## 对外接口

### tuner.py
- 输入：微基准测试输出（`stats.txt`）
- 输出：以设备名命名的目录，包含 `gpgpusim.config` 和 `trace.config`

### 参数搜索
对于无法直接通过微基准测试确定的参数，需要进行 16 种组合的搜索：
| 参数 | 可选值 |
|------|--------|
| Warp 调度策略 | lrr (Loose Round-Robin) / gto (Greedy) |
| L2 缓存交错粒度 | 32B / 256B |
| L2 缓存哈希函数 | Linear / IPOLY |
| 内存调度策略 | FCFS / FR-FCFS |

---

## 关键依赖与配置

- **真实 GPU**：需要目标硬件在本地
- **hw_def 头文件**：用户提供的最小硬件信息（核心型号、内存型号等）
- **配置模板**：`config_template/gpgpusim.config`
- 微基准测试信息来源：用户输入 + CUDA 设备查询 + 微基准测试结果 + 参数搜索

---

## 数据模型

调优器从四个来源收集参数：
1. 用户提供的 `hw_def` 头文件
2. 微基准测试套件（缓存、内存、执行单元配置）
3. CUDA deviceQuery 报告（SM 数量、内存宽度等）
4. 参数搜索空间（调度、缓存哈希等）

---

## 相关文件清单

| 文件 | 说明 |
|------|------|
| `tuner.py` | 调优器主脚本 |
| `run_all.sh` | 微基准测试批量运行脚本 |
| `get_ubench.sh` | 微基准测试获取脚本 |
| `config_template/gpgpusim.config` | 配置模板 |
| `README.md` | 详细使用文档 |
