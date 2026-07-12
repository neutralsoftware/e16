
#include "e16/emulator.h"

#include <exception>
#include <iostream>
#include <stdexcept>

namespace {

void usage() {
    std::cerr << "usage: e16emu [-s] [--headless] [--load-address addr] "
                 "[--scale n] program.bin\n";
}

}

int main(int argc, char **argv) {
    e16::EmulatorOptions options;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-s") {
            options.debug = true;
        } else if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--load-address" && i + 1 < argc) {
            options.loadAddress = e16::parseNumber(argv[++i]);
        } else if (arg == "--scale" && i + 1 < argc) {
            options.scale = std::stoi(argv[++i]);
            if (options.scale < 1) {
                throw std::invalid_argument("scale must be at least 1");
            }
        } else if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else if (options.programPath.empty()) {
            options.programPath = arg;
        } else {
            usage();
            return 1;
        }
    }

    if (options.programPath.empty()) {
        usage();
        return 1;
    }

    try {
        e16::Emulator emulator(options);
        return emulator.run();
    } catch (const std::exception &error) {
        std::cerr << "e16emu: " << error.what() << "\n";
        return 1;
    }
}
