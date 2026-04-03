[根目录](../../CLAUDE.md) > [util](../) > **plotting**

# plotting -- 关联分析与可视化

## 变更记录 (Changelog)

| 日期 | 变更内容 |
|------|----------|
| 2026-04-03 | 初始生成 |

---

## 模块职责

将 Accel-Sim 仿真结果与真实硬件性能数据进行关联分析，生成可交互的 HTML 图表和相关性报告。支持 per-app 和 per-kernel 粒度的对比。

---

## 入口与启动

### 快速柱状图
```bash
./plot-get-stats.py -c per-app-stats.csv
# 输出到 ./htmls/
```

### 关联性分析
```bash
# 先收集仿真统计
../job_launching/get_stats.py -R -K -k -C QV100-SASS -B rodinia_2.0-ft > correl.csv

# 生成关联图表
./plot-correlation.py -c correl.csv -H ../../hw_run/QUADRO-V100/device-0/9.1/
# 输出到 ./correl-html/
```

---

## 对外接口

### plot-correlation.py
- 输入：仿真统计 CSV + 硬件统计目录
- 输出：交互式 HTML 图表（per-app 和 per-kernel）、CSV 摘要、相关性系数
- 支持 PDF 输出（`-i pdf`）

### plot-get-stats.py
- 输入：`get_stats.py` 输出的 CSV
- 输出：柱状图 HTML

---

## 关键依赖与配置

- `plotly` Python 库
- `correl_mappings.py` -- 硬件统计到仿真统计的映射关系（nvprof/nsight -> Accel-Sim）
- `known.correlation.outliers.list` -- 已知的关联异常值列表

---

## 相关文件清单

| 文件 | 说明 |
|------|------|
| `plot-correlation.py` | 关联性分析主脚本 |
| `plot-get-stats.py` | 柱状图生成脚本 |
| `correl_mappings.py` | 硬件-仿真统计映射 |
| `merge-stats.py` | 统计合并工具 |
| `correlate_and_publish.sh` | 自动化关联与发布 |
| `plot-public.sh` | 公开发布脚本 |
| `README.md` | 使用文档 |
