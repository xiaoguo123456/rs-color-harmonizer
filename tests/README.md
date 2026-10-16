# Landsat 多景回归测试

`landsat_multiscene_regression.py` 是用于辐射归一化的轻量、确定性测试夹具。仓库不提交真实 Landsat 像元，也不会自动下载大影像。脚本生成三景 192×192、4 波段（B2/B3/B4/B5）、30 米分辨率的合成数据，包含空间重叠、云掩膜、场景增益/偏置和照度渐变。

## 运行

在仓库根目录执行：

```bash
python3 tests/landsat_multiscene_regression.py --generate /tmp/landsat-fixture
python3 tests/landsat_multiscene_regression.py --run /tmp/landsat-fixture
```

脚本会写出 `regression_results.json`；指标不达标时返回非零退出码。固定种子 `20240624` 下的阈值为：

- 每个重叠区至少 10000 个有效像元
- 处理后/处理前重叠 MAE 比不超过 0.35
- 处理后重叠 RMSE 不超过 0.012（反射率单位）

参考实现对有效重叠像元拟合逐波段仿射变换，使用 2%～98% 分位数去除极端值，排除云/无效掩膜并将输出裁剪到 [0, 1]。生产版 C++/GDAL 测试可以替换拟合逻辑，但复用相同的重叠区指标。

## 可选的官方元数据

```bash
python3 tests/landsat_multiscene_regression.py --generate /tmp/landsat-fixture --stac-metadata
```

该选项只查询三个 USGS STAC Feature（每个约几十 KB），不会下载影像。真实数据来源和 requester-pays 限制见 `docs/landsat_data_sources.md`。
