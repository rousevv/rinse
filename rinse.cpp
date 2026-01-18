// rinse - Fast CLI frontend for pacman and AUR
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
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/ioctl.h>

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
    bool auto_update = true;
    std::string update_branch = "main";
    std::string outdated_time = "6m";
};

Config g_config;
bool g_dry_run = false;
bool g_keep = false;
bool g_full_log = false;
bool g_auto_confirm = false;

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

std::string sanitize_package(const std::string& input) {
    if (input.empty()) return "";

    std::string sanitized;
    sanitized.reserve(input.length());

    for (char c : input) {
        // Package names: alphanumeric, hyphens, underscores, dots, plus signs, forward slashes (for repo/pkg format)
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '+' || c == '/') {
            sanitized += c;
        }
        // Remove all shell metacharacters and other dangerous characters
        else if (c == ';' || c == '&' || c == '|' || c == '$' || c == '`' || c == '(' || c == ')' ||
                 c == '<' || c == '>' || c == '\n' || c == '\r' || c == '\t' || c == '\\' ||
                 c == '"' || c == '\'' || c == ' ' || c == '!') {
            continue;
        }
    }

    return sanitized;
}

std::string sanitize_path(const std::string& input) {
    if (input.empty()) return "";

    std::string sanitized;
    sanitized.reserve(input.length());

    for (char c : input) {
        // Paths: alphanumeric, forward slashes, dots, hyphens, underscores
        // Allow spaces but be careful - they need to be handled in shell commands
        if (std::isalnum(c) || c == '/' || c == '.' || c == '-' || c == '_' || c == ' ') {
            sanitized += c;
        }
        // Remove all shell metacharacters and other dangerous characters
        else if (c == ';' || c == '&' || c == '|' || c == '$' || c == '`' || c == '(' || c == ')' ||
                 c == '<' || c == '>' || c == '\n' || c == '\r' || c == '\t' || c == '\\' ||
                 c == '"' || c == '\'' || c == '!' || c == '+' || c == '*') {
            continue;
        }
    }

    return sanitized;
}

std::string sanitize_message(const std::string& input) {
    if (input.empty()) return "";

    std::string sanitized;
    sanitized.reserve(input.length() * 2);

    for (char c : input) {
        // Messages: allow most printable characters but escape shell metacharacters
        if (c == ';' || c == '&' || c == '|' || c == '$' || c == '`' || c == '(' || c == ')' ||
            c == '<' || c == '>' || c == '\n' || c == '\r' || c == '\t' || c == '\\' ||
            c == '"' || c == '\'') {
            // Remove dangerous shell metacharacters
            continue;
        }
        else {
            sanitized += c;
        }
    }

    return sanitized;
}

std::string sanitize_config(const std::string& input) {
    if (input.empty()) return "";

    std::string sanitized;
    sanitized.reserve(input.length());

    for (char c : input) {
        // Config values: alphanumeric, hyphens, underscores, dots, spaces
        // For branch names: main, experimental, etc.
        // For time values: 6m, 1y, 30d, etc.
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == ' ') {
            sanitized += c;
        }
        // Remove all shell metacharacters and other dangerous characters
        else if (c == ';' || c == '&' || c == '|' || c == '$' || c == '`' || c == '(' || c == ')' ||
                 c == '<' || c == '>' || c == '\n' || c == '\r' || c == '\t' || c == '\\' ||
                 c == '"' || c == '\'' || c == '/' || c == '+' || c == '!') {
            continue;
        }
    }

    return sanitized;
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

bool check_flatpak() {
    return check_command("flatpak");
}

std::string get_home() {
    const char* home = getenv("HOME");
    return home ? home : "/root";
}

void send_notification(const std::string& msg) {
    if (g_config.notify && check_command("notify-send")) {
        exec_status("notify-send 'rinse' '" + sanitize_message(msg) + "' 2>/dev/null");
    }
}

std::string get_installed_flatpak_id(const std::string& pkg) {
    if (!check_flatpak()) return "";

    // Use --columns=application to get only the app ID
    std::string result = exec("flatpak list --app --columns=application 2>/dev/null | grep -i '" + sanitize_package(pkg) + "' | head -1");

    if (!result.empty()) {
        result = trim(result);
        return sanitize_package(result);
    }

    return "";
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

    if (!fs::exists(config_path)) {
        // Create config directory
        fs::create_directories(get_home() + "/.config/rinse");

        // Try to download from GitHub
        std::string download_cmd = "curl -s https://raw.githubusercontent.com/Rousevv/rinse/main/rinse.conf -o " + config_path + " 2>/dev/null";
        if (exec_status(download_cmd) != 0) {
            // Create default config if download fails
            std::ofstream file(config_path);
            file << "# rinse configuration file\n\n";
            file << "# Keep build files after AUR installation\n";
            file << "# If true, build directories will be kept in /tmp for debugging\n";
            file << "keep_build = false\n\n";
            file << "# Send desktop notifications when operations complete\n";
            file << "# Requires notify-send to be installed\n";
            file << "notify = true\n\n";
            file << "# Automatically check for rinse updates on 'rinse update'\n";
            file << "# Set to false to disable self-updates\n";
            file << "auto_update = true\n\n";
            file << "# Branch to pull updates from (main or experimental)\n";
            file << "# Use 'experimental' to test bleeding-edge features\n";
            file << "update_branch = main\n\n";
            file << "# Default time threshold for 'rinse outdated' command\n";
            file << "# Format: Nd (days), Nm (months), Ny (years)\n";
            file << "outdated_time = 6m\n";
            file.close();
        }
    }

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
            else if (key == "auto_update") g_config.auto_update = (val == "true");
            else if (key == "update_branch") g_config.update_branch = sanitize_config(val);
            else if (key == "outdated_time") g_config.outdated_time = sanitize_config(val);
        }
    }
}

std::string get_package_date_pacman(const std::string& pkg) {
    std::string cmd = "pacman -Si " + sanitize_package(pkg) + " 2>/dev/null | grep 'Build Date' | cut -d: -f2-";
    return trim(exec(cmd));
}

std::string get_package_date_aur(const std::string& pkg) {
    std::string cmd = "curl -s 'https://aur.archlinux.org/rpc/?v=5&type=info&arg=" + sanitize_package(pkg) +
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
    return exec_status("pacman -Q " + sanitize_package(pkg) + " >/dev/null 2>&1") == 0;
}

bool is_outdated(const std::string& pkg) {
    return exec_status("pacman -Qu 2>/dev/null | grep -q '^" + sanitize_package(pkg) + " '") == 0;
}

bool package_in_pacman(const std::string& pkg) {
    return exec_status("pacman -Si " + sanitize_package(pkg) + " >/dev/null 2>&1") == 0;
}

std::string fuzzy_search_package(const std::string& query) {
    std::string all_pkgs = exec("pacman -Q");
    std::istringstream iss(all_pkgs);
    std::string line;

    std::vector<std::pair<std::string, int>> matches;

    while (std::getline(iss, line)) {
        std::string pkg_name = line.substr(0, line.find(' '));
        std::string lower_pkg = pkg_name;
        std::string lower_query = query;
        std::transform(lower_pkg.begin(), lower_pkg.end(), lower_pkg.begin(), ::tolower);
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

        if (lower_pkg.find(lower_query) != std::string::npos) {
            int score = 100 - std::abs((int)lower_pkg.length() - (int)lower_query.length());
            matches.push_back({pkg_name, score});
        }
    }

    if (!matches.empty()) {
        std::sort(matches.begin(), matches.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        return matches[0].first;
    }

    return "";
}

bool package_in_aur(const std::string& pkg) {
    return exec_status("curl -s 'https://aur.archlinux.org/rpc/?v=5&type=info&arg=" + sanitize_package(pkg) +
                      "' | grep -q '\"resultcount\":1'") == 0;
}

bool package_in_flatpak(const std::string& pkg) {
    if (!check_flatpak()) return false;
    std::string result = exec("flatpak search " + sanitize_package(pkg) + " 2>/dev/null | head -1");
    return !result.empty() && result.find("No matches") == std::string::npos && result.find("Application") == std::string::npos;
}

std::string search_flatpak(const std::string& pkg) {
    if (!check_flatpak()) return "";
    std::string result = exec("flatpak search " + sanitize_package(pkg) + " 2>/dev/null | head -5");
    return result;
}

std::string get_flatpak_package_id(const std::string& pkg) {
    if (!check_flatpak()) return "";
    std::string result = exec("flatpak search " + sanitize_package(pkg) + " 2>/dev/null | head -2 | tail -1");
    if (result.empty() || result.find("No matches") != std::string::npos) return "";

    // Extract the package ID (third column, typically format: org.example.App)
    std::istringstream iss(result);
    std::string name, description, package_id;

    // Skip first two columns (name and description) to get to package ID
    std::getline(iss, name, '\t');
    std::getline(iss, description, '\t');
    std::getline(iss, package_id, '\t');

    package_id = trim(package_id);

    // Sanitize the package ID before returning it
    return sanitize_package(package_id);
}
bool confirm(const std::string& prompt, bool default_yes = true) {
    if (g_auto_confirm) return true;

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

    if (response == "yes") {
        g_auto_confirm = true;
        return true;
    }

    if (response.empty()) return default_yes;
    return (response[0] == 'y' || response[0] == 'Y');
}

int get_terminal_width() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int width = w.ws_col > 0 ? w.ws_col : 80;
    return std::min(width, 120); // Cap at 120 chars
}

void draw_progress_bar(int percent, bool failed = false) {
    int width = get_terminal_width();
    int bar_width = width - 10;

    if (bar_width < 20) bar_width = 20;

    std::string center_text = failed ? "FAILED" : std::to_string(percent) + "%";
    int filled = failed ? bar_width : (percent * bar_width) / 100;

    int center_pos = bar_width / 2 - center_text.length() / 2;

    std::cout << "\r[";

    for (int i = 0; i < bar_width; i++) {
        if (i >= center_pos && i < center_pos + (int)center_text.length()) {
            std::cout << (failed ? RED : RESET) << BOLD << center_text[i - center_pos] << RESET;
        } else if (i < filled) {
            std::cout << (failed ? RED : GREEN) << "=";
        } else {
            std::cout << RED << "-";
        }
    }

    std::cout << RESET << "]" << std::flush;
}

void show_progress(const std::string& cmd, const std::string& action = "Processing") {
    if (g_dry_run) {
        std::cout << YELLOW << "[DRY RUN] Would execute: " << RESET << cmd << "\n";
        return;
    }

    if (g_full_log) {
        int status = system(cmd.c_str());
        if (status != 0) {
            std::cout << RED << "✗ Operation failed" << RESET << std::endl;
        }
        return;
    }

    // Pre-authenticate sudo to avoid password prompt during progress bar
    if (cmd.find("sudo") != std::string::npos) {
        system("sudo -v");
    }

    std::atomic<bool> done(false);
    std::atomic<int> exit_code(0);

    std::thread worker([&]() {
        std::string silent_cmd = cmd + " > /dev/null 2>&1";
        exit_code = system(silent_cmd.c_str());
        done = true;
    });

    // Small delay to let sudo authenticate before showing progress
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int percent = 0;
    auto start = std::chrono::steady_clock::now();

    while (!done) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

        percent = std::min(95, (int)(elapsed * 95 / 10000));
        draw_progress_bar(percent);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    worker.join();

    if (exit_code != 0) {
        draw_progress_bar(100, true);
        std::cout << std::endl;
        std::cout << RED << "Running with output for debugging:" << RESET << std::endl;
        system(cmd.c_str());
    } else {
        draw_progress_bar(100);
        std::cout << std::endl;
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
    std::vector<std::string> pacman_pkgs, aur_pkgs, flatpak_pkgs, not_found;

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
            // Check for installed packages with fuzzy match
            std::string fuzzy = fuzzy_search_package(pkg);
            if (!fuzzy.empty() && is_installed(fuzzy)) {
                std::cout << YELLOW << "Package \"" << pkg << "\" not found, but \"" << fuzzy
                         << "\" is already installed." << RESET << std::endl;
            } else {
                // Check pacman repos for fuzzy match
                std::string search_result = exec("pacman -Ss '^" + sanitize_package(pkg) + "' 2>/dev/null | head -1");
                if (!search_result.empty()) {
                    std::string suggested = search_result.substr(0, search_result.find(' '));
                    if (suggested.find('/') != std::string::npos) {
                        suggested = suggested.substr(suggested.find('/') + 1);
                    }
                    std::cout << YELLOW << "Package \"" << pkg << "\" not found. Did you mean \""
                             << suggested << "\"?" << RESET << std::endl;
                    if (confirm("Install \"" + suggested + "\" instead?", false)) {
                        pacman_pkgs.push_back(suggested);
                    }
                } else {
                    // Package not found in pacman or AUR, prompt for Flatpak search
                    std::cout << YELLOW << "Package \"" << pkg << "\" not found on pacman or the AUR." << RESET << std::endl;
                    if (confirm("Search on Flatpak?", true)) {
                        if (check_flatpak()) {
                            std::string flatpak_results = search_flatpak(pkg);
                            if (!flatpak_results.empty() && flatpak_results.find("No matches") == std::string::npos &&
                                flatpak_results.find("Application") == std::string::npos) {
                                std::cout << CYAN << "Found on Flatpak:" << RESET << std::endl;
                                std::cout << flatpak_results << std::endl;
                                std::string flatpak_id = get_flatpak_package_id(pkg);
                                if (!flatpak_id.empty() && confirm("Install from Flatpak? (package id: " + flatpak_id + ") [Y/n]", true)) {
                                    flatpak_pkgs.push_back(flatpak_id);
                                }
                            } else {
                                std::cout << RED << "Package \"" << pkg << "\" not found on Flatpak either." << RESET << std::endl;
                                std::cout << "If the package is a .tar.gz file you want to install, run \"rinse <path/to/file>\"" << std::endl;
                            }
                        } else {
                            std::cout << YELLOW << "Flatpak is not installed. Install it first with: rinse flatpak" << RESET << std::endl;
                        }
                    }
                }
            }
        }
    }

    if (!pacman_pkgs.empty()) {
        std::cout << CYAN << "\nInstalling from official repos..." << RESET << std::endl;
        std::string cmd = "sudo pacman -S --noconfirm";
        for (const auto& pkg : pacman_pkgs) cmd += " " + sanitize_package(pkg);
        show_progress(cmd, "Installing");
    }

    if (!aur_pkgs.empty()) {
        ensure_yay();
        std::cout << CYAN << "\nInstalling from AUR..." << RESET << std::endl;
        std::string cmd = "yay -S --noconfirm";
        for (const auto& pkg : aur_pkgs) cmd += " " + sanitize_package(pkg);
        show_progress(cmd, "Installing");
    }

    if (!flatpak_pkgs.empty()) {
        if (!check_flatpak()) {
            std::cout << YELLOW << "\nFlatpak is not installed. Installing flatpak first..." << RESET << std::endl;
            if (confirm("Install flatpak?", true)) {
                show_progress("sudo pacman -S --noconfirm flatpak", "Installing");
            } else {
                std::cout << RED << "Cannot install Flatpak packages without flatpak" << RESET << std::endl;
                return;
            }
        }
        std::cout << CYAN << "\nInstalling from Flatpak..." << RESET << std::endl;
        for (const auto& pkg : flatpak_pkgs) {
            std::string cmd = "flatpak install -y flathub " + sanitize_package(pkg) + " --system";
            show_progress(cmd, "Installing");
        }
    }

    if (!pacman_pkgs.empty() || !aur_pkgs.empty() || !flatpak_pkgs.empty()) {
        std::cout << GREEN << "\n✓ Installation complete" << RESET << std::endl;
        send_notification("Package installation complete");
    }
}

void remove_package(const std::vector<std::string>& pkgs) {
    std::vector<std::string> to_remove;
    std::vector<std::string> flatpak_to_remove;

    for (const auto& pkg : pkgs) {
        if (!is_installed(pkg)) {
            std::string fuzzy = fuzzy_search_package(pkg);
            if (!fuzzy.empty()) {
                std::cout << YELLOW << "Package \"" << pkg << "\" is not installed, but a package called \""
                << fuzzy << "\" is." << RESET << std::endl;
                if (confirm("Did you mean \"" + fuzzy + "\"?", true)) {
                    to_remove.push_back(fuzzy);
                }
            } else {
                // Check if it's a Flatpak app
                std::string flatpak_id = get_installed_flatpak_id(pkg);
                if (!flatpak_id.empty()) {
                    std::cout << YELLOW << "Package \"" << pkg << "\" was not found as a package on your pc, but was found as a flatpak app ("
                    << flatpak_id << ")." << RESET << std::endl;
                    if (confirm("Remove " + sanitize_package(pkg) + "?", true)) {
                        flatpak_to_remove.push_back(flatpak_id);
                    }
                } else {
                    std::cerr << RED << "Package \"" << pkg << "\" is not installed" << RESET << std::endl;
                }
            }
            continue;
        }
        to_remove.push_back(pkg);
    }

    // Remove pacman packages
    if (!to_remove.empty()) {
        std::string pkg_list;
        for (size_t i = 0; i < to_remove.size(); i++) {
            pkg_list += to_remove[i];
            if (i < to_remove.size() - 1) pkg_list += ", ";
        }

        if (!confirm("Remove package" + std::string(to_remove.size() > 1 ? "s" : "") + " \"" + pkg_list + "\"?", true)) return;

        std::string orphans = exec("pacman -Qtdq 2>/dev/null");
        bool remove_orphans = false;

        if (!orphans.empty()) {
            remove_orphans = confirm("Remove orphan dependencies?", true);
        }

        std::string cmd = "sudo pacman -R";
        if (remove_orphans) cmd += "ns";
        cmd += " --noconfirm";
        for (const auto& pkg : to_remove) cmd += " " + sanitize_package(pkg);

        show_progress(cmd, "Removing");
    }

    // Remove Flatpak apps
    if (!flatpak_to_remove.empty()) {
        std::cout << CYAN << "\nRemoving Flatpak apps..." << RESET << std::endl;
        for (const auto& pkg : flatpak_to_remove) {
            std::string cmd = "flatpak uninstall -y " + sanitize_package(pkg);
            show_progress(cmd, "Removing");
        }
    }

    if (!to_remove.empty() || !flatpak_to_remove.empty()) {
        std::cout << GREEN << "\n✓ Removal complete" << RESET << std::endl;
        send_notification("Package removal complete");
    }
}
void update_rinse() {
    if (!g_config.auto_update) return;

    std::string rinse_path = trim(exec("command -v rinse 2>/dev/null"));
    if (rinse_path.empty()) return;

    std::cout << CYAN << "Checking for rinse updates..." << RESET << std::flush;

    // Get remote latest commit hash (fast, no cloning)
    std::string branch = sanitize_config(g_config.update_branch);
    std::string remote_hash = trim(exec("git ls-remote https://github.com/Rousevv/rinse refs/heads/" + branch + " 2>/dev/null | cut -f1"));

    if (remote_hash.empty() || remote_hash.length() < 7) {
        std::cout << "\r" << YELLOW << "Could not check for updates" << RESET << std::string(30, ' ') << std::endl;
        return;
    }

    // Check if we have a stored version
    std::string version_file = get_home() + "/.config/rinse/.version";
    std::string local_hash = "";

    if (fs::exists(version_file)) {
        std::ifstream vf(version_file);
        std::getline(vf, local_hash);
        local_hash = trim(local_hash);
    }

    // Compare first 7 chars of commit hash
    std::string remote_short = remote_hash.substr(0, 7);
    std::string local_short = local_hash.length() >= 7 ? local_hash.substr(0, 7) : "";

    if (remote_short == local_short && !local_short.empty()) {
        std::cout << "\r" << GREEN << "✓ rinse is up to date" << RESET << std::string(30, ' ') << std::endl;
        return;
    }

    std::cout << "\r" << std::string(50, ' ') << "\r";  // Clear line

    if (!confirm("rinse update available (" + remote_short + "). Install?", true)) return;

    std::cout << CYAN << "Starting update in background..." << RESET << std::endl;
    std::cout << YELLOW << "rinse will exit now. The update will complete shortly." << RESET << std::endl;

    // Save the target version hash before exiting
    std::ofstream vf(version_file);
    vf << remote_hash;
    vf.close();

    // Run the install script in background after this process exits
    // Use nohup and redirect all output to /dev/null for silent operation
    std::string install_cmd = "(sleep 0.5; curl -sSL https://raw.githubusercontent.com/rousevv/rinse/" +
    sanitize_config(branch) +
    "/install.sh | bash >/dev/null 2>&1; " +
    "notify-send 'rinse' 'Update complete' 2>/dev/null) &";

    system(install_cmd.c_str());

    // Exit immediately so the binary can be replaced
    std::cout << GREEN << "✓ Update initiated" << RESET << std::endl;
    exit(0);
}

void update_system() {

    std::string outdated = exec("pacman -Qu 2>/dev/null");
    if (outdated.empty()) {
        std::cout << GREEN << "✓ System is up to date" << RESET << std::endl;
        update_rinse();
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

    update_rinse();
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
        show_progress("sudo pacman -U " + sanitize_path(abs_path), "Installing");
        std::cout << GREEN << "✓ Installation complete" << RESET << std::endl;
    } else if (path.extension() == ".gz" && path.string().find(".tar.gz") != std::string::npos) {
        std::string name = sanitize_path(path.stem().stem().string());
        std::string temp_dir = "/tmp/rinse-build-" + name;

        if (g_dry_run) {
            std::cout << YELLOW << "[DRY RUN] Would extract and build from " << abs_path << RESET << std::endl;
            return;
        }

        std::cout << CYAN << "Extracting source archive..." << RESET << std::endl;

        if (exec_status(("mkdir -p " + sanitize_path(temp_dir) + " && tar -xzf " + sanitize_path(abs_path) + " -C " + sanitize_path(temp_dir)).c_str()) != 0) {
            std::cerr << RED << "✗ Failed to extract archive" << RESET << std::endl;
            return;
        }

        std::string source_dir = temp_dir;
        std::string found_dir = trim(exec("find " + sanitize_path(temp_dir) + " -maxdepth 1 -type d | tail -1"));
        if (!found_dir.empty() && found_dir != temp_dir) {
            source_dir = found_dir;
        }

        std::cout << CYAN << "Building from source..." << RESET << std::endl;
        std::cout << YELLOW << "Note: This may take a while. Use --full-log to see build output." << RESET << std::endl;

        bool built = false;

        if (fs::exists(source_dir + "/CMakeLists.txt")) {
            std::cout << CYAN << "Detected CMake project" << RESET << std::endl;
            std::string cmake_cmd = "cd " + sanitize_path(source_dir) + " && mkdir -p build && cd build && cmake .. && make -j$(nproc)";
            built = (exec_status((g_full_log ? cmake_cmd : cmake_cmd + " > /dev/null 2>&1").c_str()) == 0);

            if (built) {
                std::cout << CYAN << "Installing built files..." << RESET << std::endl;
                if (exec_status(("cd " + sanitize_path(source_dir) + "/build && sudo make install").c_str()) == 0) {
                    std::cout << GREEN << "✓ Installation complete" << RESET << std::endl;
                } else {
                    std::cout << YELLOW << "Build succeeded but install failed" << RESET << std::endl;
                }
            }
        } else if (fs::exists(source_dir + "/configure")) {
            std::cout << CYAN << "Detected autotools project" << RESET << std::endl;
            std::string build_cmd = "cd " + sanitize_path(source_dir) + " && ./configure && make -j$(nproc)";
            built = (exec_status((g_full_log ? build_cmd : build_cmd + " > /dev/null 2>&1").c_str()) == 0);

            if (built) {
                std::cout << CYAN << "Installing built files..." << RESET << std::endl;
                if (exec_status(("cd " + sanitize_path(source_dir) + " && sudo make install").c_str()) == 0) {
                    std::cout << GREEN << "✓ Installation complete" << RESET << std::endl;
                }
            }
        } else if (fs::exists(source_dir + "/Makefile") || fs::exists(source_dir + "/makefile")) {
            std::cout << CYAN << "Detected Makefile project" << RESET << std::endl;
            std::string build_cmd = "cd " + sanitize_path(source_dir) + " && make -j$(nproc)";
            built = (exec_status((g_full_log ? build_cmd : build_cmd + " > /dev/null 2>&1").c_str()) == 0);

            if (built) {
                std::cout << CYAN << "Installing built files..." << RESET << std::endl;
                exec_status(("cd " + sanitize_path(source_dir) + " && sudo make install").c_str());
                std::cout << GREEN << "✓ Installation complete" << RESET << std::endl;
            }
        } else {
            std::string dest = "/opt/" + name;
            std::cout << YELLOW << "No build system detected. Extracting to " << dest << RESET << std::endl;
            if (exec_status(("sudo mkdir -p " + sanitize_path(dest) + " && sudo cp -r " + sanitize_path(source_dir) + "/* " + sanitize_path(dest)).c_str()) == 0) {
                std::cout << GREEN << "✓ Extracted to " << dest << RESET << std::endl;
            }
        }

        if (!built && (fs::exists(source_dir + "/CMakeLists.txt") || fs::exists(source_dir + "/configure") ||
                       fs::exists(source_dir + "/Makefile"))) {
            std::cerr << RED << "✗ Build failed" << RESET << std::endl;
            std::cerr << "Try --full-log or build manually in: " << source_dir << std::endl;
        }

        if (!g_keep && !g_config.keep_build) {
            exec_status(("rm -rf " + sanitize_path(temp_dir)).c_str());
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
    std::cout << CYAN << "Version 0.3.0" << RESET << "\n\n";
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

    std::cout << "  rinse remove <pkg>...        Remove one or more packages\n";
    std::cout << "  rinse uninstall <pkg>...     Alias for remove\n";
    std::cout << "  rinse rem <pkg>...           Alias for remove\n";
    std::cout << "  rinse -R <pkg>...            pacman-style remove\n";
    std::cout << "  rinse -Rs <pkg>...           Remove with dependencies\n\n";

    std::cout << "  rinse clean                  Clean package cache and remove orphans\n";
    std::cout << "  rinse -Sc                    pacman-style cache clean\n";
    std::cout << "  rinse outdated               Show packages not updated recently\n\n";

    std::cout << BOLD << "QUERY COMMANDS:\n" << RESET;
    std::cout << "  rinse lookup [term]...       List/search installed packages\n";
    std::cout << "  rinse check [term]...        Alias for lookup\n";
    std::cout << "  rinse list [term]...         Alias for lookup\n";
    std::cout << "  rinse search [term]...       Alias for lookup\n";
    std::cout << "  rinse -Q [term]...           pacman-style query\n";
    std::cout << "  rinse -Qs <term>...          pacman-style search installed\n\n";

    std::cout << BOLD << "FLAGS:\n" << RESET;
    std::cout << "  --dry-run, -n, dry           Show what would be done without doing it\n";
    std::cout << "  -y, --yes                    Auto-confirm all prompts (skip confirmations)\n";
    std::cout << "  -k, --keep                   Keep build files after AUR installation\n";
    std::cout << "  --time <value>               Set time threshold for outdated command\n";
    std::cout << "                               Examples: 5d (days), 3m (months), 2y (years)\n";
    std::cout << "  --full-log                   Show complete installation output\n";
    std::cout << "  -h, --help, -help, --h       Show this help message\n\n";

    std::cout << BOLD << "EXAMPLES:\n" << RESET;
    std::cout << "  rinse firefox                Install Firefox\n";
    std::cout << "  rinse -y firefox discord     Install multiple packages (auto-confirm)\n";
    std::cout << "  rinse remove neofetch vim    Remove multiple packages\n";
    std::cout << "  rinse -Syu                   Update entire system\n";
    std::cout << "  rinse check fire             Search for 'fire' in installed packages\n";
    std::cout << "  rinse outdated --time 1y     Show packages not updated in 1 year\n";
    std::cout << "  rinse -n firefox             Dry run installation\n";
    std::cout << "  rinse ./package.pkg.tar.zst  Install from local file\n";
    std::cout << "  rinse clean -y               Clean cache (auto-confirm)\n\n";

    std::cout << BOLD << "TIPS:\n" << RESET;
    std::cout << "  • Type 'yes' during prompts to auto-confirm remaining operations\n";
    std::cout << "  • Use -y flag to skip all confirmations: rinse -y update\n";
    std::cout << "  • Fuzzy search suggests similar packages when not found\n";
    std::cout << "  • Progress bars show red 'FAILED' text when operations fail\n";
    std::cout << "  • Config file: ~/.config/rinse/rinse.conf\n\n";

    std::cout << BOLD << "CONFIGURATION:\n" << RESET;
    std::cout << "  Config file: " << CYAN << "~/.config/rinse/rinse.conf" << RESET << "\n";
    std::cout << "  Options:\n";
    std::cout << "    keep_build = true|false           Keep AUR build files (default: false)\n";
    std::cout << "    notify = true|false               Send desktop notifications (default: true)\n";
    std::cout << "    auto_update = true|false          Auto-check for rinse updates (default: true)\n";
    std::cout << "    update_branch = main|experimental Update branch (default: main)\n";
    std::cout << "    outdated_time = 6m                Default threshold for outdated command\n\n";

    std::cout << BOLD << "BEHAVIOR:\n" << RESET;
    std::cout << "  • Packages are checked in pacman first, then AUR\n";
    std::cout << "  • Official packages are installed before AUR packages\n";
    std::cout << "  • All pacman operations use a single call (no parallel runs)\n";
    std::cout << "  • yay is auto-installed if needed for AUR packages\n";
    std::cout << "  • Desktop notifications sent when notify=true in config\n";
    std::cout << "  • Sudo password requested once, then cached for operations\n\n";

    std::cout << BOLD << "SOURCE & ISSUES:\n" << RESET;
    std::cout << "  GitHub: " << CYAN << "https://github.com/Rousevv/rinse" << RESET << "\n";
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
        } else if (arg == "-y" || arg == "--yes") {
            g_auto_confirm = true;
        } else if (arg == "--time" && i + 1 < argc) {
            time_override = sanitize_config(argv[++i]);
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
        remove_package(std::vector<std::string>(args.begin() + 1, args.end()));
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
