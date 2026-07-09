#include "e16_assets.h"

#include <exception>

namespace {

struct Options {
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    std::filesystem::path includePath;
    std::string symbol;
    bool useFlips = true;
    bool wrap = false;
    std::uint8_t palette = 0;
};

struct TileMatch {
    std::uint16_t tile = 0;
    bool flipX = false;
    bool flipY = false;
};

void printUsage(const char *program) {
    std::cout << e16asset::Bold << "Usage" << e16asset::Reset << "\n";
    std::cout << "  " << program
              << " <background.png> [-o output.e16bg] [--inc output.e16] [--symbol NAME] [--palette n] [--wrap] [--no-flips]\n\n";
    std::cout << "Converts a PNG whose width and height are multiples of 8 into one 16-color palette page, deduplicated 4bpp tiles, and 2-byte E16 tilemap entries.\n";
}

std::uint8_t parsePalette(const std::string &text) {
    int value = std::stoi(text, nullptr, 0);
    if (value < 0 || value > 15) {
        throw std::runtime_error("Palette must be between 0 and 15");
    }
    return static_cast<std::uint8_t>(value);
}

Options parseArgs(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        }
        if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value after " + arg);
            }
            options.outputPath = argv[++i];
            continue;
        }
        if (arg == "--inc" || arg == "--include") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value after " + arg);
            }
            options.includePath = argv[++i];
            continue;
        }
        if (arg == "--symbol") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value after " + arg);
            }
            options.symbol = e16asset::symbolName(argv[++i]);
            continue;
        }
        if (arg == "--palette") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value after " + arg);
            }
            options.palette = parsePalette(argv[++i]);
            continue;
        }
        if (arg == "--wrap") {
            options.wrap = true;
            continue;
        }
        if (arg == "--no-flips") {
            options.useFlips = false;
            continue;
        }
        if (!options.inputPath.empty()) {
            throw std::runtime_error("Only one background PNG can be converted at a time");
        }
        options.inputPath = arg;
    }
    if (options.inputPath.empty()) {
        throw std::runtime_error("Missing background PNG path");
    }
    if (options.outputPath.empty()) {
        options.outputPath = e16asset::replaceExtension(options.inputPath, ".e16bg");
    }
    if (options.includePath.empty()) {
        options.includePath = e16asset::replaceExtension(options.outputPath, ".e16");
    }
    if (options.symbol.empty()) {
        options.symbol = e16asset::assetSymbol(options.outputPath);
    }
    return options;
}

std::vector<std::uint8_t> readTile(const std::vector<std::uint8_t> &indexes,
                                   std::uint32_t width, std::uint32_t tileX,
                                   std::uint32_t tileY) {
    std::vector<std::uint8_t> tile(64);
    for (std::uint32_t y = 0; y < 8; y++) {
        for (std::uint32_t x = 0; x < 8; x++) {
            tile[y * 8 + x] = indexes[(tileY * 8 + y) * width + tileX * 8 + x];
        }
    }
    return tile;
}

bool findTile(const std::vector<std::vector<std::uint8_t>> &tiles,
              const std::vector<std::uint8_t> &tile, bool useFlips,
              TileMatch &match) {
    for (std::size_t i = 0; i < tiles.size(); i++) {
        if (tiles[i] == tile) {
            match = {static_cast<std::uint16_t>(i), false, false};
            return true;
        }
        if (!useFlips) {
            continue;
        }
        if (tiles[i] == e16asset::flipTile(tile, true, false)) {
            match = {static_cast<std::uint16_t>(i), true, false};
            return true;
        }
        if (tiles[i] == e16asset::flipTile(tile, false, true)) {
            match = {static_cast<std::uint16_t>(i), false, true};
            return true;
        }
        if (tiles[i] == e16asset::flipTile(tile, true, true)) {
            match = {static_cast<std::uint16_t>(i), true, true};
            return true;
        }
    }
    return false;
}

std::uint16_t tilemapEntry(const TileMatch &match, std::uint8_t palette) {
    std::uint16_t entry = match.tile;
    entry |= static_cast<std::uint16_t>((palette & 0x0F) << 10);
    if (match.flipX) {
        entry |= 0x4000;
    }
    if (match.flipY) {
        entry |= 0x8000;
    }
    return entry;
}

void writeInclude(const Options &options, std::uint32_t tileBytes,
                  std::uint32_t mapBytes, std::uint32_t tileCount,
                  std::uint32_t mapWidth, std::uint32_t mapHeight,
                  bool transparent) {
    std::ofstream stream(options.includePath);
    if (!stream) {
        throw std::runtime_error("Could not open include file: " +
                                 options.includePath.string());
    }
    std::uint32_t mapOffset = 32 + tileBytes;
    std::uint16_t control = 0x0001;
    if (transparent) {
        control |= 0x0002;
    }
    if (options.wrap) {
        control |= 0x000C;
    }
    stream << ".const " << options.symbol << "_PALETTE_OFFSET, 0\n";
    stream << ".const " << options.symbol << "_PALETTE_BYTES, 32\n";
    stream << ".const " << options.symbol << "_TILES_OFFSET, 32\n";
    stream << ".const " << options.symbol << "_TILES_BYTES, " << tileBytes << "\n";
    stream << ".const " << options.symbol << "_TILE_COUNT, " << tileCount << "\n";
    stream << ".const " << options.symbol << "_MAP_OFFSET, " << mapOffset << "\n";
    stream << ".const " << options.symbol << "_MAP_BYTES, " << mapBytes << "\n";
    stream << ".const " << options.symbol << "_BYTES, "
           << 32 + tileBytes + mapBytes << "\n";
    stream << ".const " << options.symbol << "_MAP_WIDTH, " << mapWidth << "\n";
    stream << ".const " << options.symbol << "_MAP_HEIGHT, " << mapHeight << "\n";
    stream << ".const " << options.symbol << "_LAYER_CONTROL, "
           << e16asset::hexValue(control, 4) << "\n";
    stream << ".const " << options.symbol << "_PALETTE_NUMBER, "
           << static_cast<int>(options.palette) << "\n";
}

int run(int argc, char **argv) {
    Options options = parseArgs(argc, argv);
    e16asset::Image image = e16asset::loadPng(options.inputPath);
    if (image.width % 8 != 0 || image.height % 8 != 0) {
        throw std::runtime_error("Background width and height must be multiples of 8");
    }
    std::uint32_t mapWidth = image.width / 8;
    std::uint32_t mapHeight = image.height / 8;

    bool transparent = e16asset::hasTransparency(image);
    std::vector<std::uint16_t> palette;
    std::map<std::uint16_t, std::uint8_t> indexByColor;
    std::vector<std::uint8_t> indexes =
        e16asset::indexImage(image, palette, indexByColor, transparent);

    std::vector<std::vector<std::uint8_t>> tiles;
    std::vector<std::uint16_t> mapEntries;
    tiles.reserve(mapWidth * mapHeight);
    mapEntries.reserve(mapWidth * mapHeight);

    for (std::uint32_t y = 0; y < mapHeight; y++) {
        for (std::uint32_t x = 0; x < mapWidth; x++) {
            std::vector<std::uint8_t> tile = readTile(indexes, image.width, x, y);
            TileMatch match;
            if (!findTile(tiles, tile, options.useFlips, match)) {
                if (tiles.size() >= 1024) {
                    throw std::runtime_error("Tilemap uses more than 1024 unique tiles");
                }
                match = {static_cast<std::uint16_t>(tiles.size()), false, false};
                tiles.push_back(tile);
            }
            mapEntries.push_back(tilemapEntry(match, options.palette));
        }
    }

    std::vector<std::uint8_t> tileBytes;
    tileBytes.reserve(tiles.size() * 32);
    for (const auto &tile : tiles) {
        std::vector<std::uint8_t> packed = e16asset::packTile(tile);
        tileBytes.insert(tileBytes.end(), packed.begin(), packed.end());
    }

    std::ofstream stream(options.outputPath, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Could not open output file: " +
                                 options.outputPath.string());
    }
    e16asset::writePaddedPalette(stream, palette);
    stream.write(reinterpret_cast<const char *>(tileBytes.data()),
                 static_cast<std::streamsize>(tileBytes.size()));
    for (std::uint16_t entry : mapEntries) {
        e16asset::writeU16(stream, entry);
    }
    if (!stream) {
        throw std::runtime_error("Could not finish writing output file");
    }

    std::uint32_t tileBytesSize = static_cast<std::uint32_t>(tileBytes.size());
    std::uint32_t mapBytes = static_cast<std::uint32_t>(mapEntries.size() * 2);
    writeInclude(options, tileBytesSize, mapBytes,
                 static_cast<std::uint32_t>(tiles.size()), mapWidth, mapHeight,
                 transparent);

    std::cout << "\n" << e16asset::Bold << e16asset::Cyan
              << "E16 tilemap converter" << e16asset::Reset << "\n";
    std::cout << e16asset::Dim << "Input   " << e16asset::Reset
              << options.inputPath.string() << "\n";
    std::cout << e16asset::Dim << "Output  " << e16asset::Reset
              << options.outputPath.string() << "\n";
    std::cout << e16asset::Dim << "Include " << e16asset::Reset
              << options.includePath.string() << "\n\n";
    std::cout << e16asset::Bold << e16asset::Blue << "Asset"
              << e16asset::Reset << "\n";
    std::cout << "  Pixels        " << e16asset::Bold << image.width << "x"
              << image.height << e16asset::Reset << "\n";
    std::cout << "  Map           " << e16asset::Bold << mapWidth << "x"
              << mapHeight << e16asset::Reset << " tiles\n";
    std::cout << "  Palette       " << e16asset::Bold << palette.size()
              << "/16" << e16asset::Reset << " colors\n";
    std::cout << "  Unique tiles  " << e16asset::Bold << tiles.size()
              << e16asset::Reset << "\n";
    std::cout << "  Tile bytes    " << e16asset::Bold << tileBytesSize
              << e16asset::Reset << "\n";
    std::cout << "  Map bytes     " << e16asset::Bold << mapBytes
              << e16asset::Reset << "\n";
    std::cout << "  Flips         "
              << (options.useFlips ? std::string(e16asset::Green) + "enabled" +
                                         e16asset::Reset
                                   : std::string(e16asset::Yellow) + "disabled" +
                                         e16asset::Reset)
              << "\n\n";
    return 0;
}

}

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << e16asset::Bold << e16asset::Red << "error:"
                  << e16asset::Reset << " " << error.what() << "\n";
        return 1;
    }
}
