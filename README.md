# RS Color Harmonizer

单机 MVP：面向同一传感器、相近时相 GeoTIFF 场景的分块统计、参考影像选择、均值/方差颜色迁移、简单匀光和 GeoTIFF 导出。核心库是 C++20，命令行工具在没有 Qt/GDAL/OpenCV 开发包时也可以构建。

## 快速构建

```bash
cmake -S . -B build -DRSCH_BUILD_QT=OFF
cmake --build build -j
ctest --test-dir build --output-on-failure
```

若机器没有 CMake，可用以下最小编译命令验证核心：

```bash
g++ -std=c++20 -Iinclude src/core.cpp tests/test_core.cpp -o /tmp/rsch_core_tests
/tmp/rsch_core_tests
g++ -std=c++20 -Iinclude src/core.cpp apps/rsch.cpp -o /tmp/rsch
```

## CLI

```text
rsch inspect scene.tif
rsch stats scene.tif --block 512x512
rsch match target.tif reference.tif matched.tif --strength 0.8 --type uint16
rsch equalize scene.tif equalized.tif --strength 0.5
rsch harmonize out/ scene_a.tif scene_b.tif --reference 0
```

核心内置了无第三方依赖的 baseline TIFF/GeoTIFF 读写器，支持 little/big endian、无压缩 strip、8/16 位整数、32 位浮点、灰度/RGB、NoData、ModelPixelScale/ModelTiepoint 和常见 EPSG GeoKey。生产数据若含压缩、分块或复杂 CRS，建议启用 GDAL（`-DRSCH_USE_GDAL=ON`）并在后续版本切换为 GDAL 的窗口读取路径。当前实现会拒绝压缩/平面分离 TIFF，并明确报告原因。

Qt Quick 界面位于 `qml/Main.qml`，默认关闭；安装 Qt 6 后使用 `-DRSCH_BUILD_QT=ON` 构建。界面提供场景列表、参考影像、迁移强度和导出流程骨架，核心算法仍可独立运行。

## 算法边界

MVP 的颜色迁移按波段统计量执行：`y = (x - mean_target) * (std_reference / std_target) + mean_reference`，`strength` 可在 0 与 1 之间混合。`equalizeLighting` 使用分块遍历的稳健全局亮度偏移。云、阴影、积雪和变化地物尚未自动检测，应在生产运行前提供掩膜；接缝线和羽化会在后续阶段接入。

核心测试生成小型合成 GeoTIFF，验证元数据、坐标、分块统计、颜色迁移和多场景参考流程，不下载 Landsat 数据。真实 Landsat 回归数据建议在本地按数据许可准备，并通过 `rsch stats` 对比基线统计。
