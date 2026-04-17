#include "depth_buffer_patch.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>

static bool read_mem(pid_t pid, uintptr_t addr, void* buf, size_t sz) {
    int fd = open(("/proc/" + std::to_string(pid) + "/mem").c_str(), O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = pread(fd, buf, sz, addr);
    close(fd);
    return n == (ssize_t)sz;
}

static bool write_mem(pid_t pid, uintptr_t addr, const void* buf, size_t sz) {
    int fd = open(("/proc/" + std::to_string(pid) + "/mem").c_str(), O_RDWR);
    if (fd < 0) return false;
    ssize_t n = pwrite(fd, buf, sz, addr);
    close(fd);
    return n == (ssize_t)sz;
}

std::vector<BufferCandidate> detect_depth_buffers(pid_t pid) {
    std::cout << "[Depth] 🔍 Starting depth buffer scan for PID " << pid << "...\n";

    // AOB: mov [rcx+r11], rbx (0x48 0x89 0x5F)
    std::vector<uint8_t> pattern = { 0x48, 0x89, 0x5F };
    std::vector<bool> mask(pattern.size(), true);

    FILE* maps = fopen(("/proc/" + std::to_string(pid) + "/maps").c_str(), "r");
    if (!maps) {
        std::cerr << "[Depth] X Failed to open /proc/" << pid << "/maps\n";
        return {};
    }

    std::vector<std::pair<uintptr_t, uintptr_t>> ranges;
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t start, end;
        char perms[5];
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3 && 
            perms[0] == 'r' && perms[2] == 'x') {
            ranges.emplace_back(start, end);
        }
    }
    fclose(maps);

    std::cout << "[Depth] 📖 Found " << ranges.size() << " executable memory range(s).\n";

    int mem_fd = open(("/proc/" + std::to_string(pid) + "/mem").c_str(), O_RDONLY);
    if (mem_fd < 0) {
        std::cerr << "[Depth] X Failed to open /proc/" << pid << "/mem\n";
        return {};
    }

    std::vector<BufferCandidate> candidates;
    for (auto& range : ranges) {
        uintptr_t start = range.first;
        size_t size = range.second - start;
        std::vector<uint8_t> buffer(size);
        if (pread(mem_fd, buffer.data(), size, start) != (ssize_t)size) continue;

        for (size_t i = 0; i + pattern.size() <= buffer.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < pattern.size(); ++j) {
                if (mask[j] && buffer[i+j] != pattern[j]) { match = false; break; }
            }
            if (!match) continue;

            uintptr_t addr = start + i;
            // Check offsets +0, +4, +8 for the actual dimension DWORD
            for (int off = 0; off <= 8; off += 4) {
                uint32_t val = 0;
                if (read_mem(pid, addr + off, &val, sizeof(val))) {
                    // Filter: plausible resolution range & game binary region
                    if (val >= 100 && val <= 16384 && addr >= 0x140000000) {
                        candidates.push_back({addr + off, val});
                        std::cout << "[Depth]   ✅ Match at 0x" << std::hex << (addr+off) 
                                  << " | Offset: +" << std::dec << off 
                                  << " | Dim: " << val << "\n";
                    }
                }
            }
        }
    }
    close(mem_fd);

    // Sort largest to smallest
    std::sort(candidates.begin(), candidates.end(), [](const BufferCandidate& a, const BufferCandidate& b) {
        return a.size_value > b.size_value;
    });

    if (candidates.empty()) {
        std::cout << "[Depth] [!] No valid depth buffer candidates found in executable memory.\n";
    } else {
        std::cout << "[Depth] 📊 Filtered & sorted " << candidates.size() 
                  << " candidate(s) by dimension (largest first):\n";
        for (size_t i = 0; i < candidates.size(); ++i) {
            printf("   [%zu] Addr: 0x%lx | Dim: %u\n", i, candidates[i].address, candidates[i].size_value);
        }
    }

    return candidates;
}

bool apply_depth_res_patch(pid_t pid, uintptr_t target_addr, uint16_t new_val) {
    std::cout << "[Depth] 📝 Preparing patch for address 0x" << std::hex << target_addr << "...\n";

    if (new_val == 0) {
        // Auto-halve current value if not specified
        uint32_t current = 0;
        if (!read_mem(pid, target_addr, &current, sizeof(current))) {
            std::cerr << "[Depth] X Failed to read current value at 0x" << std::hex << target_addr << "\n";
            return false;
        }
        new_val = std::max(1u, current / 2);
        std::cout << "[Depth]   [i] Auto-halving current value: " << std::dec << current 
                  << " → " << new_val << "\n";
    }

    uint32_t patch_data = static_cast<uint32_t>(new_val);
    bool success = write_mem(pid, target_addr, &patch_data, sizeof(patch_data));

    if (success) {
        // Verify the write by reading it back immediately
        uint32_t verify = 0;
        if (read_mem(pid, target_addr, &verify, sizeof(verify))) {
            std::cout << "[Depth] ✅ Patch successful! Verified value: " << std::dec << verify 
                      << " at 0x" << std::hex << target_addr << "\n";
        } else {
            std::cerr << "[Depth] [!] Write succeeded, but verification read failed.\n";
        }
    } else {
        std::cerr << "[Depth] X Failed to write patch at 0x" << std::hex << target_addr << "\n";
    }

    return success;
}

bool auto_patch_depth_buffer(pid_t pid, uint16_t new_val) {
    std::cout << "[Depth] 🚀 Starting auto-patch sequence...\n";
    auto candidates = detect_depth_buffers(pid);
    
    if (candidates.empty()) {
        std::cerr << "[Depth] X No valid depth buffer candidates found.\n";
        return false;
    }

    // Cap at 8 largest candidates to prevent crashes from over-patching
    size_t limit = std::min(candidates.size(), static_cast<size_t>(6));
    
    bool any_success = false;
    for (size_t i = 0; i < limit; ++i) {
        uintptr_t target_addr = candidates[i].address;
        uint32_t original_val = candidates[i].size_value;

        std::cout << "[Depth] 🎯 Candidate #" << i << ": 0x" << std::hex << target_addr 
                  << " (current dim: " << std::dec << original_val << ")\n";
        
        if (apply_depth_res_patch(pid, target_addr, new_val)) {
            any_success = true;
        } else {
            std::cerr << "[Depth] [!] Failed to patch candidate #" << i << "\n";
        }
    }

    if (any_success) {
        std::cout << "[Depth] 🏁 Depth buffer patching complete.\n";
    } else {
        std::cerr << "[Depth] 💥 All depth buffer patches failed.\n";
    }
    
    return any_success;
}
