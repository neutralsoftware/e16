#include "e16/common.h"

#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace e16 {

std::uint32_t parseNumber(const std::string &text) {
    std::string value;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            value.push_back(c);
        }
    }

    if (value.empty()) {
        throw std::runtime_error("empty number");
    }

    int sign = 1;
    if (value.front() == '+' || value.front() == '-') {
        if (value.front() == '-') {
            sign = -1;
        }
        value.erase(value.begin());
    }

    int base = 10;
    std::size_t start = 0;
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
        base = 16;
        start = 2;
    } else if (value.rfind("0b", 0) == 0 || value.rfind("0B", 0) == 0) {
        std::uint32_t parsed = 0;
        for (std::size_t i = 2; i < value.size(); i++) {
            if (value[i] != '0' && value[i] != '1') {
                throw std::runtime_error("bad binary number");
            }
            parsed = (parsed << 1) | static_cast<std::uint32_t>(value[i] - '0');
        }
        return mask24(static_cast<std::uint32_t>(sign * static_cast<int>(parsed)));
    }

    std::size_t pos = 0;
    std::uint32_t parsed =
        static_cast<std::uint32_t>(std::stoul(value.substr(start), &pos, base));
    if (pos != value.size() - start) {
        throw std::runtime_error("bad number");
    }
    if (sign < 0) {
        return mask24(static_cast<std::uint32_t>(-static_cast<int>(parsed)));
    }
    return parsed;
}

std::string hex(std::uint32_t value, int width) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(width)
        << std::setfill('0') << value;
    return out.str();
}

std::vector<std::uint8_t> readFile(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not open " + path);
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}

}
