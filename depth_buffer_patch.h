#ifndef DEPTH_BUFFER_PATCH_H
#define DEPTH_BUFFER_PATCH_H

#include <sys/types.h>
#include <cstdint>
#include <vector>

struct BufferCandidate {
    uintptr_t address;
    uint32_t size_value;
};

// Scan process memory for dimension assignments, sorted largest to smallest
std::vector<BufferCandidate> detect_depth_buffers(pid_t pid);

// Write a new resolution value to a detected target address
bool apply_depth_res_patch(pid_t pid, uintptr_t target_addr, uint16_t new_val = 0);

// Auto-detect top candidate and patch it (halves current value if new_val == 0)
bool auto_patch_depth_buffer(pid_t pid, uint16_t new_val = 0);

#endif // DEPTH_BUFFER_PATCH_H
