# 测试说明

## 1. 核心单元测试

在有 CMake 的机器上：

```bash
cmake -S . -B build -DRSCH_BUILD_QT=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

没有 CMake 时，可直接使用 g++ 验证核心库：

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude src/core.cpp tests/test_core.cpp -o /tmp/rsch_core_tests
/tmp/rsch_core_tests
```

测试会在临时目录生成一个带 EPSG:4326、仿射变换、NoData 元数据的 RGB UInt16 GeoTIFF，验证读写、元数据、分块统计、均值/方差迁移与多景参考流程。

## 2. Landsat 多景回归

仓库不提交真实 Landsat 影像，避免仓库膨胀和误分发数据许可。可以用同一轨道的多景、同一波段（例如 Landsat 8/9 Collection 2 Level-2 的 SR_B2/SR_B3/SR_B4）在本地准备 2～4 个 GeoTIFF，然后执行：

```bash
./build/rsch inspect scene_01.tif
./build/rsch stats scene_01.tif --block 512x512
./build/rsch harmonize out scene_01.tif scene_02.tif scene_03.tif --reference 0 --strength 0.8
```

建议的回归检查：

- 每个输出的宽高、波段数、投影、分辨率和地理范围与输入一致
- 输出各波段均值与参考景均值的差异明显小于处理前
- 输出不存在 NaN/Inf，NoData 掩膜没有被当作有效像元参与统计
- 同一组输入重复运行时输出字节和统计结果稳定

可使用 USGS EarthExplorer、AWS Landsat Collection 2 公共桶或 Microsoft Planetary Computer 下载数据。下载与使用需遵守相应数据服务条款；脚本和样例只保留元数据，不在仓库内存放影像。

## 3. 当前环境验证记录

本开发环境没有 CMake、GDAL/OpenCV/Qt 开发包，因此无法在这里运行 CMake/CTest 或 Qt GUI。已用 g++ 直接编译并通过核心测试：

```text
rsch_core_tests: all checks passed
```

CLI 的 `inspect`、`stats`、`match` 也已使用合成 GeoTIFF 验证。安装依赖后的 CI 应补充 CMake 构建和真实 Landsat 小样本回归。

仓库还提供轻量、确定性的 Landsat-like 多景回归脚本（只生成合成数据，不下载真实影像）：

```bash
python3 tests/landsat_multiscene_regression.py --generate /tmp/rsch-landsat-fixture
python3 tests/landsat_multiscene_regression.py --run /tmp/rsch-landsat-fixture
```

本次验证结果：最小有效重叠像元 12044，处理后/处理前最大 MAE 比 0.2171，最大重叠 RMSE 0.00536，回归 PASS。脚本中的 3 个场景记录了可追溯的 USGS Landsat Collection 2 场景 ID；`--stac-metadata` 可选地查询 USGS STAC 元数据，但不会下载影像。
