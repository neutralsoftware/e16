
#include "e16/emulator.h"
#include "e16/log.h"

#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

void usage() {
    const std::string text =
        "usage: e16emu [-s] [-w] [--headless] [--load-address addr] "
        "[--scale n] program.bin";
    e16::logInfo(text);
    std::cerr << text << '\n';
}

}

int main(int argc, char **argv) {
    e16::initializeLog();
    std::ostringstream command;
    for (int i = 0; i < argc; i++) {
        if (i > 0) {
            command << ' ';
        }
        command << argv[i];
    }
    e16::logInfo("launch: " + command.str());
    e16::logInfo("log: " + e16::logFilePath());

    try {
        e16::EmulatorOptions options;

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "-s") {
                options.debug = true;
            } else if (arg == "-w") {
                options.windowed = true;
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
                e16::logInfo("exit code: 0");
                return 0;
            } else if (options.programPath.empty()) {
                options.programPath = arg;
            } else {
                usage();
                e16::logInfo("exit code: 1");
                return 1;
            }
        }

        if (options.programPath.empty()) {
            usage();
            e16::logInfo("exit code: 1");
            return 1;
        }

        e16::Emulator emulator(options);
        int result = emulator.run();
        e16::logInfo("exit code: " + std::to_string(result));
        return result;
    } catch (const std::exception &error) {
        e16::logError(std::string("e16emu: ") + error.what());
        e16::logInfo("exit code: 1");
        return 1;
    } catch (...) {
        e16::logError("e16emu: unknown fatal error");
        e16::logInfo("exit code: 1");
        return 1;
    }
}
