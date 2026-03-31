#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

// --- NEW: The Tokenizer ---
std::vector<std::string> parse_arguments(const std::string& input) {
    std::vector<std::string> args;
    std::string current_arg;
    bool in_single_quotes = false;

    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];

        if (c == '\'' && !in_single_quotes) {
            in_single_quotes = true;
        } else if (c == '\'' && in_single_quotes) {
            in_single_quotes = false;
        } else if (c == ' ' && !in_single_quotes) {
            if (!current_arg.empty()) {
                args.push_back(current_arg);
                current_arg.clear();
            }
        } else {
            current_arg += c;
        }
    }
    if (!current_arg.empty()) args.push_back(current_arg);
    return args;
}

// Keep your get_path function...
std::string get_path(std::string command) {
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    std::stringstream ss(path_env);
    std::string path;
    while (std::getline(ss, path, ':')) {
        fs::path p = fs::path(path) / command;
        if (fs::exists(p)) return p.string();
    }
    return "";
}

int main() {
    std::cout << std::unitbuf;
    std::set<std::string> builtins = {"exit", "echo", "type", "pwd", "cd"};

    while (true) {
        std::cout << "$ " << std::flush;
        std::string input;
        if (!std::getline(std::cin, input)) break;

        std::vector<std::string> args = parse_arguments(input);
        if (args.empty()) continue;

        std::string command = args[0];

        if (command == "exit") {
            return 0;
        } 
        else if (command == "echo") {
            for (size_t i = 1; i < args.size(); ++i) {
                std::cout << args[i] << (i == args.size() - 1 ? "" : " ");
            }
            std::cout << "\n";
        } 
        else if (command == "pwd") {
            std::cout << fs::current_path().string() << "\n";
        }
        else if (command == "cd") {
            std::string path = args.size() > 1 ? args[1] : "";
            if (path == "~") path = std::getenv("HOME");
            if (fs::exists(path) && fs::is_directory(path)) fs::current_path(path);
            else std::cout << "cd: " << path << ": No such file or directory\n";
        }
        else if (command == "type") {
            std::string target = args[1];
            if (builtins.count(target)) std::cout << target << " is a shell builtin\n";
            else {
                std::string p = get_path(target);
                if (!p.empty()) std::cout << target << " is " << p << "\n";
                else std::cout << target << ": not found\n";
            }
        } 
        else {
            std::string full_path = get_path(command);
            if (!full_path.empty()) {
                // For external programs with quoted args, system() is tricky.
                // Re-constructing the string carefully:
                std::string exec_cmd = full_path;
                for (size_t i = 1; i < args.size(); ++i) {
                    exec_cmd += " \"" + args[i] + "\""; 
                }
                std::system(exec_cmd.c_str());
            } else {
                std::cout << command << ": command not found\n";
            }
        }
    }
    return 0;
}