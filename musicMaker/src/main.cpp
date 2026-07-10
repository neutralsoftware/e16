#include "music_maker.h"

#include <iostream>

int main() {
    musicmaker::App app;
    std::string error;
    if (!app.open(error)) {
        std::cerr << "E16 Music Maker: " << error << std::endl;
        return 1;
    }
    return app.run();
}
