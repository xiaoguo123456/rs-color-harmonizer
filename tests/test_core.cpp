#include "rsch/core.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

using namespace rsch;

int main() {
    Raster first;
    first.metadata.width = 16;
    first.metadata.height = 8;
    first.metadata.bands = 3;
    first.metadata.pixel_type = PixelType::UInt16;
    first.metadata.geotransform.values = {120.0, 0.00025, 0, 35.0, 0, -0.00025};
    first.metadata.geotransform.valid = true;
    first.metadata.projection = "EPSG:4326";
    first.metadata.nodata = "0";
    first.data.resize(first.pixelCount() * 3);
    for (std::uint32_t y = 0; y < first.metadata.height; ++y)
        for (std::uint32_t x = 0; x < first.metadata.width; ++x) {
            first.at(x, y, 0) = 100 + x + y;
            first.at(x, y, 1) = 200 + 2 * x + y;
            first.at(x, y, 2) = 300 + 3 * x + 2 * y;
        }
    const auto path = std::filesystem::temp_directory_path() / "rsch_core_test.tif";
    writeGeoTiff(first, path, PixelType::UInt16);
    const auto decoded = readGeoTiff(path);
    assert(decoded.metadata.width == first.metadata.width);
    assert(decoded.metadata.height == first.metadata.height);
    assert(decoded.metadata.bands == first.metadata.bands);
    assert(decoded.metadata.projection == first.metadata.projection);
    assert(decoded.metadata.geotransform.valid);
    assert(std::abs(decoded.at(4, 3, 2) - first.at(4, 3, 2)) <= 1);

    const auto stats = computeBlockStatistics(decoded, 5, 3);
    assert(stats.bands.size() == 3 && stats.bands[0].count == first.pixelCount());
    assert(stats.bands[0].minimum == 100 && stats.bands[0].maximum == 122);

    Raster target = decoded;
    for (auto& x : target.data) x = x * 2.0F + 30.0F;
    auto matched = transferColor(target, decoded, {.strength = 1.0, .match_variance = true, .output_min = 0, .output_max = 65535});
    const auto matched_stats = computeBlockStatistics(matched);
    assert(std::abs(matched_stats.bands[0].mean - stats.bands[0].mean) < 1e-2);

    std::vector<Raster> scenes{decoded, target};
    HarmonizationReport report;
    const auto outputs = harmonizeScenes(scenes, 0, {.strength = 1.0, .match_variance = true, .output_min = 0, .output_max = 65535}, &report);
    assert(outputs.size() == 2 && report.reference_index == 0);
    std::filesystem::remove(path);
    std::cout << "rsch_core_tests: all checks passed\n";
}
