// rinse - Fast CLI frontend for pacman and AUR
// License: GPL-3.0
// Compile: g++ -std=c++17 -O3 rinse.cpp -o rinse

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <regex>
#include <cstdlib>
#include <unistd.h>

namespace fs = std::filesystem;

const char* RESET = "\033[0m";
const char* BOLD = "\033[1m";
const char* RED = "\033[31m";
const char* GREEN = "\033[32m";
const char* YELLOW = "\033[33m";
const char* BLUE = "\033[34m";
const char* CYAN = "\033[36m";

struct Config {
    bool keep_build = false;
    bool notify = true;
    bool disable_auto_update = false;
    std::string outdated_time = "6m";
};

Config g_config;
bool g_dry_run = false;
bool g_keep = false;
bool g_full_log = false;

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

std::string exec(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

int exec_status(const std::string& cmd) {
    return system(cmd.c_str());
}

bool check_command(const std::string& cmd) {
    return exec_status("command -v " + cmd + " >/dev/null 2>&1") == 0;
}

std::string get_home() {
    const char* home = getenv("HOME");
    return home ? home : "/root";
}

void send_notification(const std::string& msg) {
    if (g_config.notify && check_command("notify-send")) {
        exec_status("notify-send 'rinse' '" + msg + "' 2>/dev/null");
    }
}

std::string time_ago(const std::string& date_str) {
    time_t now = time(nullptr);
    struct tm tm_date = {};
    
    if (strptime(date_str.c_str(), "%d %B %Y", &tm_date) ||
        strptime(date_str.c_str(), "%Y-%m-%d", &tm_date)) {
        time_t pkg_time = mktime(&tm_date);
        double diff = difftime(now, pkg_time);
        int days = diff / 86400;
        
        if (days == 0) return "today";
        if (days == 1) return "1 day ago";
        if (days < 30) return std::to_string(days) + " days ago";
        if (days < 365) return std::to_string(days / 30) + " months ago";
        return std::to_string(days / 365) + " years ago";
    }
    
    return "unknown";
}

int parse_time_value(const std::string& val) {
    std::regex re("(\\d+)([dmy])");
    std::smatch match;
    if (std::regex_match(val, match, re)) {
        int num = std::stoi(match[1]);
        char unit = match[2].str()[0];
        if (unit == 'd') return num;
        if (unit == 'm') return num * 30;
        if (unit == 'y') return num * 365;
    }
    return 180;
}

void load_config() {
    std::string config_path = get_home() + "/.config/rinse/rinse.conf";
    if (!fs::exists(config_path)) return;
    
    std::ifstream file(config_path);
    std::string line;
    
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        size_t comment = line.find("//");
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
            line = trim(line);
        }
        
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            
            if (key == "keep_build") g_config.keep_build = (val == "true");
            else if (key == "notify") g_config.notify = (val == "true");
            else if (key == "disable_auto_update") g_config.disable_auto_update = (val == "true");
            else if (key == "outdated_time") g_config.outdated_time = val;
        }
    }
}

std::string get_package_date_pacman(const std::string& pkg) {
    std::string cmd = "pacman -Si " + pkg + " 2>/dev/null | grep 'Build Date' | cut -d: -f2-";
    return trim(exec(cmd));
}

std::string get_package_date_aur(const std::string& pkg) {
    std::string cmd = "curl -s 'https://aur.archlinux.org/rpc/?v=5&type=info&arg=" + pkg + 
                     "' | grep -o '\"LastModified\":[0-9]*' | cut -d: -f2";
    std::string result = exec(cmd);
    if (!result.empty()) {
        time_t timestamp = std::stoll(result);
        char buf[64];
        strftime(buf, sizeof(buf), "%d %B %Y", localtime(&timestamp));
        return buf;
    }
    return "";
}

bool is_installed(const std::string& pkg) {
    return exec_status("pacman -Q " + pkg + " >/dev/null 2>&1") == 0;
}

bool is_outdated(const std::string& pkg) {
    return exec_status("pacman -Qu 2>/dev/null | grep -q '^" + pkg + " '") == 0;
}

bool package_in_pacman(const std::string& pkg) {
    return exec_status("pacman -Si " + pkg + " >/dev/null 2>&1") == 0;
}

bool package_in_aur(const std::string& pkg) {
    return exec_status("curl -s 'https://aur.archlinux.org/rpc/?v=5&type=info&arg=" + pkg + 
                      "' | grep -q '\"resultcount\":1'") == 0;
}

bool confirm(const std::string& prompt, bool default_yes = true) {
    bool has_suffix = prompt.find("[Y/n]") != std::string::npos || 
                      prompt.find("[y/N]") != std::string::npos;
    
    if (has_suffix) {
        std::cout << prompt << " " << std::flush;
    } else {
        std::string suffix = default_yes ? " [Y/n] " : " [y/N] ";
        std::cout << prompt << suffix << std::flush;
    }
    
    std::string response;
    std::getline(std::cin, response);
    response = trim(response);
    
    if (response.empty()) return default_yes;
    return (response[0] == 'y' || response[0] == 'Y');
}

void show_progress(const std::string& cmd, const std::string& action = "Processing") {
    if (g_dry_run) {
        std::cout << YELLOW << "[DRY RUN] Would execute: " << RESET << cmd << "\n";
        return;
    }
    
    if (g_full_log) {
        system(cmd.c_str());
        return;
    }
    
    // Simple animated progress bar
    std::cout << CYAN << "[====================] " << action << "..." << RESET << "\n" << std::flush;
    
    std::string silent_cmd = cmd + " > /dev/null 2>&1";
    int status = system(silent_cmd.c_str());
    
    if (status != 0) {
        std::cout << RED << "✗ Failed" << RESET << std::endl;
        std::cout << "Running with output for debugging:" << std::endl;
        system(cmd.c_str());
    }
}

void ensure_yay() {
    if (check_command("yay")) return;
    
    if (!confirm(YELLOW + std::string("yay (AUR frontend) not found. Install?") + RESET, true)) {
        std::cerr << RED << "Cannot install AUR packages without yay" << RESET << std::endl;
        exit(1);
    }
    
    if (g_dry_run) {
        std::cout << YELLOW << "[DRY RUN] Would install yay" << RESET << std::endl;
        return;
    }
    
    std::cout << CYAN << "Installing yay..." << RESET << std::endl;
    system("cd /tmp && git clone https://aur.archlinux.org/yay.git && cd yay && makepkg -si --noconfirm");
}

void install_packages(const std::vector<std::string>& pkgs) {
    std::vector<std::string> pacman_pkgs, aur_pkgs, not_found;
    
    for (const auto& pkg : pkgs) {
        bool installed = is_installed(pkg);
        bool outdated = installed && is_outdated(pkg);
        
        if (package_in_pacman(pkg)) {
            std::string date = get_package_date_pacman(pkg);
            
            if (installed && !outdated) {
                if (confirm(YELLOW + std::string("Package \"") + pkg + "\" already installed. Reinstall?" + RESET, false)) {
                    pacman_pkgs.push_back(pkg);
                }
            } else if (outdated) {
                if (confirm(YELLOW + std::string("Package \"") + pkg + "\" already installed, but outdated. Update?" + RESET, true)) {
                    pacman_pkgs.push_back(pkg);
                }
            } else {
                std::cout << GREEN << "Installing package \"" << pkg << "\" from pacman" << RESET << std::endl;
                if (!date.empty()) {
                    std::cout << "Last updated: " << date << " (" << time_ago(date) << ")" << std::endl;
                }
                if (confirm("", true)) {
                    pacman_pkgs.push_back(pkg);
                }
            }
        } else if (package_in_aur(pkg)) {
            std::string date = get_package_date_aur(pkg);
            
            if (installed && !outdated) {
                if (confirm(YELLOW + std::string("Package \"") + pkg + "\" already installed. Reinstall?" + RESET, false)) {
                    aur_pkgs.push_back(pkg);
                }
            } else if (outdated) {
                if (confirm(YELLOW + std::string("Package \"") + pkg + "\" already installed, but outdated. Update?" + RESET, true)) {
                    aur_pkgs.push_back(pkg);
                }
            } else {
                std::cout << BLUE << "Package \"" << pkg << "\" not found on pacman, but found on the AUR." << RESET << std::endl;
                if (!date.empty()) {
                    std::cout << "Last updated: " << date << " (" << time_ago(date) << ")" << std::endl;
                }
                if (confirm("", false)) {
                    aur_pkgs.push_back(pkg);
                }
            }
        } else {
            not_found.push_back(pkg);
        }
    }
    
    for (const auto& pkg : not_found) {
        std::cout << RED << "Package \"" << pkg << "\" not found on pacman or the AUR." << RESET << std::endl;
        std::cout << "If the package is a .tar.gz file you want to install, run \"rinse <path/to/file>\"" << std::endl;
    }
    
    if (!pacman_pkgs.empty()) {
        std::cout << CYAN << "\nInstalling from official repos..." << RESET << std::endl;
        std::string cmd = "sudo pacman -S --noconfirm";
        for (const auto& pkg : pacman_pkgs) cmd += " " + pkg;
        show_progress(cmd, "Installing");
    }
    
    if (!aur_pkgs.empty()) {
        ensure_yay();
        std::cout << CYAN << "\nInstalling from AUR..." << RESET << std::endl;
        std::string cmd = "yay -S --noconfirm";
        for (const auto& pkg : aur_pkgs) cmd += " " + pkg;
        show_progress(cmd, "Installing");
    }
    
    if (!pacman_pkgs.empty() || !aur_pkgs.empty()) {
        std::cout << GREEN << "\n✓ Installation complete" << RESET << std::endl;
        send_notification("Package installation complete");
    }
}

void remove_package(const std::string& pkg) {
    if (!is_installed(pkg)) {
        std::cerr << RED << "Package \"" << pkg << "\" is not installed" << RESET << std::endl;
        return;
    }
    
    if (!confirm("Remove package \"" + pkg + "\"?", true)) return;
    
    std::string orphans = exec("pacman -Qtdq 2>/dev/null");
    bool remove_orphans = false;
    
    if (!orphans.empty()) {
        remove_orphans = confirm("Remove orphan dependencies?", true);
    }
    
    std::string cmd = "sudo pacman -R";
    if (remove_orphans) cmd += "ns";
    cmd += " --noconfirm " + pkg;
    
    show_progress(cmd, "Removing");
    std::cout << GREEN << "✓ Removal complete" << RESET << std::endl;
}

void update_rinse() {
    if (g_config.disable_auto_update) return;
    
    std::string rinse_path = trim(exec("command -v rinse 2>/dev/null"));
    if (rinse_path.empty()) return;
    
    if (exec("git -C /tmp ls-remote https://github.com/RousevGH/rinse 2>/dev/null").empty()) return;
    
    std::cout << CYAN << "Checking for rinse updates..." << RESET << std::endl;
    
    std::string temp_dir = "/tmp/rinse-update-" + std::to_string(time(nullptr));
    if (exec_status("git clone --depth 1 https://github.com/RousevGH/rinse " + temp_dir + " >/dev/null 2>&1") != 0) return;
    
    std::string source_file;
    if (fs::exists(temp_dir + "/rinse_latest.cpp")) source_file = temp_dir + "/rinse_latest.cpp";
    else if (fs::exists(temp_dir + "/rinse.cpp")) source_file = temp_dir + "/rinse.cpp";
    else {
        exec_status(("rm -rf " + temp_dir).c_str());
        return;
    }
    
    if (exec_status("g++ -std=c++17 -O3 " + source_file + " -o " + temp_dir + "/rinse 2>/dev/null") != 0) {
        exec_status(("rm -rf " + temp_dir).c_str());
        return;
    }
    
    std::string old_hash = trim(exec("md5sum " + rinse_path + " 2>/dev/null | cut -d' ' -f1"));
    std::string new_hash = trim(exec("md5sum " + temp_dir + "/rinse 2>/dev/null | cut -d' ' -f1"));
    
    if (old_hash == new_hash) {
        std::cout << GREEN << "✓ rinse is up to date" << RESET << std::endl;
        exec_status(("rm -rf " + temp_dir).c_str());
        return;
    }
    
    if (!g_dry_run && confirm("rinse update available. Install?", true)) {
        if (exec_status(("sudo cp " + temp_dir + "/rinse " + rinse_path + " && sudo chmod +x " + rinse_path).c_str()) == 0) {
            std::cout << GREEN << "✓ rinse updated successfully" << RESET << std::endl;
            send_notification("rinse updated to latest version");
        } else {
            std::cout << RED << "✗ Failed to update rinse" << RESET << std::endl;
        }
    } else if (g_dry_run) {
        std::cout << YELLOW << "[DRY RUN] Would update rinse" << RESET << std::endl;
    }
    
    exec_status(("rm -rf " + temp_dir).c_str());
}

void update_system() {
    update_rinse();
    
    std::string outdated = exec("pacman -Qu 2>/dev/null");
    if (outdated.empty()) {
        std::cout << GREEN << "✓ System is up to date" << RESET << std::endl;
        return;
    }
    
    std::vector<std::string> pkgs;
    std::istringstream iss(outdated);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) {
            pkgs.push_back(line.substr(0, line.find(' ')));
        }
    }
    
    std::cout << YELLOW << "Found " << pkgs.size() << " outdated package" 
              << (pkgs.size() == 1 ? "" : "s") << RESET << std::endl;
    
    if (!confirm("Update all?", true)) return;
    
    std::cout << CYAN << "\nUpdating official packages..." << RESET << std::endl;
    show_progress("sudo pacman -Syu --noconfirm", "Updating");
    
    if (check_command("yay")) {
        std::cout << CYAN << "Updating AUR packages..." << RESET << std::endl;
        show_progress("yay -Syu --noconfirm", "Updating");
    }
    
    std::cout << GREEN << "\n✓ Update complete" << RESET << std::endl;
    send_notification("System update complete");
}

void lookup_packages(const std::vector<std::string>& search_terms = {}) {
    if (search_terms.empty()) {
        std::string result = exec("pacman -Q");
        if (result.empty()) {
            std::cout << YELLOW << "No packages installed" << RESET << std::endl;
        } else {
            std::cout << result;
        }
    } else {
        std::vector<std::string> found;
        std::string all_pkgs = exec("pacman -Q");
        std::istringstream iss(all_pkgs);
        std::string line;
        
        while (std::getline(iss, line)) {
            std::string pkg_name = line.substr(0, line.find(' '));
            
            for (const auto& term : search_terms) {
                std::string lower_pkg = pkg_name;
                std::string lower_term = term;
                std::transform(lower_pkg.begin(), lower_pkg.end(), lower_pkg.begin(), ::tolower);
                std::transform(lower_term.begin(), lower_term.end(), lower_term.begin(), ::tolower);
                
                if (lower_pkg.find(lower_term) != std::string::npos) {
                    found.push_back(line);
                    break;
                }
            }
        }
        
        if (found.empty()) {
            std::cout << YELLOW << "No installed packages matching: ";
            for (size_t i = 0; i < search_terms.size(); i++) {
                std::cout << "\"" << search_terms[i] << "\"";
                if (i < search_terms.size() - 1) std::cout << ", ";
            }
            std::cout << RESET << std::endl;
        } else {
            std::cout << GREEN << "Found " << found.size() << " package" 
                      << (found.size() == 1 ? "" : "s") << ":" << RESET << std::endl;
            for (const auto& pkg : found) {
                std::cout << "  " << pkg << std::endl;
            }
        }
    }
}

void clean_cache() {
    std::cout << CYAN << "Cleaning package cache..." << RESET << std::endl;
    show_progress("sudo pacman -Sc --noconfirm", "Cleaning");
    
    if (check_command("yay")) {
        std::cout << CYAN << "Cleaning AUR cache..." << RESET << std::endl;
        show_progress("yay -Sc --noconfirm", "Cleaning");
    }
    
    std::string orphans = exec("pacman -Qtdq 2>/dev/null");
    if (!orphans.empty() && confirm("Remove orphan packages?", true)) {
        std::cout << CYAN << "Removing orphan packages..." << RESET << std::endl;
        show_progress("sudo pacman -Rns $(pacman -Qtdq) --noconfirm 2>/dev/null", "Removing");
    } else if (orphans.empty()) {
        std::cout << GREEN << "No orphan packages found" << RESET << std::endl;
    }
    
    std::cout << GREEN << "\n✓ Cache cleanup complete" << RESET << std::endl;
}

void install_file(const std::string& filepath) {
    if (!fs::exists(filepath)) {
        std::cerr << RED << "File not found: " << filepath << RESET << std::endl;
        return;
    }
    
    fs::path path(filepath);
    std::string abs_path = fs::absolute(path);
    
    if (!confirm("Installing from " + abs_path, true)) return;
    
    if (path.extension() == ".zst" || path.string().find(".pkg.tar") != std::string::npos) {
        show_progress("sudo pacman -U " + abs_path, "Installing");
        std::cout << GREEN << "✓ Installation complete" << RESET << std::endl;
    } else if (path.extension() == ".gz" && path.string().find(".tar.gz") != std::string::npos) {
        std::string name = path.stem().stem().string();
        std::string temp_dir = "/tmp/rinse-build-" + name;
        
        if (g_dry_run) {
            std::cout << YELLOW << "[DRY RUN] Would extract and build from " << abs_path << RESET << std::endl;
            return;
        }
        
        std::cout << CYAN << "Extracting source archive..." << RESET << std::endl;
        
        if (exec_status(("mkdir -p " + temp_dir + " && tar -xzf " + abs_path + " -C " + temp_dir).c_str()) != 0) {
            std::cerr << RED << "✗ Failed to extract archive" << RESET << std::endl;
            return;
        }
        
        std::string source_dir = temp_dir;
        std::string found_dir = trim(exec("find " + temp_dir + " -maxdepth 1 -type d | tail -1"));
        if (!found_dir.empty() && found_dir != temp_dir) {
            source_dir = found_dir;
        }
        
        std::cout << CYAN << "Building from source..." << RESET << std::endl;
        std::cout << YELLOW << "Note: This may take a while. Use --full-log to see build output." << RESET << std::endl;
        
        bool built = false;
        
        if (fs::exists(source_dir + "/CMakeLists.txt")) {
            std::cout << CYAN << "Detected CMake project" << RESET << std::endl;
            std::string cmake_cmd = "cd " + source_dir + " && mkdir -p build && cd build && cmake .. && make -j$(nproc)";
            built = (exec_status((g_full_log ? cmake_cmd : cmake_cmd + " > /dev/null 2>&1").c_str()) == 0);
            
            if (built) {
                std::cout << CYAN << "Installing built files..." << RESET << std::endl;
                if (exec_status(("cd " + source_dir + "/build && sudo make install").c_str()) == 0) {
                    std::cout << GREEN << "✓ Installation complete" << RESET << std::endl;
                } else {
                    std::cout << YELLOW << "Build succeeded but install failed" << RESET << std::endl;
                }
            }
        } else if (fs::exists(source_dir + "/configure")) {
            std::cout << CYAN << "Detected autotools project" << RESET << std::endl;
            std::string build_cmd = "cd " + source_dir + " && ./configure && make -j$(nproc)";
            built = (exec_status((g_full_log ? build_cmd : build_cmd + " > /dev/null 2>&1").c_str()) == 0);
            
            if (built) {
                std::cout << CYAN << "Installing built files..." << RESET << std::endl;
                if (exec_status(("cd " + source_dir + " && sudo make install").c_str()) == 0) {
                    std::cout << GREEN << "✓ Installation complete" << RESET << std::endl;
                }
            }
        } else if (fs::exists(source_dir + "/Makefile") || fs::exists(source_dir + "/makefile")) {
            std::cout << CYAN << "Detected Makefile project" << RESET << std::endl;
            std::string build_cmd = "cd " + source_dir + " && make -j$(nproc)";
            built = (exec_status((g_full_log ? build_cmd : build_cmd + " > /dev/null 2>&1").c_str()) == 0);
            
            if (built) {
                std::cout << CYAN << "Installing built files..." << RESET << std::endl;
                exec_status(("cd " + source_dir + " && sudo make install").c_str());
                std::cout << GREEN << "✓ Installation complete" << RESET << std::endl;
            }
        } else {
            std::string dest = "/opt/" + name;
            std::cout << YELLOW << "No build system detected. Extracting to " << dest << RESET << std::endl;
            if (exec_status(("sudo mkdir -p " + dest + " && sudo cp -r " + source_dir + "/* " + dest).c_str()) == 0) {
                std::cout << GREEN << "✓ Extracted to " << dest << RESET << std::endl;
            }
        }
        
        if (!built && (fs::exists(source_dir + "/CMakeLists.txt") || fs::exists(source_dir + "/configure") || 
                       fs::exists(source_dir + "/Makefile"))) {
            std::cerr << RED << "✗ Build failed" << RESET << std::endl;
            std::cerr << "Try --full-log or build manually in: " << source_dir << std::endl;
        }
        
        if (!g_keep && !g_config.keep_build) {
            exec_status(("rm -rf " + temp_dir).c_str());
        } else {
            std::cout << CYAN << "Build files kept in: " << temp_dir << RESET << std::endl;
        }
    } else {
        std::cerr << RED << "Unsupported file type" << RESET << std::endl;
    }
}

void show_outdated(const std::string& time_val) {
    int days = parse_time_value(time_val);
    std::cout << CYAN << "Finding packages not updated in " << days << " days..." << RESET << std::endl;
    
    std::string installed = exec("pacman -Q");
    std::istringstream iss(installed);
    std::string line;
    
    time_t now = time(nullptr);
    time_t threshold = now - (days * 86400);
    
    std::vector<std::string> outdated_pkgs;
    
    while (std::getline(iss, line)) {
        std::string pkg = line.substr(0, line.find(' '));
        std::string date = get_package_date_pacman(pkg);
        
        if (!date.empty()) {
            struct tm tm_date = {};
            if (strptime(date.c_str(), "%d %B %Y", &tm_date) || strptime(date.c_str(), "%Y-%m-%d", &tm_date)) {
                time_t pkg_time = mktime(&tm_date);
                if (pkg_time < threshold) {
                    outdated_pkgs.push_back(pkg + " (last updated: " + date + ")");
                }
            }
        }
    }
    
    if (outdated_pkgs.empty()) {
        std::cout << GREEN << "No packages found" << RESET << std::endl;
    } else {
        std::cout << YELLOW << "Found " << outdated_pkgs.size() << " outdated packages:" << RESET << std::endl;
        for (const auto& pkg : outdated_pkgs) {
            std::cout << "  " << pkg << std::endl;
        }
    }
}

void print_help() {
    std::cout << BOLD << "rinse" << RESET << " - Fast CLI frontend for pacman and AUR\n";
    std::cout << CYAN << "Version 1.0" << RESET << "\n\n";
    
    std::cout << BOLD << "USAGE:\n" << RESET;
    std::cout << "  rinse <package>...           Install one or more packages\n";
    std::cout << "  rinse <command> [options]    Run a specific command\n\n";
    
    std::cout << BOLD << "INSTALL COMMANDS:\n" << RESET;
    std::cout << "  rinse <pkg>...               Install packages from pacman or AUR\n";
    std::cout << "  rinse install <pkg>...       Same as above (explicit)\n";
    std::cout << "  rinse -S <pkg>...            pacman-style install\n";
    std::cout << "  rinse <file>                 Install from .pkg.tar.zst or .tar.gz file\n\n";
    
    std::cout << BOLD << "PACKAGE MANAGEMENT:\n" << RESET;
    std::cout << "  rinse update                 Update all packages (pacman + AUR)\n";
    std::cout << "  rinse upgrade                Alias for update\n";
    std::cout << "  rinse new                    Alias for update\n";
    std::cout << "  rinse -Syu                   pacman-style update\n";
    std::cout << "  rinse -Syyu                  Force database refresh + update\n\n";
    
    std::cout << "  rinse remove <pkg>           Remove a package\n";
    std::cout << "  rinse uninstall <pkg>        Alias for remove\n";
    std::cout << "  rinse rem <pkg>              Alias for remove\n";
    std::cout << "  rinse -R <pkg>               pacman-style remove\n";
    std::cout << "  rinse -Rs <pkg>              Remove with dependencies\n\n";
    
    std::cout << "  rinse clean                  Clean package cache and remove orphans\n";
    std::cout << "  rinse -Sc                    pacman-style cache clean\n";
    std::cout << "  rinse outdated               Show packages not updated recently\n\n";
    
    std::cout << BOLD << "QUERY COMMANDS:\n" << RESET;
    std::cout << "  rinse lookup [term]          List/search installed packages\n";
    std::cout << "  rinse check [term]           Alias for lookup\n";
    std::cout << "  rinse list [term]            Alias for lookup\n";
    std::cout << "  rinse search [term]          Alias for lookup\n";
    std::cout << "  rinse -Q [term]              pacman-style query\n";
    std::cout << "  rinse -Qs <term>             pacman-style search installed\n\n";
    
    std::cout << BOLD << "FLAGS:\n" << RESET;
    std::cout << "  --dry-run, -n, dry           Show what would be done without doing it\n";
    std::cout << "  -k, --keep                   Keep build files after AUR installation\n";
    std::cout << "  --time <value>               Set time threshold for outdated command\n";
    std::cout << "                               Examples: 5d (days), 3m (months), 2y (years)\n";
    std::cout << "  --full-log                   Show complete installation output\n";
    std::cout << "  -h, --help, -help, --h       Show this help message\n\n";
    
    std::cout << BOLD << "EXAMPLES:\n" << RESET;
    std::cout << "  rinse firefox                Install Firefox\n";
    std::cout << "  rinse -S firefox discord     Install multiple packages\n";
    std::cout << "  rinse remove neofetch        Remove a package\n";
    std::cout << "  rinse -Syu                   Update entire system\n";
    std::cout << "  rinse check fire             Search for 'fire' in installed packages\n";
    std::cout << "  rinse outdated --time 1y     Show packages not updated in 1 year\n";
    std::cout << "  rinse -n firefox             Dry run installation\n";
    std::cout << "  rinse ./package.pkg.tar.zst  Install from local file\n\n";
    
    std::cout << BOLD << "CONFIGURATION:\n" << RESET;
    std::cout << "  Config file: " << CYAN << "~/.config/rinse/rinse.conf" << RESET << "\n";
    std::cout << "  Options:\n";
    std::cout << "    keep_build = true|false           Keep AUR build files (default: false)\n";
    std::cout << "    notify = true|false               Send desktop notifications (default: true)\n";
    std::cout << "    disable_auto_update = true|false  Disable rinse self-updates (default: false)\n";
    std::cout << "    outdated_time = 6m                Default threshold for outdated command\n\n";
    
    std::cout << BOLD << "BEHAVIOR:\n" << RESET;
    std::cout << "  • Packages are checked in pacman first, then AUR\n";
    std::cout << "  • Official packages are installed before AUR packages\n";
    std::cout << "  • All pacman operations use a single call (no parallel runs)\n";
    std::cout << "  • yay is auto-installed if needed for AUR packages\n";
    std::cout << "  • Desktop notifications sent when notify=true in config\n\n";
    
    std::cout << BOLD << "SOURCE & ISSUES:\n" << RESET;
    std::cout << "  GitHub: " << CYAN << "https://github.com/RousevGH/rinse" << RESET << "\n";
}

int main(int argc, char* argv[]) {
    // Check if running as root
    if (geteuid() == 0) {
        std::cout << YELLOW << "Warning: rinse isn't meant to be run as sudo!" << RESET << std::endl;
        std::cout << "Continuing anyway...\n" << std::endl;
    }
    
    if (argc < 2) {
        print_help();
        return 0;
    }
    
    load_config();
    
    std::vector<std::string> args;
    std::string time_override = "";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--dry-run" || arg == "-n" || arg == "dry") {
            g_dry_run = true;
        } else if (arg == "-k" || arg == "--keep") {
            g_keep = true;
        } else if (arg == "--full-log") {
            g_full_log = true;
        } else if (arg == "--time" && i + 1 < argc) {
            time_override = argv[++i];
        } else if (arg == "--help" || arg == "-h" || arg == "-help" || arg == "--h" || arg == "help") {
            print_help();
            return 0;
        } else {
            args.push_back(arg);
        }
    }
    
    if (args.empty()) {
        print_help();
        return 0;
    }
    
    std::string cmd = args[0];
    
    if (cmd == "install" || cmd == "-S") {
        if (args.size() > 1) {
            install_packages(std::vector<std::string>(args.begin() + 1, args.end()));
        }
    } else if (cmd == "remove" || cmd == "uninstall" || cmd == "rem" || cmd == "-R" || cmd == "-Rs") {
        if (args.size() < 2) {
            std::cerr << RED << "Error: No package specified\n" << RESET;
            return 1;
        }
        remove_package(args[1]);
    } else if (cmd == "update" || cmd == "upgrade" || cmd == "new" || cmd == "-Syu" || cmd == "-Syyu") {
        update_system();
    } else if (cmd == "-Q" || cmd == "-Qs" || cmd == "lookup" || cmd == "check" || cmd == "list" || cmd == "search") {
        std::vector<std::string> search_terms(args.begin() + 1, args.end());
        lookup_packages(search_terms);
    } else if (cmd == "clean" || cmd == "-Sc") {
        clean_cache();
    } else if (cmd == "outdated") {
        show_outdated(time_override.empty() ? g_config.outdated_time : time_override);
    } else if (fs::exists(cmd)) {
        install_file(cmd);
    } else {
        bool looks_like_command = (cmd.find('-') == 0 || cmd.length() <= 3);
        
        if (looks_like_command && args.size() == 1) {
            std::cerr << RED << "Error: Unrecognized command '" << cmd << "'\n" << RESET;
            std::cerr << "Try 'rinse --help'\n";
            return 1;
        }
        
        install_packages(args);
    }
    
    return 0;
}
