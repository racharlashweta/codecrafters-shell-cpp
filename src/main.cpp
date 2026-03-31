#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

// Keep your get_path function here...
std::string get_path(std::string command) {
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    std::stringstream ss(path_env);
    std::string path;
    while (std::getline(ss, path, ':')) {
        fs::path full_path = fs::path(path) / command;
        if (fs::exists(full_path)) {
            auto perms = fs::status(full_path).permissions();
            if ((perms & fs::perms::owner_exec) != fs::perms::none) {
                return full_path.string();
            }
        }
    }
    return "";
}

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // Add 'cd' to the builtins
    std::set<std::string> builtins = {"exit", "echo", "type", "pwd", "cd"};

    while (true) {
        std::cout << "$ " << std::flush;

        std::string input;
        if (!std::getline(std::cin, input)) break;

        size_t space_pos = input.find(' ');
        std::string command = input.substr(0, space_pos);
        std::string argument = (space_pos != std::string::npos) ? input.substr(space_pos + 1) : "";

        if (command == "exit") {
            return 0;
        } 
        else if (command == "echo") {
            std::cout << argument << "\n";
        } 
        else if (command == "pwd") {
            std::cout << fs::current_path().string() << "\n";
        }
        // NEW: Handle the cd builtin
        else if (command == "cd") {
            std::string target_path = argument;
            
            // Handle tilde (home directory)
            if (target_path == "~") {
                char* home = std::getenv("HOME");
                if (home) target_path = std::string(home);
            }

            // Check if directory exists before changing
            if (fs::exists(target_path) && fs::is_directory(target_path)) {
                fs::current_path(target_path);
            } else {
                std::cout << "cd: " << argument << ": No such file or directory" << std::endl;
            }
        }
        else if (command == "type") {
            if (builtins.count(argument)) {
                std::cout << argument << " is a shell builtin" << std::endl;
            } else {
                std::string path = get_path(argument);
                if (!path.empty()) {
                    std::cout << argument << " is " << path << std::endl;
                } else {
                    std::cout << argument << ": not found" << std::endl;
                }
            }
        } 
        else {
            std::string full_path = get_path(command);
            if (!full_path.empty()) {
                std::system(input.c_str());
            } else {
                std::cout << command << ": command not found" << std::endl;
            }
        }
    }
    return 0;
}