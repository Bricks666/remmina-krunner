// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <string_view>
#include <thread>

int main(int argc, char **argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--rescan") {
        if (const char *logPath = std::getenv("PACKAGE_HELPER_LOG")) {
            std::ofstream(logPath, std::ios::app) << "--rescan\n";
        }
        if (const char *readyPath = std::getenv("PACKAGE_HELPER_BLOCK_READY");
            readyPath && *readyPath) {
            std::ofstream(readyPath) << "ready\n";
            const char *releasePath = std::getenv("PACKAGE_HELPER_BLOCK_RELEASE");
            if (!releasePath || !*releasePath) {
                return 65;
            }
            while (!std::filesystem::exists(releasePath)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "hold") {
        std::this_thread::sleep_for(std::chrono::minutes(5));
        return 0;
    }
    return 64;
}
