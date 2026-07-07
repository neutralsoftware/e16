
#include "compiler.h"
#include "parser.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace {
void printUsage() {
    std::cout << "Usage: e16asm [--base address] [-o output.bin] [--print-ast] "
                 "file.e16"
              << std::endl;
}

std::string defaultOutputPath(const std::string &inputPath) {
    size_t slash = inputPath.find_last_of("/\\");
    size_t dot = inputPath.find_last_of('.');

    if (dot != std::string::npos &&
        (slash == std::string::npos || dot > slash)) {
        return inputPath.substr(0, dot) + ".bin";
    }

    return inputPath + ".bin";
}
}

int main(int argc, char **argv) {
    std::string inputPath;
    std::string outputPath;
    std::uint32_t baseAddress = 0x200000;
    bool printAst = false;

    for (int i = 1; i < argc; i++) {
        std::string argument = argv[i];

        if (argument == "--help" || argument == "-h") {
            printUsage();
            return 0;
        }

        if (argument == "-o" || argument == "--output") {
            if (i + 1 >= argc) {
                std::cerr << argument << " expects a file path." << std::endl;
                return 1;
            }
            outputPath = argv[++i];
            continue;
        }

        if (argument == "-b" || argument == "--base") {
            if (i + 1 >= argc) {
                std::cerr << argument << " expects an address." << std::endl;
                return 1;
            }

            try {
                int parsed = utils::parseNumber(argv[++i]);
                if (parsed < 0 || parsed > 0xFFFFFF) {
                    std::cerr << "Base address must fit in 24 bits."
                              << std::endl;
                    return 1;
                }
                baseAddress = static_cast<std::uint32_t>(parsed);
            } catch (...) {
                std::cerr << "Could not parse base address." << std::endl;
                return 1;
            }
            continue;
        }

        if (argument == "--print-ast") {
            printAst = true;
            continue;
        }

        if (!argument.empty() && argument[0] == '-') {
            std::cerr << "Unknown option " << argument << "." << std::endl;
            return 1;
        }

        if (!inputPath.empty()) {
            std::cerr << "Only one input file can be provided." << std::endl;
            return 1;
        }

        inputPath = argument;
    }

    if (inputPath.empty()) {
        printUsage();
        return 1;
    }

    if (outputPath.empty()) {
        outputPath = defaultOutputPath(inputPath);
    }

    std::ifstream file(inputPath);

    if (!file) {
        std::cerr << "Could not open file " << inputPath << "." << std::endl;
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    try {
        Parser parser(buffer.str(), inputPath);
        parser.parse();
        parser.verifyIntegrity();
        parser.parseAddressingModes();

        if (printAst) {
            parser.printExpressions();
        }

        Compiler compiler(parser, baseAddress);
        std::vector<std::uint8_t> bytes = compiler.compile();
        std::ofstream output(outputPath, std::ios::binary);

        if (!output) {
            std::cerr << "Could not open output file " << outputPath << "."
                      << std::endl;
            return 1;
        }

        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        std::cout << "Wrote " << bytes.size() << " bytes to " << outputPath
                  << std::endl;
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }

    return 0;
}
