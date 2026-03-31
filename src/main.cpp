#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <filesystem> // For checking files
#include <cstdlib>    // For getenv

namespace fs = std::filesystem;

// Helper function to find a command in the PATH
std::string get_path(std::string command) {
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";

    std::stringstream ss(path_env);
    std::string path;
    
    // Split PATH by ':'
    while (std::getline(ss, path, ':')) {
        fs::path full_path = fs::path(path) / command;
        
        // Check if file exists and is executable
        if (fs::exists(full_path)) {
            // Check for execute permission
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

    std::set<std::string> builtins = {"exit", "echo", "type"};

    while (true) {
        std::cout << "$ ";
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
        else if (command == "type") {
            if (builtins.count(argument)) {
                std::cout << argument << " is a shell builtin" << std::endl;
            } else {
                // NEW: Search the PATH for the executable
                std::string path = get_path(argument);
                if (!path.empty()) {
                    std::cout << argument << " is " << path << std::endl;
                } else {
                    std::cout << argument << ": not found" << std::endl;
                }
            }
        } 
        else {
            std::cout << command << ": command not found" << std::endl;
        }
    }
    return 0;
}