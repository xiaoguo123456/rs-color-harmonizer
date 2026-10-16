#include "rsch/core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string_view>

#ifdef RSCH_WITH_GDAL
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#include <cpl_string.h>
#endif

namespace rsch {
namespace {

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("rsch: " + message); }

#ifdef RSCH_WITH_GDAL
Raster readWithGdal(const std::filesystem::path& path) {
    GDALAllRegister();
    auto* dataset = static_cast<GDALDataset*>(GDALOpen(path.string().c_str(), GA_ReadOnly));
    if (!dataset) return {};
    const auto width = dataset->GetRasterXSize();
    const auto height = dataset->GetRasterYSize();
    const auto bands = dataset->GetRasterCount();
    if (width <= 0 || height <= 0 || bands <= 0) { GDALClose(dataset); return {}; }
    Raster result;
    result.metadata.width = static_cast<std::uint32_t>(width);
    result.metadata.height = static_cast<std::uint32_t>(height);
    result.metadata.bands = static_cast<std::uint16_t>(bands);
    const auto type = dataset->GetRasterBand(1)->GetRasterDataType();
    result.metadata.pixel_type = type == GDT_Byte ? PixelType::UInt8 : type == GDT_UInt16 ? PixelType::UInt16 : PixelType::Float32;
    double transform[6]{};
    if (dataset->GetGeoTransform(transform) == CE_None) { std::copy(std::begin(transform), std::end(transform), result.metadata.geotransform.values.begin()); result.metadata.geotransform.valid = true; }
    if (const char* projection = dataset->GetProjectionRef(); projection && *projection) result.metadata.projection = projection;
    if (const char* software = dataset->GetMetadataItem("TIFFTAG_SOFTWARE"); software) result.metadata.software = software;
    result.data.resize(static_cast<std::size_t>(width) * height * bands);
    for (int band = 1; band <= bands; ++band) {
        auto* rb = dataset->GetRasterBand(band);
        int has_nodata = 0; const auto nodata = rb->GetNoDataValue(&has_nodata);
        if (has_nodata && result.metadata.nodata.empty()) result.metadata.nodata = std::to_string(nodata);
        std::vector<float> plane(static_cast<std::size_t>(width) * height);
        if (rb->RasterIO(GF_Read, 0, 0, width, height, plane.data(), width, height, GDT_Float32, 0, 0, nullptr) != CE_None) { GDALClose(dataset); fail("GDAL RasterIO failed for " + path.string()); }
        for (std::size_t i = 0; i < plane.size(); ++i) result.data[i * bands + (band - 1)] = plane[i];
    }
    GDALClose(dataset);
    return result;
}

bool writeWithGdal(const Raster& raster, const std::filesystem::path& path, PixelType output_type, bool overwrite) {
    GDALAllRegister();
    if (!overwrite && std::filesystem::exists(path)) fail("output exists: " + path.string());
    auto* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) return false;
    if (overwrite) VSIUnlink(path.string().c_str());
    char** options = nullptr;
    options = CSLSetNameValue(options, "TILED", "YES");
    options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
    const auto gdal_type = output_type == PixelType::UInt8 ? GDT_Byte : output_type == PixelType::UInt16 ? GDT_UInt16 : GDT_Float32;
    auto* dataset = driver->Create(path.string().c_str(), raster.metadata.width, raster.metadata.height, raster.metadata.bands, gdal_type, options);
    CSLDestroy(options);
    if (!dataset) return false;
    if (raster.metadata.geotransform.valid) dataset->SetGeoTransform(const_cast<double*>(raster.metadata.geotransform.values.data()));
    if (!raster.metadata.projection.empty()) {
        if (raster.metadata.projection.rfind("EPSG:", 0) == 0) {
            try {
                OGRSpatialReference srs;
                srs.importFromEPSG(std::stoi(raster.metadata.projection.substr(5)));
                char* wkt = nullptr;
                if (srs.exportToWkt(&wkt) == OGRERR_NONE) { dataset->SetProjection(wkt); CPLFree(wkt); }
            } catch (...) {}
        } else dataset->SetProjection(raster.metadata.projection.c_str());
    }
    for (std::uint16_t band = 0; band < raster.metadata.bands; ++band) {
        std::vector<float> plane(raster.pixelCount());
        for (std::size_t i = 0; i < plane.size(); ++i) plane[i] = raster.data[i * raster.metadata.bands + band];
        if (output_type == PixelType::UInt8) for (auto& value : plane) value = std::clamp(value, 0.0F, 255.0F);
        else if (output_type == PixelType::UInt16) for (auto& value : plane) value = std::clamp(value, 0.0F, 65535.0F);
        auto* rb = dataset->GetRasterBand(band + 1);
        if (!raster.metadata.nodata.empty()) { try { rb->SetNoDataValue(std::stod(raster.metadata.nodata)); } catch (...) {} }
        if (rb->RasterIO(GF_Write, 0, 0, raster.metadata.width, raster.metadata.height, plane.data(), raster.metadata.width, raster.metadata.height, GDT_Float32, 0, 0, nullptr) != CE_None) { GDALClose(dataset); fail("GDAL RasterIO failed writing " + path.string()); }
    }
    GDALClose(dataset);
    return true;
}
#endif

struct TiffReader {
    std::vector<std::uint8_t> bytes;
    bool little{true};

    std::uint16_t u16(std::size_t p) const {
        if (p + 2 > bytes.size()) fail("truncated TIFF");
        if (little) return static_cast<std::uint16_t>(bytes[p] | (bytes[p + 1] << 8));
        return static_cast<std::uint16_t>((bytes[p] << 8) | bytes[p + 1]);
    }
    std::uint32_t u32(std::size_t p) const {
        if (p + 4 > bytes.size()) fail("truncated TIFF");
        if (little) return static_cast<std::uint32_t>(bytes[p] | (bytes[p + 1] << 8) |
                                                      (bytes[p + 2] << 16) | (bytes[p + 3] << 24));
        return static_cast<std::uint32_t>((bytes[p] << 24) | (bytes[p + 1] << 16) |
                                          (bytes[p + 2] << 8) | bytes[p + 3]);
    }
    std::uint64_t u64(std::size_t p) const {
        if (p + 8 > bytes.size()) fail("truncated TIFF");
        std::uint64_t result = 0;
        if (little) {
            for (int i = 7; i >= 0; --i) result = (result << 8) | bytes[p + i];
        } else {
            for (int i = 0; i < 8; ++i) result = (result << 8) | bytes[p + i];
        }
        return result;
    }
    float f32(std::size_t p) const {
        std::uint32_t x = u32(p);
        float f{};
        std::memcpy(&f, &x, sizeof f);
        return f;
    }
    double f64(std::size_t p) const {
        std::uint64_t x = u64(p);
        double f{};
        std::memcpy(&f, &x, sizeof f);
        return f;
    }
};

struct TiffEntry {
    std::uint16_t tag{};
    std::uint16_t type{};
    std::uint32_t count{};
    std::size_t value_offset{};
};

std::size_t typeSize(std::uint16_t type) {
    switch (type) {
        case 1: return 1;  // BYTE
        case 2: return 1;  // ASCII
        case 3: return 2;  // SHORT
        case 4: return 4;  // LONG
        case 5: return 8;  // RATIONAL
        case 11: return 4; // FLOAT
        case 12: return 8; // DOUBLE
        default: fail("unsupported TIFF field type " + std::to_string(type));
    }
}

std::vector<TiffEntry> readEntries(const TiffReader& t, std::uint32_t ifd_offset) {
    if (ifd_offset + 2 > t.bytes.size()) fail("invalid TIFF IFD offset");
    const auto count = t.u16(ifd_offset);
    if (ifd_offset + 2ULL + static_cast<std::size_t>(count) * 12 > t.bytes.size())
        fail("truncated TIFF IFD");
    std::vector<TiffEntry> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto p = ifd_offset + 2 + i * 12;
        TiffEntry e;
        e.tag = t.u16(p);
        e.type = t.u16(p + 2);
        e.count = t.u32(p + 4);
        const std::size_t bytes_count = typeSize(e.type) * static_cast<std::size_t>(e.count);
        e.value_offset = bytes_count <= 4 ? p + 8 : t.u32(p + 8);
        if (e.value_offset + bytes_count > t.bytes.size()) fail("TIFF value outside file");
        result.push_back(e);
    }
    return result;
}

const TiffEntry* findEntry(const std::vector<TiffEntry>& entries, std::uint16_t tag) {
    for (const auto& e : entries) if (e.tag == tag) return &e;
    return nullptr;
}

std::vector<std::uint32_t> readInts(const TiffReader& t, const TiffEntry& e) {
    std::vector<std::uint32_t> values;
    values.reserve(e.count);
    for (std::uint32_t i = 0; i < e.count; ++i) {
        const auto p = e.value_offset + i * typeSize(e.type);
        switch (e.type) {
            case 1: values.push_back(t.bytes[p]); break;
            case 3: values.push_back(t.u16(p)); break;
            case 4: values.push_back(t.u32(p)); break;
            default: fail("TIFF integer tag has non-integer type");
        }
    }
    return values;
}

std::vector<double> readDoubles(const TiffReader& t, const TiffEntry& e) {
    std::vector<double> values;
    values.reserve(e.count);
    for (std::uint32_t i = 0; i < e.count; ++i) {
        const auto p = e.value_offset + i * typeSize(e.type);
        if (e.type == 12) values.push_back(t.f64(p));
        else if (e.type == 11) values.push_back(t.f32(p));
        else if (e.type == 5) {
            const auto denominator = t.u32(p + 4);
            values.push_back(denominator == 0 ? 0 : static_cast<double>(t.u32(p)) / denominator);
        } else fail("TIFF value is not floating point");
    }
    return values;
}

std::string readAscii(const TiffReader& t, const TiffEntry& e) {
    std::string result;
    result.reserve(e.count);
    for (std::uint32_t i = 0; i < e.count; ++i) {
        const auto c = static_cast<char>(t.bytes[e.value_offset + i]);
        if (c == '\0') break;
        result.push_back(c);
    }
    return result;
}

TiffReader loadTiff(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) fail("cannot open " + path.string());
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size < 8) fail("file is not a TIFF: " + path.string());
    stream.seekg(0);
    TiffReader t;
    t.bytes.resize(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(t.bytes.data()), size);
    if (t.bytes[0] == 'I' && t.bytes[1] == 'I') t.little = true;
    else if (t.bytes[0] == 'M' && t.bytes[1] == 'M') t.little = false;
    else fail("unsupported TIFF byte order");
    if (t.u16(2) != 42) fail("unsupported TIFF magic");
    return t;
}

struct ParsedTiff {
    TiffReader reader;
    std::vector<TiffEntry> entries;
};

ParsedTiff parseTiff(const std::filesystem::path& path) {
    auto t = loadTiff(path);
    const auto offset = t.u32(4);
    auto entries = readEntries(t, offset);
    return {std::move(t), std::move(entries)};
}

Raster decode(const std::filesystem::path& path) {
    auto parsed = parseTiff(path);
    auto& t = parsed.reader;
    const auto& entries = parsed.entries;
    auto required = [&](std::uint16_t tag) -> const TiffEntry& {
        const auto* e = findEntry(entries, tag);
        if (!e) fail("TIFF is missing tag " + std::to_string(tag));
        return *e;
    };
    const auto width = readInts(t, required(256)).at(0);
    const auto height = readInts(t, required(257)).at(0);
    const auto bits = readInts(t, required(258));
    const auto samples = findEntry(entries, 277) ? readInts(t, *findEntry(entries, 277)).at(0) : 1;
    const auto compression = findEntry(entries, 259) ? readInts(t, *findEntry(entries, 259)).at(0) : 1;
    if (compression != 1) fail("only uncompressed TIFF is supported (compression tag " + std::to_string(compression) + ")");
    const auto planar = findEntry(entries, 284) ? readInts(t, *findEntry(entries, 284)).at(0) : 1;
    if (planar != 1) fail("planar TIFF is not supported");
    const auto strips = readInts(t, required(273));
    const auto counts = readInts(t, required(279));
    const auto rows_per_strip = findEntry(entries, 278) ? readInts(t, *findEntry(entries, 278)).at(0) : height;
    if (bits.empty() || bits[0] != bits.front()) fail("mixed sample bit depths are not supported");
    const auto bits_per_sample = bits.front();
    if (bits_per_sample != 8 && bits_per_sample != 16 && bits_per_sample != 32)
        fail("only 8, 16 and 32 bit TIFF samples are supported");
    if (samples == 0 || samples > 16) fail("invalid TIFF sample count");
    Raster result;
    result.metadata.width = width;
    result.metadata.height = height;
    result.metadata.bands = static_cast<std::uint16_t>(samples);
    const auto sample_format = findEntry(entries, 339) ? readInts(t, *findEntry(entries, 339)).front() : 1;
    result.metadata.pixel_type = bits_per_sample == 8 ? PixelType::UInt8 :
                                 bits_per_sample == 16 ? PixelType::UInt16 : PixelType::Float32;
    if (bits_per_sample == 32 && sample_format != 3) result.metadata.pixel_type = PixelType::UInt16;
    if (const auto* e = findEntry(entries, 270)) result.metadata.description = readAscii(t, *e);
    if (const auto* e = findEntry(entries, 42113)) result.metadata.nodata = readAscii(t, *e);
    if (const auto* e = findEntry(entries, 305)) result.metadata.software = readAscii(t, *e);
    if (const auto* e = findEntry(entries, 33550)) {
        const auto scale = readDoubles(t, *e);
        if (scale.size() >= 2) {
            result.metadata.geotransform.values[1] = scale[0];
            result.metadata.geotransform.values[5] = -std::abs(scale[1]);
        }
    }
    if (const auto* e = findEntry(entries, 33922)) {
        const auto tie = readDoubles(t, *e);
        if (tie.size() >= 6) {
            result.metadata.geotransform.values[0] = tie[3] - tie[0] * result.metadata.geotransform.values[1];
            result.metadata.geotransform.values[3] = tie[4] - tie[1] * result.metadata.geotransform.values[5];
            result.metadata.geotransform.valid = true;
        }
    }
    if (const auto* e = findEntry(entries, 34735)) {
        const auto keys = readInts(t, *e);
        if (keys.size() >= 4) {
            for (std::size_t i = 4; i + 3 < keys.size(); i += 4) {
                if ((keys[i] == 3072 || keys[i] == 2048) && keys[i + 1] == 0 && keys[i + 2] == 1) {
                    result.metadata.projection = "EPSG:" + std::to_string(keys[i + 3]);
                    break;
                }
            }
        }
    }
    result.data.assign(static_cast<std::size_t>(width) * height * samples, 0.0F);
    const std::size_t row_bytes = static_cast<std::size_t>(width) * samples * bits_per_sample / 8;
    for (std::size_t strip = 0, row = 0; strip < strips.size() && row < height; ++strip) {
        const auto rows = std::min<std::size_t>(rows_per_strip, height - row);
        const auto needed = rows * row_bytes;
        if (counts[std::min(strip, counts.size() - 1)] < needed) fail("TIFF strip is truncated");
        const auto off = strips[strip];
        if (off + needed > t.bytes.size()) fail("TIFF strip outside file");
        for (std::size_t r = 0; r < rows; ++r) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(width) * samples; ++i) {
                const auto p = off + r * row_bytes + i * bits_per_sample / 8;
                float value = 0;
                if (bits_per_sample == 8) value = static_cast<float>(t.bytes[p]);
                else if (bits_per_sample == 16) value = static_cast<float>(t.u16(p));
                else if (sample_format == 3) value = t.f32(p);
                else value = static_cast<float>(t.u32(p));
                result.data[(row + r) * static_cast<std::size_t>(width) * samples + i] = value;
            }
        }
        row += rows;
    }
    return result;
}

void put16(std::vector<std::uint8_t>& out, std::size_t p, std::uint16_t value) {
    out[p] = static_cast<std::uint8_t>(value & 255); out[p + 1] = static_cast<std::uint8_t>(value >> 8);
}
void put32(std::vector<std::uint8_t>& out, std::size_t p, std::uint32_t value) {
    out[p] = static_cast<std::uint8_t>(value & 255); out[p + 1] = static_cast<std::uint8_t>((value >> 8) & 255);
    out[p + 2] = static_cast<std::uint8_t>((value >> 16) & 255); out[p + 3] = static_cast<std::uint8_t>(value >> 24);
}
void append16(std::vector<std::uint8_t>& out, std::uint16_t value) { out.push_back(value & 255); out.push_back(value >> 8); }
void append32(std::vector<std::uint8_t>& out, std::uint32_t value) { out.push_back(value & 255); out.push_back((value >> 8) & 255); out.push_back((value >> 16) & 255); out.push_back(value >> 24); }
void append64(std::vector<std::uint8_t>& out, std::uint64_t value) { for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i))); }

struct OutEntry { std::uint16_t tag, type; std::uint32_t count; std::vector<std::uint8_t> value; std::uint32_t offset{0}; };
void addShort(std::vector<OutEntry>& e, std::uint16_t tag, std::uint16_t value) { OutEntry x{tag, 3, 1, {0,0,0,0}}; x.value[0] = value & 255; x.value[1] = value >> 8; e.push_back(std::move(x)); }
void addLong(std::vector<OutEntry>& e, std::uint16_t tag, std::uint32_t value) { OutEntry x{tag, 4, 1, {0,0,0,0}}; x.value[0] = value & 255; x.value[1] = (value >> 8) & 255; x.value[2] = (value >> 16) & 255; x.value[3] = value >> 24; e.push_back(std::move(x)); }
void addShorts(std::vector<OutEntry>& e, std::uint16_t tag, const std::vector<std::uint16_t>& vals) { OutEntry x{tag, 3, static_cast<std::uint32_t>(vals.size()), {}}; for (auto v : vals) append16(x.value, v); e.push_back(std::move(x)); }
void addDouble(std::vector<OutEntry>& e, std::uint16_t tag, const std::vector<double>& vals) { OutEntry x{tag, 12, static_cast<std::uint32_t>(vals.size()), {}}; for (auto v : vals) { std::uint64_t bits; std::memcpy(&bits, &v, sizeof bits); append64(x.value, bits); } e.push_back(std::move(x)); }
void addAscii(std::vector<OutEntry>& e, std::uint16_t tag, std::string value) { value.push_back('\0'); e.push_back({tag, 2, static_cast<std::uint32_t>(value.size()), std::vector<std::uint8_t>(value.begin(), value.end())}); }

void encode(const Raster& raster, const std::filesystem::path& path, PixelType output_type, bool overwrite) {
    if (!overwrite && std::filesystem::exists(path)) fail("output exists: " + path.string());
    const auto w = raster.metadata.width;
    const auto h = raster.metadata.height;
    const auto b = raster.metadata.bands;
    if (w == 0 || h == 0 || b == 0 || raster.data.size() < static_cast<std::size_t>(w) * h * b) fail("invalid raster dimensions");
    const auto bits = output_type == PixelType::UInt8 ? 8 : output_type == PixelType::UInt16 ? 16 : 32;
    std::vector<std::uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(w) * h * b * bits / 8);
    float minv = 0, maxv = output_type == PixelType::UInt8 ? 255.0F : output_type == PixelType::UInt16 ? 65535.0F : std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h * b; ++i) {
        float v = std::isfinite(raster.data[i]) ? raster.data[i] : 0.0F;
        if (output_type != PixelType::Float32) v = std::clamp(v, minv, maxv);
        if (output_type == PixelType::UInt8) pixels.push_back(static_cast<std::uint8_t>(std::lround(v)));
        else if (output_type == PixelType::UInt16) append16(pixels, static_cast<std::uint16_t>(std::lround(v)));
        else { std::uint32_t bits32; std::memcpy(&bits32, &v, sizeof v); append32(pixels, bits32); }
    }
    std::vector<OutEntry> entries;
    addLong(entries, 256, w); addLong(entries, 257, h);
    addShorts(entries, 258, std::vector<std::uint16_t>(b, static_cast<std::uint16_t>(bits)));
    addShort(entries, 259, 1); addShort(entries, 262, b >= 3 ? 2 : 1);  // Photometric (RGB for 3+ bands)
    addLong(entries, 273, 0); // patched after extra fields
    addShort(entries, 277, b); addLong(entries, 278, h); addLong(entries, 279, static_cast<std::uint32_t>(pixels.size()));
    addShort(entries, 284, 1); addShorts(entries, 338, {1});
    if (output_type == PixelType::Float32) addShorts(entries, 339, {3});
    if (raster.metadata.geotransform.valid) {
        addDouble(entries, 33550, {raster.metadata.geotransform.values[1], std::abs(raster.metadata.geotransform.values[5]), 0});
        addDouble(entries, 33922, {0, 0, 0, raster.metadata.geotransform.values[0], raster.metadata.geotransform.values[3], 0});
        // KeyDirectoryVersion, revision, minor revision, number of keys; one
        // projected/geographic CRS key when the metadata is an EPSG code.
        unsigned epsg = 0;
        if (raster.metadata.projection.rfind("EPSG:", 0) == 0) {
            try { epsg = static_cast<unsigned>(std::stoul(raster.metadata.projection.substr(5))); } catch (...) { epsg = 0; }
        }
        if (epsg) addShorts(entries, 34735, {1, 1, 0, 1, static_cast<std::uint16_t>(epsg >= 2000 ? 3072 : 2048), 0, 1, static_cast<std::uint16_t>(epsg)});
    }
    if (!raster.metadata.nodata.empty()) addAscii(entries, 42113, raster.metadata.nodata);
    addAscii(entries, 305, raster.metadata.software.empty() ? "RS Color Harmonizer" : raster.metadata.software);
    if (!raster.metadata.description.empty()) addAscii(entries, 270, raster.metadata.description);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.tag < b.tag; });
    std::vector<std::uint8_t> out;
    out.reserve(8 + 2 + entries.size() * 12 + 1024 + pixels.size());
    out.insert(out.end(), {'I', 'I'}); append16(out, 42); append32(out, 8);
    append16(out, static_cast<std::uint16_t>(entries.size()));
    const std::size_t entry_table_end = out.size() + entries.size() * 12 + 4;
    std::size_t extra = entry_table_end;
    for (auto& e : entries) {
        if (e.value.size() > 4) { if (extra & 1) ++extra; e.offset = static_cast<std::uint32_t>(extra); extra += e.value.size(); }
    }
    // Rebuild table now that values have offsets. Locate strip-offset entry.
    const auto pixel_offset = static_cast<std::uint32_t>((extra + 1) & ~std::size_t(1));
    for (auto& e : entries) if (e.tag == 273) { e.value = {static_cast<std::uint8_t>(pixel_offset & 255), static_cast<std::uint8_t>((pixel_offset >> 8) & 255), static_cast<std::uint8_t>((pixel_offset >> 16) & 255), static_cast<std::uint8_t>(pixel_offset >> 24)}; }
    // Entries were initially written as count=4-byte records; values with
    // offsets already have four bytes at the front and are emitted inline.
    out.resize(8 + 2);
    for (const auto& e : entries) {
        append16(out, e.tag); append16(out, e.type); append32(out, e.count);
        if (e.value.size() <= 4) {
            out.insert(out.end(), e.value.begin(), e.value.end());
            out.resize(out.size() + (4 - e.value.size()), 0);
        } else append32(out, e.offset);
    }
    append32(out, 0);
    // Append out-of-line values in exactly the same order as offset assignment.
    std::size_t cursor = entry_table_end;
    if (out.size() < cursor) out.resize(cursor, 0);
    for (const auto& e : entries) if (e.value.size() > 4) { if (cursor < e.offset) out.resize(e.offset, 0); out.insert(out.end(), e.value.begin(), e.value.end()); cursor = e.offset + e.value.size(); }
    if (out.size() < pixel_offset) out.resize(pixel_offset, 0);
    out.insert(out.end(), pixels.begin(), pixels.end());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) fail("cannot write " + path.string());
    stream.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!stream) fail("failed writing " + path.string());
}

}  // namespace

float& Raster::at(std::uint32_t x, std::uint32_t y, std::uint16_t band) {
    if (x >= metadata.width || y >= metadata.height || band >= metadata.bands) fail("raster index out of range");
    return data[(static_cast<std::size_t>(y) * metadata.width + x) * metadata.bands + band];
}
float Raster::at(std::uint32_t x, std::uint32_t y, std::uint16_t band) const {
    if (x >= metadata.width || y >= metadata.height || band >= metadata.bands) fail("raster index out of range");
    return data[(static_cast<std::size_t>(y) * metadata.width + x) * metadata.bands + band];
}

Raster readGeoTiff(const std::filesystem::path& path) {
#ifdef RSCH_WITH_GDAL
    if (auto raster = readWithGdal(path); raster.metadata.width != 0) return raster;
#endif
    return decode(path);
}
RasterMetadata readGeoTiffMetadata(const std::filesystem::path& path) { return readGeoTiff(path).metadata; }
void writeGeoTiff(const Raster& raster, const std::filesystem::path& path, PixelType output_type, bool overwrite) {
#ifdef RSCH_WITH_GDAL
    if (writeWithGdal(raster, path, output_type, overwrite)) return;
#endif
    encode(raster, path, output_type, overwrite);
}

BlockStatistics computeBlockStatistics(const Raster& raster, std::uint32_t block_width, std::uint32_t block_height, std::optional<float> nodata) {
    if (block_width == 0 || block_height == 0) fail("block dimensions must be positive");
    if (!nodata && !raster.metadata.nodata.empty()) {
        try { nodata = std::stof(raster.metadata.nodata); } catch (...) { /* preserve all pixels if the metadata is non-numeric */ }
    }
    BlockStatistics result; result.metadata = raster.metadata; result.block_width = block_width; result.block_height = block_height;
    result.bands.resize(raster.metadata.bands);
    std::vector<double> m2(raster.metadata.bands, 0.0);
    for (std::uint32_t by = 0; by < raster.metadata.height; by += block_height)
        for (std::uint32_t bx = 0; bx < raster.metadata.width; bx += block_width)
            for (std::uint32_t y = by; y < std::min(raster.metadata.height, by + block_height); ++y)
                for (std::uint32_t x = bx; x < std::min(raster.metadata.width, bx + block_width); ++x)
                    for (std::uint16_t band = 0; band < raster.metadata.bands; ++band) {
                        const double value = raster.at(x, y, band);
                        if (!std::isfinite(value) || (nodata && value == *nodata)) continue;
                        auto& s = result.bands[band]; ++s.count;
                        if (s.count == 1) s.minimum = s.maximum = s.mean = value;
                        else { s.minimum = std::min(s.minimum, value); s.maximum = std::max(s.maximum, value); const auto delta = value - s.mean; s.mean += delta / s.count; m2[band] += delta * (value - s.mean); }
                    }
    for (std::size_t i = 0; i < result.bands.size(); ++i) result.bands[i].standard_deviation = result.bands[i].count > 1 ? std::sqrt(m2[i] / (result.bands[i].count - 1)) : 0;
    return result;
}
BlockStatistics computeBlockStatistics(const std::filesystem::path& path, std::uint32_t block_width, std::uint32_t block_height, std::optional<float> nodata) { return computeBlockStatistics(readGeoTiff(path), block_width, block_height, nodata); }

Raster transferColor(const Raster& target, const Raster& reference, ColorTransferParameters parameters) {
    if (target.metadata.width != reference.metadata.width || target.metadata.height != reference.metadata.height || target.metadata.bands != reference.metadata.bands) {
        // Matching is valid for scenes with different extents as well; only the
        // per-band distributions are needed, so dimensions are intentionally not required to match.
        if (target.metadata.bands != reference.metadata.bands) fail("color transfer requires equal band counts");
    }
    const auto a = computeBlockStatistics(target).bands; const auto b = computeBlockStatistics(reference).bands;
    Raster result = target; result.data.resize(target.data.size());
    float output_max = parameters.output_max;
    // Keep the convenient uint8 default while avoiding accidental clipping
    // when callers pass uint16/float32 scenes without overriding the range.
    if (parameters.output_max == 255.0F && target.metadata.pixel_type == PixelType::UInt16) output_max = 65535.0F;
    if (target.metadata.pixel_type == PixelType::Float32 && parameters.output_max == 255.0F) output_max = std::numeric_limits<float>::max();
    for (std::uint16_t band = 0; band < target.metadata.bands; ++band) {
        const double gain = parameters.match_variance && a[band].standard_deviation > 1e-9 ? b[band].standard_deviation / a[band].standard_deviation : 1.0;
        const double full_offset = b[band].mean - a[band].mean * gain;
        const double mixed_gain = 1 + parameters.strength * (gain - 1);
        const double mixed_offset = parameters.strength * full_offset;
        for (std::size_t i = band; i < result.data.size(); i += target.metadata.bands) {
            result.data[i] = static_cast<float>(result.data[i] * mixed_gain + mixed_offset);
            result.data[i] = std::clamp(result.data[i], parameters.output_min, output_max);
        }
    }
    return result;
}

Raster equalizeLighting(const Raster& input, double strength, std::optional<float> nodata) {
    strength = std::clamp(strength, 0.0, 1.0); Raster result = input;
    const auto stats = computeBlockStatistics(input, 256, 256, nodata);
    for (std::uint16_t band = 0; band < input.metadata.bands; ++band) {
        const auto& s = stats.bands[band]; if (s.count == 0) continue;
        const double target = (s.minimum + s.maximum) * 0.5; const double offset = strength * (target - s.mean);
        for (std::size_t i = band; i < result.data.size(); i += input.metadata.bands) result.data[i] = static_cast<float>(result.data[i] + offset);
    }
    return result;
}

std::size_t chooseReferenceScene(const std::vector<Raster>& scenes) {
    if (scenes.empty()) fail("cannot choose a reference from an empty scene list");
    if (scenes.size() == 1) return 0;
    std::vector<std::vector<BandStatistics>> all; all.reserve(scenes.size());
    for (const auto& scene : scenes) all.push_back(computeBlockStatistics(scene).bands);
    std::size_t best = 0; double best_score = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < scenes.size(); ++i) { double score = 0; for (std::size_t j = 0; j < scenes.size(); ++j) if (i != j) for (std::size_t b = 0; b < std::min(all[i].size(), all[j].size()); ++b) score += std::abs(all[i][b].mean - all[j][b].mean); if (score < best_score) { best_score = score; best = i; } }
    return best;
}

std::vector<Raster> harmonizeScenes(const std::vector<Raster>& scenes, std::optional<std::size_t> reference_index, ColorTransferParameters parameters, HarmonizationReport* report) {
    if (scenes.empty()) return {};
    const auto ref = reference_index.value_or(chooseReferenceScene(scenes)); if (ref >= scenes.size()) fail("reference index out of range");
    std::vector<Raster> result; result.reserve(scenes.size()); for (std::size_t i = 0; i < scenes.size(); ++i) result.push_back(i == ref ? scenes[i] : transferColor(scenes[i], scenes[ref], parameters));
    if (report) { report->reference_index = ref; report->reference_statistics = computeBlockStatistics(scenes[ref]).bands; report->input_statistics.clear(); for (const auto& s : scenes) report->input_statistics.push_back(computeBlockStatistics(s).bands); report->applied_gain_offset.clear(); for (std::size_t i = 0; i < scenes.size(); ++i) { if (i == ref) { report->applied_gain_offset.push_back({1, 0}); continue; } const auto a = report->input_statistics[i][0]; const auto b = report->reference_statistics[0]; const auto gain = parameters.match_variance && a.standard_deviation > 1e-9 ? b.standard_deviation / a.standard_deviation : 1; report->applied_gain_offset.push_back({gain, b.mean - a.mean * gain}); } }
    return result;
}

std::string pixelTypeName(PixelType type) { switch (type) { case PixelType::UInt8: return "uint8"; case PixelType::UInt16: return "uint16"; case PixelType::Float32: return "float32"; } return "unknown"; }
PixelType pixelTypeFromName(const std::string& name) { if (name == "uint8" || name == "byte") return PixelType::UInt8; if (name == "uint16") return PixelType::UInt16; if (name == "float32" || name == "float") return PixelType::Float32; fail("unknown pixel type: " + name); }

}  // namespace rsch
