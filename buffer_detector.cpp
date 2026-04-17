#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm> // Added for std::sort
#include "ultrawide_patch.h" // Reusing AOB scanner and memory readers because i am lazy

struct BufferCandidate {
    uintptr_t address;
    uint32_t size_value;
};

void detect_depth_buffer_logic(pid_t pid) {
    std::cout << "[Detector] 🔍 Scanning for depth buffer allocation logic in PID " << pid << "...\n";

    // Pattern: mov [rcx+r11], rbx (very common, so we filter heavily)
    std::vector<uint8_t> pattern = { 0x48, 0x89, 0x5F }; 
    std::vector<bool> mask(pattern.size(), true);

    auto matches = aob_scan_all(pid, pattern, mask);
    if (matches.empty()) {
        std::cout << "[Detector] X No depth buffer logic found.\n";
        return;
    }

    const uint32_t MIN_RES = 100;   // Catches half-res/tiling buffers too
    const uint32_t MAX_RES = 16384; // Future-proof for 8K+ displays

    std::vector<BufferCandidate> candidates;
    for (auto addr : matches) {
        bool found_valid = false;
        // Try reading at +0, +4, and +8 offsets relative to the match address
        for (int off = 0; off <= 8; off += 4) {
            uint32_t val = 0;
            if (read_process_memory(pid, addr + off, &val, sizeof(val))) {
                if (val >= MIN_RES && val <= MAX_RES) {
                    candidates.push_back({addr + off, val});
                    found_valid = true;
                    break; // Stop at first valid offset for this match
                }
            }
        }
    }

    if (candidates.empty()) {
        std::cout << "[Detector] [!]️ Found matches but none in valid resolution range (" 
                  << MIN_RES << "-" << MAX_RES << ").\n";
        return;
    }

    // Sort from largest to smallest buffer dimension
    std::sort(candidates.begin(), candidates.end(), [](const BufferCandidate& a, const BufferCandidate& b) {
        return a.size_value > b.size_value;
    });

    std::cout << "[Detector] ✅ Filtered & sorted " << candidates.size() 
              << " candidate(s) by dimension (largest first):\n";
    for (size_t i = 0; i < candidates.size(); ++i) {
        printf("   [%zu] Addr: 0x%lx | Dim: %u\n", i, candidates[i].address, candidates[i].size_value);
    }
}
