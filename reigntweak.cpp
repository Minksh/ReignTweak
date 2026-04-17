#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/wait.h>
#include <thread>
#include <chrono>

#include "fps_patch.h"
#include "ultrawide_patch.h"
#include "depth_buffer_patch.h"


// Forward declarations for the new module logic
void detect_depth_buffer_logic(pid_t pid);
bool apply_depth_res_patch(pid_t pid, uintptr_t target_addr);

// ✅ PID finder (needed for patching)
pid_t find_pid_by_name(const std::string& name) {
    DIR* proc = opendir("/proc");
    if (!proc) return -1; // FIXED: was -int

    struct dirent* entry;
    while ((entry = readdir(proc))) {
        if (entry->d_type != DT_DIR) continue;
        pid_t pid = atoi(entry->d_name);
        if (pid <= 0) continue;

        std::string cmdline_path = "/proc/" + std::to_string(pid) + "/cmdline";
        std::ifstream cmdline_file(cmdline_path);
        if (!cmdline_file.is_open()) continue;

        std::string cmdline;
        std::getline(cmdline_file, cmdline, '\0');
        if (cmdline.find("nightreign.exe") != std::string::npos) {
            closedir(proc);
            return pid;
        }
    }
    closedir(proc);
    return -1;
}

std::string get_runtime_dir() {
    const char* xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg) return std::string(xdg) + "/reigntweak";
    return "/run/user/" + std::to_string(getuid()) + "/reigntweak";
}

void usage() {
    std::cout << "Usage:\n";
    std::cout << "  reigntweak fps <val>         - Changes FPS limit for running game\n";
    std::cout << "  reigntweak ultrawide         - Applies ultrawide patch\n";
    std::cout << "  reigntweak detect_buffers    - Scans for depth buffer logic (requires running game)\n";
    std::cout << "  reigntweak lowres_depth     - Patches depth buffers to lower res\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage();
        return 1;
    }

    std::string cmd = argv[1];

    // ===== MODE: STANDALONE UTILITIES =====
    if (cmd == "detect_buffers") {
        pid_t game_pid = find_pid_by_name("nightreign.exe");
        if (game_pid <= 0) {
            std::cerr << "[!] Error: nightreign.exe must be running to detect buffers.\n";
            return 1;
        }
        auto candidates = detect_depth_buffers(game_pid);
        if (candidates.empty()) {
            std::cout << "[Detector] No valid depth buffer candidates found.\n";
            return 0;
        }
        std::cout << "[Detector] ✅ Found " << candidates.size() 
                  << " candidate(s), sorted by dimension (largest first):\n";
        for (size_t i = 0; i < candidates.size(); ++i) {
            printf("   [%zu] Addr: 0x%lx | Dim: %u\n", i, candidates[i].address, candidates[i].size_value);
        }
        return 0;
    }

    if (cmd == "lowres_depth") {
        pid_t game_pid = find_pid_by_name("nightreign.exe");
        if (game_pid <= 0) {
            std::cerr << "[!] Error: nightreign.exe must be running to apply patch.\n";
            return 1;
        }

        uint16_t target_val = 0;
        bool use_specific_addr = false;
        uintptr_t specific_addr = 0;

        // Support: reigntweak lowres_depth <addr> [value]
        if (argc >= 2) {
            std::string arg = argv[1];
            if (arg.find("0x") == 0 || arg.find("0X") == 0) {
                specific_addr = std::stoul(arg, nullptr, 16);
                use_specific_addr = true;
            } else {
                target_val = static_cast<uint16_t>(std::stoi(arg));
            }
        }
        if (argc >= 3 && !use_specific_addr) {
            target_val = static_cast<uint16_t>(std::stoi(argv[2]));
        }

        bool success = false;
        if (use_specific_addr) {
            std::cout << "[Depth] Patching specific address: 0x" << std::hex << specific_addr << "\n";
            success = apply_depth_res_patch(game_pid, specific_addr, target_val);
        } else {
            success = auto_patch_depth_buffer(game_pid, target_val);
        }

        return success ? 0 : 1;
    }

    // %command% logic
    std::string appid = "2622380";  
    std::string phd = get_runtime_dir();

    if (cmd == "fps" && argc < 3) {
        std::cerr << "Usage: reigntweak fps <value>\n";
        return 1;
    }

    //  Collect commands before %command% 
    std::vector<std::string> patch_cmds;
    int game_index = -1;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find('/') == 0 || arg.find("./") == 0) {
            game_index = i;
            break;
        }
        patch_cmds.push_back(arg);
    }

    if (game_index == -1) {
        std::cerr << "Error: No game launch command found (missing %command%).\n";
        return 1;
    }

    // Setup environment and directories
    std::string appdir = phd + "/" + appid;
    mkdir(phd.c_str(), 0755);
    mkdir(appdir.c_str(), 0755);

    {
        // Write exe file (first arg containing "/proton")
        std::ofstream exe_file(appdir + "/exe");
        for (int i = game_index; i < argc; i++) {
            if (strstr(argv[i], "/proton")) {
                exe_file << argv[i] << "\n";
                break;
            }
        }

        // Write pfx file if Steam sets it
        const char* compat_data = getenv("STEAM_COMPAT_DATA_PATH");
        if (compat_data) {
            std::ofstream pfx_file(appdir + "/pfx");
            pfx_file << compat_data << "/pfx";
        }

        // Save all environment vars
        {
            std::ofstream env_file(appdir + "/env");
            extern char **environ;
            for (char **env = environ; *env; ++env) {
                env_file << *env << "\n";
            }
        }

        // Launch game process
        pid_t pid = fork();
        if (pid == 0) {
            execvp(argv[game_index], &argv[game_index]);
            perror("execvp");
            exit(127);
        } else {
            // Retry loop to wait for nightreign.exe to show up
            pid_t game_pid = -1;
            for (int attempt = 1; attempt <= 15; attempt++) {
                game_pid = find_pid_by_name("nightreign.exe");
                if (game_pid > 0) {
                    std::cout << "[+] Found nightreign.exe (PID " << game_pid
                              << ") on attempt " << attempt << "\n";
                    break;
                }
                std::cerr << "[!] nightreign.exe not found (attempt " << attempt
                          << "), retrying...\n";
                std::this_thread::sleep_for(std::chrono::seconds(2)); // Wait 2s
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }

 if (game_pid > 0) {
                // Apply patches passed via command line arguments
                for (size_t i = 0; i < patch_cmds.size(); i++) {
                    if (patch_cmds[i] == "fps" && i + 1 < patch_cmds.size()) {
                        float fpsValue = std::stof(patch_cmds[i + 1]);
                        bool fpsSet = false;
                        for (int attempt = 1; attempt <= 5; attempt++) {
                            if (set_fps_from_pid(game_pid, fpsValue)) {
                                std::cout << "[+] FPS set to " << fpsValue 
                                          << " (attempt " << attempt << ")\n";
                                fpsSet = true;
                                break;
                            } else {
                                std::cerr << "[!] FPS patch attempt " << attempt 
                                          << " failed, retrying...\n";
                                std::this_thread::sleep_for(std::chrono::seconds(2));
                            }
                        }
                        if (!fpsSet) {
                            std::cerr << "[!] Failed to set FPS after 5 attempts.\n";
                        }
                        i++; // skip FPS value
                    } 
                    else if (patch_cmds[i] == "ultrawide") {
                        if (!set_ultrawide_patch(game_pid, 1)) {
                            std::cerr << "[!] Failed to apply ultrawide patch.\n";
                        } else {
                            std::cout << "[+] Ultrawide patch applied.\n";
                        }
                    }
                    // ✅ NEW: Depth buffer patch handling
                    else if (patch_cmds[i] == "depth" || patch_cmds[i] == "lowres_depth") {
                        uint16_t target_val = 0;
                        bool use_specific_addr = false;
                        uintptr_t specific_addr = 0;

                        size_t next_idx = i + 1;
                        if (next_idx < patch_cmds.size()) {
                            std::string arg = patch_cmds[next_idx];
                            // Check for hex address format
                            if ((arg.find("0x") == 0 || arg.find("0X") == 0)) {
                                try {
                                    specific_addr = std::stoul(arg, nullptr, 16);
                                    use_specific_addr = true;
                                    next_idx++; // consume address from args
                                } catch (...) {}
                            } else {
                                // Assume it's a numeric value if not an address
                                try { target_val = static_cast<uint16_t>(std::stoi(arg)); } catch (...) {}
                            }
                        }

                        bool success = false;
                        if (use_specific_addr) {
                            std::cout << "[Depth] Patching specific address: 0x" << std::hex << specific_addr 
                                      << (target_val ? " to value: " + std::to_string(target_val) : "") << "\n";
                            success = apply_depth_res_patch(game_pid, specific_addr, target_val);
                        } else {
                            std::cout << "[Depth] Auto-detecting & patching largest depth buffer" 
                                      << (target_val ? " to value: " + std::to_string(target_val) : "") << "\n";
                            success = auto_patch_depth_buffer(game_pid, target_val);
                        }

                        if (success) {
                            std::cout << "[+] Depth buffer patched successfully.\n";
                        } else {
                            std::cerr << "[!] Failed to patch depth buffer.\n";
                        }
                    }
                }
            } else {
                std::cerr << "[!] Could not find nightreign.exe process after waiting.\n";
            }

            int status;
            waitpid(pid, &status, 0);

            // Cleanup after game exits
            std::string rm_cmd = "rm -r " + appdir;
            system(rm_cmd.c_str()); // FIXED: was system(rm.c_str())
            return WEXITSTATUS(status);
        }
    }
}
