#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

// --- FINAL TOKENIZER: Handles Single, Double, and Picky Backslashes ---
std::vector<std::string> parse_arguments(const std::string& input) {
    std::vector<std::string> args;
    std::string current_arg;
    bool in_single_quotes = false;
    bool in_double_quotes = false;

    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];

        // 1. Backslash OUTSIDE of quotes (Escape everything)
        if (c == '\\' && !in_single_quotes && !in_double_quotes) {
            if (i + 1 < input.length()) {
                current_arg += input[++i];
            }
            continue;
        }

        // 2. Backslash INSIDE Double Quotes (Picky Escape)
        if (c == '\\' && in_double_quotes) {
            if (i + 1 < input.length()) {
                char next = input[i + 1];
                if (next == '\"' || next == '\\' || next == '$') {
                    current_arg += next;
                    i++; // Skip the escaped character
                } else {
                    current_arg += c; // Keep the backslash as literal
                }
            } else {
                current_arg += c;
            }
            continue;
        }

        // 3. Handle Quote Toggles
        if (c == '\'' && !in_double_quotes) {
            in_single_quotes = !in_single_quotes;
        } else if (c == '\"' && !in_single_quotes) {
            in_double_quotes = !in_double_quotes;
        } 
        // 4. Handle Spaces (Delimiters)
        else if (c == ' ' && !in_single_quotes && !in_double_quotes) {
            if (!current_arg.empty()) {
                args.push_back(current_arg);
                current_arg.clear();
            }
        } 
        // 5. Normal characters
        else {
            current_arg += c;
        }
    }
    if (!current_arg.empty()) args.push_back(current_arg);
    return args;
}

// Helper to find executable in PATH
std::string get_path(std::string command) {
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    std::stringstream ss(path_env);
    std::string path_dir;
    while (std::getline(ss, path_dir, ':')) {
        fs::path p = fs::path(path_dir) / command;
        if (fs::exists(p)) {
            auto perms = fs::status(p).permissions();
            if ((perms & fs::perms::owner_exec) != fs::perms::none) return p.string();
        }
    }
    return "";
}

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

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
        } else if (command == "echo") {
            for (size_t i = 1; i < args.size(); ++i) {
                std::cout << args[i] << (i == args.size() - 1 ? "" : " ");
            }
            std::cout << "\n";
        } else if (command == "pwd") {
            std::cout << fs::current_path().string() << "\n";
        } else if (command == "cd") {
            std::string path = args.size() > 1 ? args[1] : "";
            if (path == "~") {
                char* home = std::getenv("HOME");
                if (home) path = std::string(home);
            }
            if (fs::exists(path) && fs::is_directory(path)) fs::current_path(path);
            else std::cout << "cd: " << path << ": No such file or directory\n";
        } else if (command == "type") {
            std::string target = args[1];
            if (builtins.count(target)) std::cout << target << " is a shell builtin\n";
            else {
                std::string p = get_path(target);
                if (!p.empty()) std::cout << target << " is " << p << "\n";
                else std::cout << target << ": not found\n";
            }
        } else {
            std::string full_path = get_path(command);
            if (!full_path.empty()) {
                pid_t pid = fork();
                if (pid == 0) {
                    std::vector<char*> c_args;
                    for (auto& arg : args) c_args.push_back(&arg[0]);
                    c_args.push_back(nullptr);
                    execvp(c_args[0], c_args.data());
                    exit(1);
                } else {
                    wait(nullptr);
                }
            } else {
                std::cout << command << ": command not found\n";
            }
        }
    }
    return 0;
}