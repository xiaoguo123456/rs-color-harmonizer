#include "rsch/core.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace rsch;

namespace {
void usage(std::ostream& out) {
    out << "RS Color Harmonizer (rsch)\n\n"
        << "Usage:\n"
        << "  rsch inspect <input.tif>\n"
        << "  rsch stats <input.tif> [--block <width>x<height>]\n"
        << "  rsch match <target.tif> <reference.tif> <output.tif> [--strength 0..1] [--type uint8|uint16|float32]\n"
        << "  rsch equalize <input.tif> <output.tif> [--strength 0..1] [--type uint8|uint16|float32]\n"
        << "  rsch harmonize <output-dir> <scene1.tif> <scene2.tif> ... [--reference N] [--strength 0..1]\n";
}
double optionDouble(int& i, int argc, char** argv, const std::string& name) {
    if (i + 1 >= argc) throw std::runtime_error("missing value for " + name);
    return std::stod(argv[++i]);
}
PixelType optionType(int& i, int argc, char** argv) {
    if (i + 1 >= argc) throw std::runtime_error("missing value for --type");
    return pixelTypeFromName(argv[++i]);
}
std::pair<std::uint32_t, std::uint32_t> parseBlock(const std::string& text) {
    const auto x = text.find('x'); if (x == std::string::npos) throw std::runtime_error("block must be WIDTHxHEIGHT");
    return {static_cast<std::uint32_t>(std::stoul(text.substr(0, x))), static_cast<std::uint32_t>(std::stoul(text.substr(x + 1)))};
}
void printMetadata(const RasterMetadata& m) {
    std::cout << "width: " << m.width << "\nheight: " << m.height << "\nbands: " << m.bands
              << "\npixel_type: " << pixelTypeName(m.pixel_type) << "\n";
    if (m.geotransform.valid) { std::cout << "geotransform:"; for (double v : m.geotransform.values) std::cout << ' ' << std::setprecision(15) << v; std::cout << "\n"; }
    if (!m.projection.empty()) std::cout << "projection: " << m.projection << "\n";
    if (!m.nodata.empty()) std::cout << "nodata: " << m.nodata << "\n";
    if (!m.description.empty()) std::cout << "description: " << m.description << "\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h") { usage(std::cout); return argc < 2 ? 2 : 0; }
        const std::string command = argv[1];
        if (command == "inspect") {
            if (argc != 3) { usage(std::cerr); return 2; }
            printMetadata(readGeoTiffMetadata(argv[2])); return 0;
        }
        if (command == "stats") {
            if (argc < 3) { usage(std::cerr); return 2; }
            std::uint32_t bw = 256, bh = 256;
            for (int i = 3; i < argc; ++i) {
                if (std::string_view(argv[i]) == "--block") {
                    if (++i >= argc) throw std::runtime_error("missing value for --block");
                    auto [w, h] = parseBlock(argv[i]); bw = w; bh = h;
                } else throw std::runtime_error("unknown option: " + std::string(argv[i]));
            }
            const auto stats = computeBlockStatistics(argv[2], bw, bh);
            printMetadata(stats.metadata);
            for (std::size_t i = 0; i < stats.bands.size(); ++i) std::cout << "band " << i + 1 << ": count=" << stats.bands[i].count << " min=" << stats.bands[i].minimum << " max=" << stats.bands[i].maximum << " mean=" << stats.bands[i].mean << " stddev=" << stats.bands[i].standard_deviation << "\n";
            return 0;
        }
        if (command == "match" || command == "equalize") {
            if (argc < (command == "equalize" ? 4 : 5)) { usage(std::cerr); return 2; }
            double strength = 1.0; PixelType type = PixelType::UInt8;
            for (int i = command == "equalize" ? 4 : 5; i < argc; ++i) { if (std::string_view(argv[i]) == "--strength") strength = optionDouble(i, argc, argv, "--strength"); else if (std::string_view(argv[i]) == "--type") type = optionType(i, argc, argv); else throw std::runtime_error("unknown option: " + std::string(argv[i])); }
            const auto input = readGeoTiff(argv[2]); Raster output;
            if (command == "equalize") output = equalizeLighting(input, strength); else output = transferColor(input, readGeoTiff(argv[3]), {.strength = strength});
            writeGeoTiff(output, command == "equalize" ? argv[3] : argv[4], type); return 0;
        }
        if (command == "harmonize") {
            if (argc < 4) { usage(std::cerr); return 2; }
            const std::string output_dir = argv[2]; std::vector<std::filesystem::path> inputs; std::optional<std::size_t> reference;
            double strength = 1.0;
            for (int i = 3; i < argc; ++i) {
                if (std::string_view(argv[i]) == "--reference") { if (++i >= argc) throw std::runtime_error("missing --reference value"); reference = static_cast<std::size_t>(std::stoul(argv[i])); }
                else if (std::string_view(argv[i]) == "--strength") strength = optionDouble(i, argc, argv, "--strength");
                else inputs.emplace_back(argv[i]);
            }
            if (inputs.empty()) throw std::runtime_error("harmonize needs at least one input");
            std::filesystem::create_directories(output_dir); std::vector<Raster> scenes; for (const auto& p : inputs) scenes.push_back(readGeoTiff(p));
            HarmonizationReport report; auto outputs = harmonizeScenes(scenes, reference, {.strength = strength}, &report);
            for (std::size_t i = 0; i < outputs.size(); ++i) { const auto out = std::filesystem::path(output_dir) / (inputs[i].stem().string() + "_harmonized.tif"); writeGeoTiff(outputs[i], out); std::cout << out.string() << "\n"; }
            std::cout << "reference_index: " << report.reference_index << "\n"; return 0;
        }
        usage(std::cerr); return 2;
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; return 1; }
}
