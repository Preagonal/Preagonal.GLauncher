#pragma once
#include <windows.h>
#include <vector>
#ifndef MEMORY_HOOK_HPP
#define MEMORY_HOOK_HPP



struct HookInfo {
    void* original;
    std::vector<uint8_t> originalBytes;
    size_t size;
};

namespace Memory {
    // Singleton to avoid multiple definition
    inline std::vector<HookInfo>& getHooks() {
        static std::vector<HookInfo> hooks;
        return hooks;
    }

    /**
     * Creates a hook redirecting execution from one function to another
     * @param original Pointer to the original function
     * @param hook Pointer to the destination function
     * @param bytes Number of bytes to overwrite (minimum 14 for x64 absolute jmp)
     * @return true if hook was created successfully
     */
    inline bool hook(void* original, void* hook, size_t bytes = 14) {
        if (!original || !hook || bytes < 14) {
            return false;
        }

        // Save original bytes
        HookInfo info;
        info.original = original;
        info.size = bytes;
        info.originalBytes.resize(bytes);
        memcpy(info.originalBytes.data(), original, bytes);

        // Change memory protection to writable
        DWORD oldProtect;
        if (!VirtualProtect(original, bytes, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }

        // Assemble 64-bit absolute jump
        uint8_t* pOriginal = static_cast<uint8_t*>(original);

        // mov rax, imm64 (48 B8 + 8 address bytes)
        pOriginal[0] = 0x48;
        pOriginal[1] = 0xB8;
        *reinterpret_cast<uint64_t*>(&pOriginal[2]) = reinterpret_cast<uint64_t>(hook);

        // jmp rax (FF E0)
        pOriginal[10] = 0xFF;
        pOriginal[11] = 0xE0;

        // Fill the rest with NOPs
        for (size_t i = 12; i < bytes; i++) {
            pOriginal[i] = 0x90; // NOP
        }

        // Flush instruction cache
        FlushInstructionCache(GetCurrentProcess(), original, bytes);

        // Restore original protection
        VirtualProtect(original, bytes, oldProtect, &oldProtect);

        // Save hook information
        getHooks().push_back(info);

        return true;
    }

    /**
     * Restores a function to its original state
     * @param original Pointer to the original function
     * @return true if unhook was successful
     */
    inline bool unhook(void* original) {
        auto& hooks = getHooks();
        for (auto it = hooks.begin(); it != hooks.end(); ++it) {
            if (it->original == original) {
                DWORD oldProtect;
                if (!VirtualProtect(original, it->size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    return false;
                }

                memcpy(original, it->originalBytes.data(), it->size);
                FlushInstructionCache(GetCurrentProcess(), original, it->size);
                VirtualProtect(original, it->size, oldProtect, &oldProtect);

                hooks.erase(it);
                return true;
            }
        }
        return false;
    }

    /**
     * Patches memory with raw bytes
     * @param address Pointer to memory location
     * @param bytes Byte array to write
     * @param size Number of bytes to write
     * @return true if patch was successful
     */
    inline bool patch(void* address, const uint8_t* bytes, size_t size) {
        if (!address || !bytes || size == 0) {
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }

        memcpy(address, bytes, size);
        FlushInstructionCache(GetCurrentProcess(), address, size);

        VirtualProtect(address, size, oldProtect, &oldProtect);
        return true;
    }
}

#endif // MEMORY_HOOK_HPP