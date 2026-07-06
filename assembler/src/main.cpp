
#include "parser.h"

#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cout << "Usage: [file.e16]" << std::endl;
    }
    std::ifstream file(argv[1]);

    if (!file) {
        throw std::runtime_error("Could not open file " + std::string(argv[1]));
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    Parser parser(buffer.str());
    parser.parse();
    parser.verifyIntegrity();
    parser.printExpressions();

    return 0;
}