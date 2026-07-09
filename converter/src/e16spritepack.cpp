#include "e16_assets.h"

#include <exception>
#include <utility>

namespace {

struct SpriteAsset {
    std::filesystem::path path;
    e16asset::Image image;
    std::vector<std::uint8_t> indexes;
    std::vector<std::uint8_t> tileBytes;
};

struct Options {
    std::filesystem::path outputPath;
    std::filesystem::path includePath;
    bool yes = false;
    bool force = false;
    std::vector<std::filesystem::path> inputs;
};

void printUsage(const char *program) {
    std::cout << e16asset::Bold << "Usage" << e16asset::Reset << "\n";
    std::cout << "  " << program
              << " [--yes] [--force] [-o output.e16spr] [--inc output.e16] <sprite.png>...\n\n";
    std::cout << "Combines same-sized 8x8, 16x16, or 32x32 PNG sprites into one 16-color palette page plus packed 4bpp sprite data.\n";
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
        if (arg == "-y" || arg == "--yes") {
            options.yes = true;
            continue;
        }
        if (arg == "--force") {
            options.force = true;
            continue;
        }
        options.inputs.push_back(arg);
    }
    if (options.inputs.size() < 2) {
        throw std::runtime_error("Provide at least two sprite PNG files");
    }
    if (options.outputPath.empty()) {
        options.outputPath = e16asset::replaceExtension(options.inputs.front(), ".e16spr");
    }
    if (options.includePath.empty()) {
        options.includePath = e16asset::replaceExtension(options.outputPath, ".e16");
    }
    return options;
}

bool sameOpaqueMask(const e16asset::Image &a, const e16asset::Image &b) {
    if (a.width != b.width || a.height != b.height) {
        return false;
    }
    for (std::size_t i = 0; i < a.pixels.size(); i++) {
        if ((a.pixels[i].a >= 128) != (b.pixels[i].a >= 128)) {
            return false;
        }
    }
    return true;
}

void confirmCombine(const Options &options, std::size_t spriteBytes,
                    std::size_t paletteColors) {
    if (options.yes) {
        return;
    }
    std::cout << "\n" << e16asset::Bold << e16asset::Cyan
              << "E16 sprite pack" << e16asset::Reset << "\n";
    std::cout << e16asset::Dim << "Sprites " << e16asset::Reset
              << options.inputs.size() << "\n";
    std::cout << e16asset::Dim << "Palette " << e16asset::Reset
              << paletteColors << "/16 colors\n";
    std::cout << e16asset::Dim << "Bytes   " << e16asset::Reset
              << "32 palette + " << spriteBytes << " sprites\n\n";
    std::cout << "Combine these sprites into one shared palette? [y/N] ";
    std::string answer;
    std::getline(std::cin, answer);
    if (answer != "y" && answer != "Y" && answer != "yes" && answer != "YES") {
        throw std::runtime_error("Canceled");
    }
}

void writeInclude(const Options &options, const std::string &prefix,
                  std::uint32_t spriteBytes,
                  const std::vector<SpriteAsset> &sprites) {
    std::ofstream stream(options.includePath);
    if (!stream) {
        throw std::runtime_error("Could not open include file: " +
                                 options.includePath.string());
    }
    stream << ".const " << prefix << "_PALETTE_OFFSET, 0\n";
    stream << ".const " << prefix << "_PALETTE_BYTES, 32\n";
    stream << ".const " << prefix << "_SPRITES_OFFSET, 32\n";
    stream << ".const " << prefix << "_SPRITE_BYTES, " << spriteBytes << "\n";
    stream << ".const " << prefix << "_SPRITE_COUNT, " << sprites.size() << "\n";
    stream << ".const " << prefix << "_BYTES, "
           << 32 + spriteBytes * sprites.size() << "\n";
    for (std::size_t i = 0; i < sprites.size(); i++) {
        std::string name = e16asset::assetSymbol(sprites[i].path);
        std::uint32_t offset = 32 + static_cast<std::uint32_t>(i * spriteBytes);
        stream << ".const " << prefix << "_" << name << "_OFFSET, " << offset << "\n";
        stream << ".const " << prefix << "_" << name << "_TILE_INDEX, "
               << (i * spriteBytes) / 32 << "\n";
    }
}

int run(int argc, char **argv) {
    Options options = parseArgs(argc, argv);
    std::vector<SpriteAsset> sprites;
    sprites.reserve(options.inputs.size());

    bool reserveTransparent = false;
    for (const auto &path : options.inputs) {
        SpriteAsset sprite;
        sprite.path = path;
        sprite.image = e16asset::loadPng(path);
        e16asset::requireSpriteSize(sprite.image);
        reserveTransparent = reserveTransparent || e16asset::hasTransparency(sprite.image);
        sprites.push_back(std::move(sprite));
    }

    const e16asset::Image &first = sprites.front().image;
    for (std::size_t i = 1; i < sprites.size(); i++) {
        if (sprites[i].image.width != first.width ||
            sprites[i].image.height != first.height) {
            throw std::runtime_error("All sprites must have the same dimensions");
        }
        if (!options.force && !sameOpaqueMask(first, sprites[i].image)) {
            throw std::runtime_error(
                "Sprites do not share the same opaque/transparent shape; pass --force to combine anyway");
        }
    }

    std::vector<std::uint16_t> palette;
    std::map<std::uint16_t, std::uint8_t> indexByColor;
    for (auto &sprite : sprites) {
        sprite.indexes = e16asset::indexImage(sprite.image, palette, indexByColor,
                                              reserveTransparent);
        sprite.tileBytes =
            e16asset::packTiles(sprite.indexes, sprite.image.width, sprite.image.height);
    }

    std::uint32_t spriteBytes = static_cast<std::uint32_t>(sprites.front().tileBytes.size());
    confirmCombine(options, spriteBytes * sprites.size(), palette.size());

    std::ofstream stream(options.outputPath, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Could not open output file: " +
                                 options.outputPath.string());
    }
    e16asset::writePaddedPalette(stream, palette);
    for (const auto &sprite : sprites) {
        stream.write(reinterpret_cast<const char *>(sprite.tileBytes.data()),
                     static_cast<std::streamsize>(sprite.tileBytes.size()));
    }
    if (!stream) {
        throw std::runtime_error("Could not finish writing output file");
    }

    std::string prefix = e16asset::assetSymbol(options.outputPath);
    writeInclude(options, prefix, spriteBytes, sprites);

    std::cout << "\n" << e16asset::Bold << e16asset::Cyan
              << "E16 sprite pack" << e16asset::Reset << "\n";
    std::cout << e16asset::Dim << "Output  " << e16asset::Reset
              << options.outputPath.string() << "\n";
    std::cout << e16asset::Dim << "Include " << e16asset::Reset
              << options.includePath.string() << "\n\n";
    std::cout << e16asset::Bold << e16asset::Blue << "Asset"
              << e16asset::Reset << "\n";
    std::cout << "  Size          " << e16asset::Bold << first.width << "x"
              << first.height << e16asset::Reset << "\n";
    std::cout << "  Sprites       " << e16asset::Bold << sprites.size()
              << e16asset::Reset << "\n";
    std::cout << "  Palette       " << e16asset::Bold << palette.size()
              << "/16" << e16asset::Reset << " colors\n";
    std::cout << "  Palette bytes " << e16asset::Bold << 32 << e16asset::Reset
              << "\n";
    std::cout << "  Sprite bytes  " << e16asset::Bold << spriteBytes
              << e16asset::Reset << " each\n";
    std::cout << "  Shape check   "
              << (options.force ? std::string(e16asset::Yellow) + "forced" +
                                      e16asset::Reset
                                : std::string(e16asset::Green) + "matched" +
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
