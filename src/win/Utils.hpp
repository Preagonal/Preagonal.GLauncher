#include "pch.h"

void SetupConsole() {
    ::AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    ShowWindow(GetConsoleWindow(), SW_HIDE);
}

void CloseConsole() {
    fclose(stdout);
    fclose(stderr);
    fclose(stdin);
    FreeConsole();
}

void* alloc(size_t size) {
    void* ptr = malloc(size);
    memset(ptr, 0, size);
    return ptr;
}

std::string from_hex(const std::string& hex_str) {
    std::string bytes;
    std::string hex_clean;

    for (char c : hex_str) {
        if (!std::isspace(c)) {
            hex_clean += std::toupper(c);
        }
    }

    if (hex_clean.length() % 2 != 0) {
        return bytes;
    }

    bytes.reserve(hex_clean.length() / 2);
    for (size_t i = 0; i < hex_clean.length(); i += 2) {
        std::string byte_str = hex_clean.substr(i, 2);
        unsigned char byte = (unsigned char)strtol(byte_str.c_str(), nullptr, 16);
        bytes += byte;
    }
    return bytes;
}
