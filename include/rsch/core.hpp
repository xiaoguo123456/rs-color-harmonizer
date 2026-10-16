#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rsch {

enum class PixelType { UInt8, UInt16, Float32 };

struct GeoTransform {
    // GDAL order: origin x, pixel width, x rotation, origin y, y rotation,
    // pixel height (usually negative for north-up rasters).
    std::array<double, 6> values{0, 1, 0, 0, 0, -1};
    bool valid{false};
};

struct RasterMetadata {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint16_t bands{0};
    PixelType pixel_type{PixelType::UInt8};
    GeoTransform geotransform{};
    std::string projection;
    std::string nodata;
    std::string software;
    std::string description;
};

// A compact, interleaved-in-memory raster. Values are represented as float so
// harmonization does not lose precision; output chooses the requested type.
struct Raster {
    RasterMetadata metadata;
    std::vector<float> data;

    std::size_t pixelCount() const noexcept {
        return static_cast<std::size_t>(metadata.width) * metadata.height;
    }
    std::size_t size() const noexcept { return data.size(); }
    float& at(std::uint32_t x, std::uint32_t y, std::uint16_t band = 0);
    float at(std::uint32_t x, std::uint32_t y, std::uint16_t band = 0) const;
};

struct BandStatistics {
    std::uint64_t count{0};
    double minimum{0};
    double maximum{0};
    double mean{0};
    double standard_deviation{0};
};

struct BlockStatistics {
    RasterMetadata metadata;
    std::vector<BandStatistics> bands;
    std::uint32_t block_width{256};
    std::uint32_t block_height{256};
};

struct ColorTransferParameters {
    // 0 leaves a band unchanged, 1 applies the full transfer.
    double strength{1.0};
    bool match_variance{true};
    float output_min{0.0F};
    float output_max{255.0F};
};

struct HarmonizationReport {
    std::size_t reference_index{0};
    std::vector<BandStatistics> reference_statistics;
    std::vector<std::vector<BandStatistics>> input_statistics;
    std::vector<std::array<double, 2>> applied_gain_offset;
};

// Read a baseline GeoTIFF/GeoTIFF-like file without requiring GDAL. The
// implementation supports uncompressed strips, 8/16-bit integer and float32,
// RGB or grayscale, and common GeoTIFF coordinate tags. Builds that have GDAL
// available may define RSCH_WITH_GDAL to use GDAL in future releases.
Raster readGeoTiff(const std::filesystem::path& path);
RasterMetadata readGeoTiffMetadata(const std::filesystem::path& path);
void writeGeoTiff(const Raster& raster, const std::filesystem::path& path,
                  PixelType output_type = PixelType::UInt8,
                  bool overwrite = true);

BlockStatistics computeBlockStatistics(const Raster& raster,
                                       std::uint32_t block_width = 256,
                                       std::uint32_t block_height = 256,
                                       std::optional<float> nodata = std::nullopt);
BlockStatistics computeBlockStatistics(const std::filesystem::path& path,
                                       std::uint32_t block_width = 256,
                                       std::uint32_t block_height = 256,
                                       std::optional<float> nodata = std::nullopt);

// Transfer per-band mean and (optionally) standard deviation from reference
// into target. Metadata and dimensions are preserved.
Raster transferColor(const Raster& target, const Raster& reference,
                     ColorTransferParameters parameters = {});

// A robust single-scene lighting correction: each band is centered around the
// median scene level while retaining a configurable amount of local contrast.
Raster equalizeLighting(const Raster& input, double strength = 1.0,
                        std::optional<float> nodata = std::nullopt);

// Harmonize scenes against the selected reference. If reference_index is not
// supplied, the scene with the smallest median luminance distance to all other
// scenes is selected.
std::vector<Raster> harmonizeScenes(const std::vector<Raster>& scenes,
                                    std::optional<std::size_t> reference_index = std::nullopt,
                                    ColorTransferParameters parameters = {},
                                    HarmonizationReport* report = nullptr);

std::size_t chooseReferenceScene(const std::vector<Raster>& scenes);
std::string pixelTypeName(PixelType type);
PixelType pixelTypeFromName(const std::string& name);

}  // namespace rsch

