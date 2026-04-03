[根目录](../../CLAUDE.md) > [util](../) > **job_launching**

# job_launching -- 仿真任务调度与管理

## 变更记录 (Changelog)

| 日期 | 变更内容 |
|------|----------|
| 2026-04-03 | 初始生成 |

---

## 模块职责

提供标准化的仿真任务全生命周期管理，包括：
- 批量启动多基准测试 x 多配置的仿真矩阵
- 任务状态监控与错误聚合
- 统计数据收集与格式化输出（CSV）
- 支持多种任务管理器（Torque/Slurm/本地 procman）

---

## 入口与启动

### 核心脚本

| 脚本 | 职责 |
|------|------|
| `run_simulations.py` | 启动仿真任务（配置环境、创建运行目录、分发任务） |
| `monitor_func_test.py` | 监控任务状态，汇报通过/失败 |
| `job_status.py` | 打印所有任务的实时状态摘要 |
| `get_stats.py` | 收集已完成任务的统计数据，输出 CSV |

### 典型工作流
```bash
# 1. 启动 SASS 仿真
./run_simulations.py -B rodinia_2.0-ft -C QV100-SASS \
    -T ../../hw_run/traces/device-0/12.8/ -N myTest

# 2. 监控任务
./monitor_func_test.py -v -N myTest

# 3. 查看状态
./job_status.py -N myTest

# 4. 收集统计
./get_stats.py -N myTest | tee stats.csv

# 按 kernel 粒度收集（用于关联分析）
./get_stats.py -K -k -R -B rodinia_2.0-ft -C QV100-SASS | tee per-kernel.csv
```

---

## 对外接口

### run_simulations.py 关键参数
| 参数 | 说明 |
|------|------|
| `-B` | 基准测试套件名称（逗号分隔多个） |
| `-C` | GPU 配置名称（逗号分隔多个，如 `QV100-SASS,A100-PTX`） |
| `-T` | 追踪文件根目录（SASS 模式必须） |
| `-N` | 本次运行的标识符（用于后续监控和统计收集） |
| `-M` | 内存限制（如 `70G`） |

---

## 关键依赖与配置

### 配置定义文件
- `configs/define-standard-cfgs.yml` -- GPU 配置模板和组合选项
  - 基础配置（GPU 型号）+ 可组合的额外参数（SASS/PTX、调度策略、缓存配置等）
- `apps/define-all-apps.yml` -- 基准测试应用定义
  - 包含可执行文件路径、数据目录、命令行参数、内存需求

### 运行目录结构
```
sim_run_<cuda_version>/
  <app_name>/
    <app_args>/
      <config>/
        justrun.sh          # 可用于 gdb 调试
        *.o<jobId>          # stdout 输出
        *.e<jobId>          # stderr 输出
```

### 统计定义
- `stats/example_stats.yml` -- 默认统计项（正则表达式匹配仿真输出）

---

## 数据模型

- 应用定义：`apps/define-*.yml` -- YAML 格式，定义 exec_dir、data_dirs、execs 及参数
- 配置定义：`configs/define-*.yml` -- YAML 格式，定义 base_file 和 extra_params
- 公共库：`common.py` -- 共享的路径解析和配置加载逻辑

---

## 测试与质量

- 集成在 CI 流水线中使用，不单独测试
- `monitor_func_test.py` 会检查每个任务的通过/失败状态
- 支持的应用列表文件：`apps/*.list`（如 `all-apps.list`、`ft-apps.list`）

---

## 相关文件清单

| 文件 | 说明 |
|------|------|
| `run_simulations.py` | 任务启动脚本 |
| `monitor_func_test.py` | 任务监控脚本 |
| `job_status.py` | 状态查看脚本 |
| `get_stats.py` | 统计收集脚本 |
| `common.py` | 公共库 |
| `procman.py` | 本地任务管理器 |
| `slurm.sim` / `torque.sim` | 集群任务模板 |
| `configs/define-standard-cfgs.yml` | GPU 配置定义 |
| `apps/define-all-apps.yml` | 应用定义 |
| `stats/example_stats.yml` | 统计项定义 |
| `README.md` | 详细使用文档 |
