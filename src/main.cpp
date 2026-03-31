#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

// Helper to find executable in PATH
std::string get_path(std::string command) {
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";

    std::stringstream ss(path_env);
    std::string path;
    while (std::getline(ss, path, ':')) {
        fs::path full_path = fs::path(path) / command;
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
        // NEW: Handle external programs
        else {
            std::string full_path = get_path(command);
            if (!full_path.empty()) {
                // Run the program using the full input string (command + args)
                std::system(input.c_str());
            } else {
                std::cout << command << ": command not found" << std::endl;
            }
        }
    }
    return 0;
}