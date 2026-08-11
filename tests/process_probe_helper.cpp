// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>

#include <unistd.h>

namespace {

bool writePidFile(const char *path)
{
    std::ofstream file(path);
    file << getpid();
    return file.good();
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        return 64;
    }

    const std::string_view mode(argv[1]);
    if (mode == "echo") {
        if (argc != 3) {
            return 64;
        }
        std::cout << argv[2];
        return 0;
    }
    if (mode == "mark") {
        return argc == 3 && writePidFile(argv[2]) ? 0 : 65;
    }
    if (mode == "stderr-flood") {
        for (int index = 0; index < 256 * 1024; ++index) {
            std::cerr.put('e');
        }
        std::cout << "safe stdout";
        return 0;
    }
    if (mode == "nonzero") {
        return 23;
    }
    if (mode == "crash") {
        std::abort();
    }
    if (mode == "timeout") {
        if (argc != 3 || !writePidFile(argv[2])) {
            return 65;
        }
        std::this_thread::sleep_for(std::chrono::seconds(10));
        return 0;
    }
    if (mode == "exact-limit") {
        for (int index = 0; index < 64 * 1024; ++index) {
            std::cout.put('e');
        }
        return 0;
    }
    if (mode == "limit-plus-one") {
        for (int index = 0; index < 64 * 1024 + 1; ++index) {
            std::cout.put('p');
        }
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(10));
        return 0;
    }
    if (mode == "oversize") {
        if (argc != 3 || !writePidFile(argv[2])) {
            return 65;
        }
        for (int index = 0; index < 256 * 1024; ++index) {
            std::cout.put('o');
        }
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(10));
        return 0;
    }
    return 64;
}
