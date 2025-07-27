#include <sys/types.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <dirent.h>
#include <cstring>

bool read_process_memory(pid_t pid, uintptr_t address, void* buffer, size_t size) {
    std::string mem_path = "/proc/" + std::to_string(pid) + "/mem";
    int mem_fd = open(mem_path.c_str(), O_RDONLY);
    if (mem_fd == -1) {
        perror("[Ultrawide] open(mem) for reading failed");
        return false;
    }
    ssize_t n = pread(mem_fd, buffer, size, address);
    close(mem_fd);
    return (n == (ssize_t)size);
}


bool write_process_memory(pid_t pid, uintptr_t address, const void* buffer, size_t size) {
    std::string mem_path = "/proc/" + std::to_string(pid) + "/mem";
    int mem_fd = open(mem_path.c_str(), O_RDWR);
    if (mem_fd == -1) {
        perror("[Ultrawide] open(mem) for writing failed");
        return false;
    }
    ssize_t n = pwrite(mem_fd, buffer, size, address);
    close(mem_fd);
    return (n == (ssize_t)size);
}


std::vector<std::pair<uintptr_t, uintptr_t>> get_process_memory_ranges(pid_t pid) {
    std::vector<std::pair<uintptr_t, uintptr_t>> ranges;
    std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
    FILE* maps = fopen(maps_path.c_str(), "r");
    if (!maps) return ranges;

    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t start, end;
        char perms[5];
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3) {
            // Only scan readable + executable code sections
            if (perms[0] == 'r' && perms[2] == 'x') {
                ranges.emplace_back(start, end);
            }
        }
    }
    fclose(maps);
    return ranges;
}


std::vector<uintptr_t> aob_scan_all(pid_t pid, const std::vector<uint8_t>& pattern, const std::vector<bool>& mask) {
    std::vector<uintptr_t> results;
    auto ranges = get_process_memory_ranges(pid);

    std::string mem_path = "/proc/" + std::to_string(pid) + "/mem";
    int mem_fd = open(mem_path.c_str(), O_RDONLY);
    if (mem_fd == -1) {
        perror("[Ultrawide] open(mem) failed for scanning");
        return results;
    }

    for (auto& range : ranges) {
        uintptr_t start = range.first;
        uintptr_t end   = range.second;
        size_t size     = end - start;

        std::vector<uint8_t> buffer(size);
        if (pread(mem_fd, buffer.data(), size, start) != (ssize_t)size) continue;

        for (size_t i = 0; i + pattern.size() <= buffer.size(); i++) {
            bool match = true;
            for (size_t j = 0; j < pattern.size(); j++) {
                if (mask[j] && buffer[i + j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                results.push_back(start + i);
            }
        }
    }

    close(mem_fd);
    return results;
}

// Main function: scan and patch
bool set_ultrawide_patch(pid_t pid, int mode) {

    std::vector<uint8_t> pattern = {0x85, 0xC0, 0x74, 0x73, 0x46, 0x8B, 0x84, 0x21,
                                    0xB4, 0x04, 0x00, 0x00, 0x45, 0x85, 0xC0, 0x74};
    std::vector<bool> mask(pattern.size(), true);

    printf("[Ultrawide] 🔍 Scanning for AOB pattern in PID %d...\n", pid);
    auto matches = aob_scan_all(pid, pattern, mask);

    if (matches.empty()) {
        printf("[Ultrawide] ❌ No matches found.\n");
        return false;
    }

    printf("[Ultrawide] ✅ Found %zu match(es):\n", matches.size());
    for (size_t i = 0; i < matches.size(); i++) {
        printf("   [%zu] 0x%lx\n", i, matches[i]);
    }

    // Pick the FIRST match automatically
    uintptr_t patchAddress = matches[0] + 0x0F;

    uint8_t currentByte;
    if (!read_process_memory(pid, patchAddress, &currentByte, 1)) {
        printf("[Ultrawide] ❌ Failed to read target byte.\n");
        return false;
    }

    printf("[Ultrawide] Current byte at patch location: 0x%02X\n", currentByte);

    if (currentByte != 0x74) {
        printf("[Ultrawide] ⚠ Unexpected byte (expected 0x74). Aborting patch.\n");
        return false;
    }

    // Overwrite with EB (jmp)
    uint8_t newByte = 0xEB;
    if (!write_process_memory(pid, patchAddress, &newByte, 1)) {
        printf("[Ultrawide] ❌ Failed to write patch.\n");
        return false;
    }

    printf("[Ultrawide] ✅ Patched byte at 0x%lx (0x74 → 0xEB)\n", patchAddress);
    return true;
}
