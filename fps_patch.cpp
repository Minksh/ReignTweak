#include "fps_patch.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdio>

bool write_memory(pid_t pid, uintptr_t address, const void* buffer, size_t size) {
    iovec local { (void*)buffer, size };
    iovec remote { (void*)address, size };
    ssize_t bytes = process_vm_writev(pid, &local, 1, &remote, 1, 0);
    return (bytes == (ssize_t)size);
}


// AOB scanner

uintptr_t aob_scan(pid_t pid, const std::vector<uint8_t> &pattern, const std::string &mask) {
    std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
    if (!maps.is_open()) return 0;

    int mem_fd = open(("/proc/" + std::to_string(pid) + "/mem").c_str(), O_RDONLY);
    if (mem_fd == -1) return 0;

    std::string line;
    while (std::getline(maps, line)) {
        uintptr_t start, end;
        char perms[5];
        if (sscanf(line.c_str(), "%lx-%lx %4s", &start, &end, perms) != 3)
            continue;

        if (perms[0] != 'r') continue;

        size_t region_size = end - start;
        std::vector<uint8_t> buffer(region_size);

        if (pread(mem_fd, buffer.data(), buffer.size(), start) != (ssize_t)buffer.size())
            continue;

        for (size_t i = 0; i < buffer.size() - pattern.size(); i++) {
            bool match = true;
            for (size_t j = 0; j < pattern.size(); j++) {
                if (mask[j] == 'x' && buffer[i+j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                close(mem_fd);
                return start + i;
            }
        }
    }

    close(mem_fd);
    return 0;
}



bool set_fps_from_pid(pid_t pid, float fps) {
    float frametime = (1000.0f / fps) / 1000.0f;

    std::cout << "[FPS] Setting FPS to " << fps << "…\n";

    std::vector<uint8_t> pattern = { 0xC7, 0x43, 0x20, 0x89, 0x88, 0x88, 0x3C };
    std::string mask = "xxxxxxx";

    uintptr_t addr = aob_scan(pid, pattern, mask);
    if (!addr) {
        std::cerr << "[FPS] Could not find FPS limiter pattern in memory.\n";
        return false;
    }

    uintptr_t float_addr = addr + 3;

    if (write_memory(pid, float_addr, &frametime, sizeof(frametime))) {
        std::cout << "[FPS] Successfully patched FPS to " << fps << "!\n";
        return true;
    } else {
        std::cerr << "[FPS] Failed to write FPS value.\n";
        return false;
    }
}
