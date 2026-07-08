#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
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

namespace {

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

struct PaletteEntry {
    std::uint16_t rgb555 = 0;
    std::size_t count = 0;
};

struct ConvertedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool hasTransparency = false;
    std::vector<PaletteEntry> palette;
    std::vector<std::uint8_t> indexes;
    std::vector<std::uint8_t> tileBytes;
};

constexpr const char *Reset = "\x1b[0m";
constexpr const char *Bold = "\x1b[1m";
constexpr const char *Dim = "\x1b[2m";
constexpr const char *Red = "\x1b[31m";
constexpr const char *Green = "\x1b[32m";
constexpr const char *Yellow = "\x1b[33m";
constexpr const char *Blue = "\x1b[34m";
constexpr const char *Cyan = "\x1b[36m";

std::uint32_t readBe32(const std::vector<std::uint8_t> &bytes,
                       std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("Unexpected end of PNG data");
    }
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint8_t paeth(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
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

std::vector<std::uint8_t> readFile(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Could not open input file: " + path.string());
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> inflate(const std::vector<std::uint8_t> &input,
                                  std::size_t expectedSize) {
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

Image loadPng(const std::filesystem::path &path) {
    std::vector<std::uint8_t> bytes = readFile(path);
    const std::array<std::uint8_t, 8> signature{0x89, 'P',  'N',  'G',
                                                0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() < signature.size() ||
        !std::equal(signature.begin(), signature.end(), bytes.begin())) {
        throw std::runtime_error("Input is not a PNG file");
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
                throw std::runtime_error("Unsupported PNG compression or filter method");
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
        throw std::runtime_error("Only RGB, indexed, and RGBA PNG files are supported");
    }

    std::size_t rowBytes = static_cast<std::size_t>(image.width) * channels;
    std::size_t expectedSize = (rowBytes + 1) * image.height;
    std::vector<std::uint8_t> inflated = inflate(idat, expectedSize);
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
                    value + static_cast<std::uint8_t>((static_cast<int>(left) + static_cast<int>(up)) / 2));
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
                    throw std::runtime_error("Indexed PNG references a missing palette entry");
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

std::uint16_t toRgb555(Rgba color) {
    std::uint16_t r = static_cast<std::uint16_t>((static_cast<int>(color.r) * 31 + 127) / 255);
    std::uint16_t g = static_cast<std::uint16_t>((static_cast<int>(color.g) * 31 + 127) / 255);
    std::uint16_t b = static_cast<std::uint16_t>((static_cast<int>(color.b) * 31 + 127) / 255);
    return static_cast<std::uint16_t>((r << 10) | (g << 5) | b);
}

Rgba fromRgb555(std::uint16_t value) {
    std::uint8_t b = static_cast<std::uint8_t>((value & 0x001F) << 3);
    std::uint8_t g = static_cast<std::uint8_t>(((value >> 5) & 0x001F) << 3);
    std::uint8_t r = static_cast<std::uint8_t>(((value >> 10) & 0x001F) << 3);
    r = static_cast<std::uint8_t>(r | (r >> 5));
    g = static_cast<std::uint8_t>(g | (g >> 5));
    b = static_cast<std::uint8_t>(b | (b >> 5));
    return {r, g, b, 255};
}

ConvertedImage convert(const Image &image) {
    if (image.width != image.height ||
        (image.width != 8 && image.width != 16 && image.width != 32)) {
        throw std::runtime_error("Image must be exactly 8x8, 16x16, or 32x32");
    }

    ConvertedImage result;
    result.width = image.width;
    result.height = image.height;
    result.indexes.resize(image.pixels.size());
    std::map<std::uint16_t, std::uint8_t> indexByColor;

    for (const Rgba &pixel : image.pixels) {
        if (pixel.a < 128) {
            result.hasTransparency = true;
            break;
        }
    }

    if (result.hasTransparency) {
        result.palette.push_back({0, 0});
    }

    for (std::size_t i = 0; i < image.pixels.size(); i++) {
        const Rgba &pixel = image.pixels[i];
        if (pixel.a < 128) {
            result.indexes[i] = 0;
            result.palette[0].count++;
            continue;
        }

        std::uint16_t rgb555 = toRgb555(pixel);
        auto found = indexByColor.find(rgb555);
        if (found == indexByColor.end()) {
            if (result.palette.size() >= 16) {
                throw std::runtime_error("Image uses more than 16 colors after RGB555 conversion");
            }
            std::uint8_t newIndex = static_cast<std::uint8_t>(result.palette.size());
            indexByColor[rgb555] = newIndex;
            result.palette.push_back({rgb555, 0});
            found = indexByColor.find(rgb555);
        }
        result.indexes[i] = found->second;
        result.palette[found->second].count++;
    }

    for (std::uint32_t tileY = 0; tileY < image.height; tileY += 8) {
        for (std::uint32_t tileX = 0; tileX < image.width; tileX += 8) {
            for (std::uint32_t y = 0; y < 8; y++) {
                for (std::uint32_t x = 0; x < 8; x += 2) {
                    std::uint8_t left = result.indexes[(tileY + y) * image.width + tileX + x];
                    std::uint8_t right = result.indexes[(tileY + y) * image.width + tileX + x + 1];
                    result.tileBytes.push_back(static_cast<std::uint8_t>((left << 4) | right));
                }
            }
        }
    }

    return result;
}

std::string hexValue(std::uint32_t value, int width) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(width)
           << std::setfill('0') << value;
    return stream.str();
}

std::string swatch(std::uint16_t rgb555) {
    Rgba color = fromRgb555(rgb555);
    return "\x1b[48;2;" + std::to_string(color.r) + ";" +
           std::to_string(color.g) + ";" + std::to_string(color.b) + "m  " +
           Reset;
}

std::string indexedCell(std::uint8_t index, std::uint16_t rgb555) {
    Rgba color = fromRgb555(rgb555);
    int brightness = (static_cast<int>(color.r) * 299 +
                      static_cast<int>(color.g) * 587 +
                      static_cast<int>(color.b) * 114) /
                     1000;
    const char *foreground = brightness > 130 ? "\x1b[38;2;0;0;0m"
                                              : "\x1b[38;2;255;255;255m";
    std::ostringstream stream;
    stream << "\x1b[48;2;" << static_cast<int>(color.r) << ";"
           << static_cast<int>(color.g) << ";" << static_cast<int>(color.b)
           << "m" << foreground << std::uppercase << std::hex
           << static_cast<int>(index) << Reset;
    return stream.str();
}

void printReport(const ConvertedImage &image,
                 const std::filesystem::path &inputPath,
                 const std::filesystem::path &outputPath) {
    std::cout << "\n" << Bold << Cyan << "E16 image converter" << Reset << "\n";
    std::cout << Dim << "Input  " << Reset << inputPath.string() << "\n";
    std::cout << Dim << "Output " << Reset << outputPath.string() << "\n\n";

    std::cout << Bold << Blue << "Asset" << Reset << "\n";
    std::cout << "  Size        " << Bold << image.width << "x" << image.height << Reset << "\n";
    std::cout << "  Palette     " << Bold << image.palette.size() << "/16" << Reset << " colors\n";
    std::cout << "  Tile bytes  " << Bold << image.tileBytes.size() << Reset << "\n";
    std::cout << "  Format      " << Bold << "raw RGB555 palette + 4bpp tiles" << Reset << "\n";
    std::cout << "  Alpha       "
              << (image.hasTransparency ? std::string(Green) + "index 0 transparent" + Reset
                                         : std::string(Yellow) + "not present" + Reset)
              << "\n\n";

    std::cout << Bold << Blue << "Palette" << Reset << "\n";
    for (std::size_t i = 0; i < image.palette.size(); i++) {
        Rgba color = fromRgb555(image.palette[i].rgb555);
        std::cout << "  " << Bold << std::uppercase << std::hex << i << std::dec
                  << Reset << "  " << swatch(image.palette[i].rgb555) << "  "
                  << hexValue(image.palette[i].rgb555, 4) << "  "
                  << "rgb(" << static_cast<int>(color.r) << ", "
                  << static_cast<int>(color.g) << ", "
                  << static_cast<int>(color.b) << ")  "
                  << Dim << image.palette[i].count << " px" << Reset << "\n";
    }

    std::cout << "\n" << Bold << Blue << "Indexes" << Reset << "\n";
    for (std::uint32_t y = 0; y < image.height; y++) {
        std::cout << "  " << Dim << std::setw(2) << std::setfill('0') << y
                  << std::setfill(' ') << Reset << "  ";
        for (std::uint32_t x = 0; x < image.width; x++) {
            if (x > 0 && x % 8 == 0) {
                std::cout << " ";
            }
            std::uint8_t index = image.indexes[y * image.width + x];
            std::cout << indexedCell(index, image.palette[index].rgb555);
        }
        std::cout << "\n";
        if ((y + 1) % 8 == 0 && y + 1 < image.height) {
            std::cout << "\n";
        }
    }
    std::cout << "\n";
}

void writeU16(std::ofstream &stream, std::uint16_t value) {
    stream.put(static_cast<char>(value & 0xFF));
    stream.put(static_cast<char>((value >> 8) & 0xFF));
}

void writeRawImage(const ConvertedImage &image,
                   const std::filesystem::path &outputPath) {
    std::ofstream stream(outputPath, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Could not open output file: " + outputPath.string());
    }

    for (const PaletteEntry &entry : image.palette) {
        writeU16(stream, entry.rgb555);
    }
    stream.write(reinterpret_cast<const char *>(image.tileBytes.data()),
                 static_cast<std::streamsize>(image.tileBytes.size()));

    if (!stream) {
        throw std::runtime_error("Could not finish writing output file");
    }
}

std::filesystem::path defaultOutputPath(const std::filesystem::path &inputPath) {
    std::filesystem::path output = inputPath;
    output.replace_extension(".e16img");
    return output;
}

void printUsage(const char *program) {
    std::cout << Bold << "Usage" << Reset << "\n";
    std::cout << "  " << program << " <image.png> [-o output.e16img]\n\n";
    std::cout << "Accepts 8x8, 16x16, and 32x32 PNG files with up to 16 RGB555 colors.\n";
}

int run(int argc, char **argv) {
    if (argc == 1) {
        printUsage(argv[0]);
        return 1;
    }

    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value after " + arg);
            }
            outputPath = argv[++i];
            continue;
        }
        if (!inputPath.empty()) {
            throw std::runtime_error("Only one input PNG can be converted at a time");
        }
        inputPath = arg;
    }

    if (inputPath.empty()) {
        throw std::runtime_error("Missing input PNG path");
    }
    if (outputPath.empty()) {
        outputPath = defaultOutputPath(inputPath);
    }

    Image image = loadPng(inputPath);
    ConvertedImage converted = convert(image);
    writeRawImage(converted, outputPath);
    printReport(converted, inputPath, outputPath);
    return 0;
}

}

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << Bold << Red << "error:" << Reset << " " << error.what()
                  << "\n";
        return 1;
    }
}
