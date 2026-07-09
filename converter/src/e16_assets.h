#ifndef E16_ASSETS_H
#define E16_ASSETS_H

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <zlib.h>

namespace e16asset {

struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<Rgba> pixels;
};

constexpr const char *Reset = "\x1b[0m";
constexpr const char *Bold = "\x1b[1m";
constexpr const char *Dim = "\x1b[2m";
constexpr const char *Red = "\x1b[31m";
constexpr const char *Green = "\x1b[32m";
constexpr const char *Yellow = "\x1b[33m";
constexpr const char *Blue = "\x1b[34m";
constexpr const char *Cyan = "\x1b[36m";

inline std::uint32_t readBe32(const std::vector<std::uint8_t> &bytes,
                              std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("Unexpected end of PNG data");
    }
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

inline std::uint8_t paeth(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
    int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
    int pa = std::abs(p - static_cast<int>(a));
    int pb = std::abs(p - static_cast<int>(b));
    int pc = std::abs(p - static_cast<int>(c));
    if (pa <= pb && pa <= pc) {
        return a;
    }
    if (pb <= pc) {
        return b;
    }
    return c;
}

inline std::vector<std::uint8_t> readFile(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Could not open input file: " + path.string());
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

inline std::vector<std::uint8_t> inflateBytes(
    const std::vector<std::uint8_t> &input, std::size_t expectedSize) {
    std::vector<std::uint8_t> output(expectedSize);
    z_stream stream{};
    stream.next_in = const_cast<Bytef *>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    int init = inflateInit(&stream);
    if (init != Z_OK) {
        throw std::runtime_error("Could not initialize zlib");
    }
    int result = ::inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END || stream.total_out != expectedSize) {
        throw std::runtime_error("PNG image data did not inflate correctly");
    }
    return output;
}

inline Image loadPng(const std::filesystem::path &path) {
    std::vector<std::uint8_t> bytes = readFile(path);
    const std::array<std::uint8_t, 8> signature{0x89, 'P',  'N',  'G',
                                                0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() < signature.size() ||
        !std::equal(signature.begin(), signature.end(), bytes.begin())) {
        throw std::runtime_error("Input is not a PNG file: " + path.string());
    }

    Image image;
    std::uint8_t bitDepth = 0;
    std::uint8_t colorType = 0;
    std::uint8_t interlace = 0;
    std::vector<std::uint8_t> idat;
    std::vector<Rgba> pngPalette;
    std::vector<std::uint8_t> alphaPalette;

    std::size_t offset = 8;
    while (offset + 12 <= bytes.size()) {
        std::uint32_t length = readBe32(bytes, offset);
        offset += 4;
        if (offset + 4 + length + 4 > bytes.size()) {
            throw std::runtime_error("PNG chunk extends past end of file");
        }
        std::string type(reinterpret_cast<const char *>(&bytes[offset]), 4);
        offset += 4;
        const std::uint8_t *data = bytes.data() + offset;

        if (type == "IHDR") {
            if (length != 13) {
                throw std::runtime_error("Invalid IHDR chunk");
            }
            image.width = readBe32(bytes, offset);
            image.height = readBe32(bytes, offset + 4);
            bitDepth = data[8];
            colorType = data[9];
            if (data[10] != 0 || data[11] != 0) {
                throw std::runtime_error(
                    "Unsupported PNG compression or filter method");
            }
            interlace = data[12];
        } else if (type == "PLTE") {
            if (length % 3 != 0) {
                throw std::runtime_error("Invalid PLTE chunk");
            }
            pngPalette.clear();
            for (std::size_t i = 0; i < length; i += 3) {
                pngPalette.push_back({data[i], data[i + 1], data[i + 2], 255});
            }
        } else if (type == "tRNS") {
            alphaPalette.assign(data, data + length);
        } else if (type == "IDAT") {
            idat.insert(idat.end(), data, data + length);
        } else if (type == "IEND") {
            break;
        }

        offset += length + 4;
    }

    if (image.width == 0 || image.height == 0) {
        throw std::runtime_error("PNG is missing a valid IHDR chunk");
    }
    if (bitDepth != 8) {
        throw std::runtime_error("Only 8-bit PNG files are supported");
    }
    if (interlace != 0) {
        throw std::runtime_error("Interlaced PNG files are not supported");
    }
    if (idat.empty()) {
        throw std::runtime_error("PNG has no image data");
    }

    std::size_t channels = 0;
    if (colorType == 2) {
        channels = 3;
    } else if (colorType == 3) {
        channels = 1;
    } else if (colorType == 6) {
        channels = 4;
    } else {
        throw std::runtime_error(
            "Only RGB, indexed, and RGBA PNG files are supported");
    }

    std::size_t rowBytes = static_cast<std::size_t>(image.width) * channels;
    std::size_t expectedSize = (rowBytes + 1) * image.height;
    std::vector<std::uint8_t> inflated = inflateBytes(idat, expectedSize);
    std::vector<std::uint8_t> raw(rowBytes * image.height);
    std::vector<std::uint8_t> previous(rowBytes);

    for (std::uint32_t y = 0; y < image.height; y++) {
        std::size_t sourceOffset = static_cast<std::size_t>(y) * (rowBytes + 1);
        std::size_t targetOffset = static_cast<std::size_t>(y) * rowBytes;
        std::uint8_t filter = inflated[sourceOffset];
        for (std::size_t x = 0; x < rowBytes; x++) {
            std::uint8_t value = inflated[sourceOffset + 1 + x];
            std::uint8_t left = x >= channels ? raw[targetOffset + x - channels] : 0;
            std::uint8_t up = previous[x];
            std::uint8_t upperLeft = x >= channels ? previous[x - channels] : 0;
            switch (filter) {
            case 0:
                break;
            case 1:
                value = static_cast<std::uint8_t>(value + left);
                break;
            case 2:
                value = static_cast<std::uint8_t>(value + up);
                break;
            case 3:
                value = static_cast<std::uint8_t>(
                    value + static_cast<std::uint8_t>(
                                (static_cast<int>(left) + static_cast<int>(up)) / 2));
                break;
            case 4:
                value = static_cast<std::uint8_t>(value + paeth(left, up, upperLeft));
                break;
            default:
                throw std::runtime_error("Unsupported PNG row filter");
            }
            raw[targetOffset + x] = value;
        }
        std::copy(raw.begin() + static_cast<std::ptrdiff_t>(targetOffset),
                  raw.begin() + static_cast<std::ptrdiff_t>(targetOffset + rowBytes),
                  previous.begin());
    }

    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
    for (std::uint32_t y = 0; y < image.height; y++) {
        for (std::uint32_t x = 0; x < image.width; x++) {
            std::size_t pixelIndex = static_cast<std::size_t>(y) * image.width + x;
            std::size_t rawIndex = static_cast<std::size_t>(y) * rowBytes + x * channels;
            if (colorType == 2) {
                image.pixels[pixelIndex] = {raw[rawIndex], raw[rawIndex + 1],
                                            raw[rawIndex + 2], 255};
            } else if (colorType == 3) {
                std::uint8_t paletteIndex = raw[rawIndex];
                if (paletteIndex >= pngPalette.size()) {
                    throw std::runtime_error(
                        "Indexed PNG references a missing palette entry");
                }
                Rgba color = pngPalette[paletteIndex];
                if (paletteIndex < alphaPalette.size()) {
                    color.a = alphaPalette[paletteIndex];
                }
                image.pixels[pixelIndex] = color;
            } else {
                image.pixels[pixelIndex] = {raw[rawIndex], raw[rawIndex + 1],
                                            raw[rawIndex + 2], raw[rawIndex + 3]};
            }
        }
    }

    return image;
}

inline std::uint16_t toRgb555(Rgba color) {
    std::uint16_t r =
        static_cast<std::uint16_t>((static_cast<int>(color.r) * 31 + 127) / 255);
    std::uint16_t g =
        static_cast<std::uint16_t>((static_cast<int>(color.g) * 31 + 127) / 255);
    std::uint16_t b =
        static_cast<std::uint16_t>((static_cast<int>(color.b) * 31 + 127) / 255);
    return static_cast<std::uint16_t>((r << 10) | (g << 5) | b);
}

inline bool hasTransparency(const Image &image) {
    return std::any_of(image.pixels.begin(), image.pixels.end(),
                       [](Rgba color) { return color.a < 128; });
}

inline void requireSpriteSize(const Image &image) {
    if (image.width != image.height ||
        (image.width != 8 && image.width != 16 && image.width != 32)) {
        throw std::runtime_error("Sprites must be exactly 8x8, 16x16, or 32x32");
    }
}

inline void addPaletteColor(std::vector<std::uint16_t> &palette,
                            std::map<std::uint16_t, std::uint8_t> &indexByColor,
                            std::uint16_t rgb555) {
    if (indexByColor.find(rgb555) != indexByColor.end()) {
        return;
    }
    if (palette.size() >= 16) {
        throw std::runtime_error("Asset uses more than 16 colors after RGB555 conversion");
    }
    std::uint8_t index = static_cast<std::uint8_t>(palette.size());
    palette.push_back(rgb555);
    indexByColor[rgb555] = index;
}

inline std::vector<std::uint8_t> indexImage(
    const Image &image, std::vector<std::uint16_t> &palette,
    std::map<std::uint16_t, std::uint8_t> &indexByColor, bool reserveTransparent) {
    if (reserveTransparent && palette.empty()) {
        palette.push_back(0);
    }
    std::vector<std::uint8_t> indexes(image.pixels.size());
    for (std::size_t i = 0; i < image.pixels.size(); i++) {
        const Rgba &pixel = image.pixels[i];
        if (pixel.a < 128) {
            indexes[i] = 0;
            continue;
        }
        std::uint16_t rgb555 = toRgb555(pixel);
        addPaletteColor(palette, indexByColor, rgb555);
        indexes[i] = indexByColor[rgb555];
    }
    return indexes;
}

inline std::vector<std::uint8_t> packTiles(const std::vector<std::uint8_t> &indexes,
                                           std::uint32_t width,
                                           std::uint32_t height) {
    std::vector<std::uint8_t> tileBytes;
    for (std::uint32_t tileY = 0; tileY < height; tileY += 8) {
        for (std::uint32_t tileX = 0; tileX < width; tileX += 8) {
            for (std::uint32_t y = 0; y < 8; y++) {
                for (std::uint32_t x = 0; x < 8; x += 2) {
                    std::uint8_t left = indexes[(tileY + y) * width + tileX + x];
                    std::uint8_t right = indexes[(tileY + y) * width + tileX + x + 1];
                    tileBytes.push_back(static_cast<std::uint8_t>((left << 4) | right));
                }
            }
        }
    }
    return tileBytes;
}

inline std::vector<std::uint8_t> packTile(const std::vector<std::uint8_t> &tile) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(32);
    for (std::size_t y = 0; y < 8; y++) {
        for (std::size_t x = 0; x < 8; x += 2) {
            std::uint8_t left = tile[y * 8 + x];
            std::uint8_t right = tile[y * 8 + x + 1];
            bytes.push_back(static_cast<std::uint8_t>((left << 4) | right));
        }
    }
    return bytes;
}

inline std::vector<std::uint8_t> flipTile(const std::vector<std::uint8_t> &tile,
                                          bool flipX, bool flipY) {
    std::vector<std::uint8_t> flipped(64);
    for (std::size_t y = 0; y < 8; y++) {
        for (std::size_t x = 0; x < 8; x++) {
            std::size_t sourceX = flipX ? 7 - x : x;
            std::size_t sourceY = flipY ? 7 - y : y;
            flipped[y * 8 + x] = tile[sourceY * 8 + sourceX];
        }
    }
    return flipped;
}

inline void writeU16(std::ofstream &stream, std::uint16_t value) {
    stream.put(static_cast<char>(value & 0xFF));
    stream.put(static_cast<char>((value >> 8) & 0xFF));
}

inline void writePaddedPalette(std::ofstream &stream,
                               const std::vector<std::uint16_t> &palette) {
    for (std::size_t i = 0; i < 16; i++) {
        std::uint16_t value = i < palette.size() ? palette[i] : 0;
        writeU16(stream, value);
    }
}

inline std::string hexValue(std::uint32_t value, int width) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(width)
           << std::setfill('0') << value;
    return stream.str();
}

inline std::string symbolName(const std::string &text) {
    std::string symbol;
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            symbol.push_back(static_cast<char>(std::toupper(ch)));
        } else if (!symbol.empty() && symbol.back() != '_') {
            symbol.push_back('_');
        }
    }
    while (!symbol.empty() && symbol.back() == '_') {
        symbol.pop_back();
    }
    if (symbol.empty() || std::isdigit(static_cast<unsigned char>(symbol.front()))) {
        symbol.insert(symbol.begin(), 'A');
    }
    return symbol;
}

inline std::string assetSymbol(const std::filesystem::path &path) {
    return symbolName(path.stem().string());
}

inline std::filesystem::path replaceExtension(const std::filesystem::path &path,
                                              const std::string &extension) {
    std::filesystem::path output = path;
    output.replace_extension(extension);
    return output;
}

}

#endif
