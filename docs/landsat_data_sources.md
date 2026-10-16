# Landsat 8/9 数据来源与真实回放方案

## 官方来源

- USGS Landsat Collection 2 Level-2：<https://www.usgs.gov/landsat-missions/landsat-collection-2-level-2-science-products>，光学表面反射率资产是 COG GeoTIFF，可引用 DOI `10.5066/P9OGBGM6`
- USGS Landsat STAC：<https://landsatlook.usgs.gov/stac-server/>，脚本只查询元数据，不自动下载场景
- AWS Open Data 镜像：<https://registry.opendata.aws/usgs-landsat/>，位于 `us-west-2`，是 requester-pays，需要调用者自己的 AWS 账户并承担请求/流量费用
- Earth Search STAC：<https://earth-search.aws.element84.com/v1/>，便于发现数据，但不能绕过 USGS/AWS 的访问和许可要求
- Google Earth Engine：公开目录 `LANDSAT/LC08/C02/T1_L2`、`LANDSAT/LC09/C02/T1_L2`，需要认证项目和配额，适合手动导出小窗口

## 建议的三景元数据样本

| 场景 | 平台 | WRS path/row | 获取日期 | 陆地区域云量 |
|---|---|---|---|---:|
| `LC08_L2SP_045034_20240624_20240708_02_T1_SR` | Landsat 8 | 045/034 | 2024-06-24 | 7.78% |
| `LC09_L2SP_045034_20240616_20240617_02_T1_SR` | Landsat 9 | 045/034 | 2024-06-16 | 3.33% |
| `LC09_L2SP_044034_20240625_20240626_02_T1_SR` | Landsat 9 | 044/034 | 2024-06-25 | 21.39% |

前两景足够做同轨道时间归一化，第三景用于相邻轨道重叠测试。真实测试必须排除云、云影、卷云、雪、填充值和发生变化的地物。

## 真实数据回放建议

1. 先用 STAC 查看几何、云量、投影、仿射变换和资产链接
2. 选择至少两景共有的 256×256 或更小窗口，只读取 B2/B3/B4/B5 和 QA_PIXEL 的 COG 范围，不下载整景
3. 对 Collection 2 Level-2 光学波段应用比例因子和偏置：`reflectance = DN * 0.0000275 - 0.2`
4. 记录场景 ID、资产链接、窗口、QA 位掩膜、缩放参数、校验和与软件版本，再比较 MAE、RMSE、均值/标准差比和接缝跳变

USGS 资产可能重定向到登录，AWS 镜像是 requester-pays，因此仓库只提供合成夹具和元数据查询，不存放真实 COG 或凭据，避免大文件和意外费用。

## 已验证的真实 Landsat 样例

为了做离线、可复现的真实数据回归，本项目还使用了欧盟 JRC IMPACT Toolbox 提供的 Brazil Landsat 样例：
<https://forobs.jrc.ec.europa.eu/IMPACT/images>

下载包 `Landsat_test_data_1990_2000_2014_Brazil.zip` 约 27 MB，包含三景完整 GeoTIFF：

- `tm_226-068_02081990_calrefbyt.tif`
- `tm_226-068_28072000_calrefbyt.tif`
- `oli_226-068_03072014_calrefbyt.tif`

三景均为 1557×1603 像元、EPSG:32621。本次测试用 GDAL 从每景选取 B3/B2/B1 生成无压缩 RGB GeoTIFF，再用 `rsch harmonize` 以 2000 年景为参考处理 1990 年和 2014 年景。完整整景前后截图和统计结果已在开发记录中验证。
