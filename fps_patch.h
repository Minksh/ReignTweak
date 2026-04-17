#ifndef FPS_PATCH_H
#define FPS_PATCH_H

#include <sys/types.h>
#include <cstdint>
#include <vector>
#include <string>

// Write memory to a remote process
bool write_memory(pid_t pid, uintptr_t address, const void* buffer, size_t size);

// Scan for pattern in memory of a given PID (AOB scanning)
uintptr_t aob_scan(pid_t pid, const std::vector<uint8_t> &pattern, const std::string &mask);

// Set FPS limit by patching the game's frametime value
bool set_fps_from_pid(pid_t pid, float fps);

#endif // FPS_PATCH_H
