#ifndef ULTRAWIDE_PATCH_H
#define ULTRAWIDE_PATCH_H

#include <sys/types.h>
#include <cstdint>
#include <vector>
#include <string>

// Read memory from a remote process
bool read_process_memory(pid_t pid, uintptr_t address, void* buffer, size_t size);

// Write memory to a remote process
bool write_process_memory(pid_t pid, uintptr_t address, const void* buffer, size_t size);

// Get all readable & executable memory ranges for a given PID
std::vector<std::pair<uintptr_t, uintptr_t>> get_process_memory_ranges(pid_t pid);

// Scan for multiple matches of an AOB pattern in the process's memory
std::vector<uintptr_t> aob_scan_all(pid_t pid, const std::vector<uint8_t>& pattern, const std::vector<bool>& mask);

// Apply ultrawide patch to the game
bool set_ultrawide_patch(pid_t pid, int mode);

#endif // ULTRAWIDE_PATCH_H
