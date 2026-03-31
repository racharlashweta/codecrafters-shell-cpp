#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>

namespace fs = std::filesystem;

// --- TOKENIZER: Handles Single Quotes, Double Quotes, and Backslashes ---
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
                    i++;
                } else {
                    current_arg += c;
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

// --- PATH SEARCH: Checks for existence AND executable permissions ---
std::string get_path(std::string command) {
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    std::stringstream ss(path_env);
    std::string path_dir;
    
    while (std::getline(ss, path_dir, ':')) {
        fs::path p = fs::path(path_dir) / command;
        if (fs::exists(p)) {
            auto perms = fs::status(p).permissions();
            // Ensure the file is actually executable by someone
            if ((perms & fs::perms::owner_exec) != fs::perms::none ||
                (perms & fs::perms::group_exec) != fs::perms::none ||
                (perms & fs::perms::others_exec) != fs::perms::none) {
                return p.string();
            }
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

        // --- REDIRECTION PARSING ---
        std::string stdout_file = "";
        std::string stderr_file = "";
        int redirect_idx = -1;

        for (int i = 0; i < (int)args.size(); ++i) {
            if (args[i] == ">" || args[i] == "1>") {
                if (i + 1 < (int)args.size()) {
                    stdout_file = args[i + 1];
                    redirect_idx = i;
                    break;
                }
            } else if (args[i] == "2>") {
                if (i + 1 < (int)args.size()) {
                    stderr_file = args[i + 1];
                    redirect_idx = i;
                    break;
                }
            }
        }

        // Clean up command arguments
        std::vector<std::string> cmd_args = args;
        if (redirect_idx != -1) {
            cmd_args.erase(cmd_args.begin() + redirect_idx, cmd_args.end());
        }

        std::string command = cmd_args[0];

        // --- PRE-EXECUTION: Ensure redirection files exist ---
        if (!stdout_file.empty()) { std::ofstream out(stdout_file); }
        if (!stderr_file.empty()) { std::ofstream err(stderr_file); }

        // --- BUILTINS ---
        if (command == "exit") {
            return 0;
        } else if (command == "echo") {
            std::ostream* out = &std::cout;
            std::ofstream file_out;
            if (!stdout_file.empty()) {
                file_out.open(stdout_file);
                out = &file_out;
            }
            for (size_t i = 1; i < cmd_args.size(); ++i) {
                *out << cmd_args[i] << (i == cmd_args.size() - 1 ? "" : " ");
            }
            *out << "\n";
        } else if (command == "pwd") {
            if (!stdout_file.empty()) {
                std::ofstream out(stdout_file);
                out << fs::current_path().string() << "\n";
            } else {
                std::cout << fs::current_path().string() << "\n";
            }
        } else if (command == "cd") {
            std::string path = cmd_args.size() > 1 ? cmd_args[1] : "";
            if (path == "~") path = std::getenv("HOME");
            if (fs::exists(path) && fs::is_directory(path)) fs::current_path(path);
            else std::cout << "cd: " << path << ": No such file or directory\n";
        } else if (command == "type") {
            std::string target = cmd_args[1];
            if (builtins.count(target)) {
                std::cout << target << " is a shell builtin\n";
            } else {
                std::string p = get_path(target);
                if (!p.empty()) std::cout << target << " is " << p << "\n";
                else std::cout << target << ": not found\n";
            }
        } 
        // --- EXTERNAL PROGRAMS ---
        else {
            std::string full_path = get_path(command);
            if (!full_path.empty()) {
                pid_t pid = fork();
                if (pid == 0) { // Child
                    if (!stdout_file.empty()) {
                        int fd = open(stdout_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        dup2(fd, STDOUT_FILENO);
                        close(fd);
                    }
                    if (!stderr_file.empty()) {
                        int fd = open(stderr_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        dup2(fd, STDERR_FILENO);
                        close(fd);
                    }
                    std::vector<char*> c_args;
                    for (auto& arg : cmd_args) c_args.push_back(&arg[0]);
                    c_args.push_back(nullptr);
                    execvp(c_args[0], c_args.data());
                    exit(1);
                } else { // Parent
                    wait(nullptr);
                }
            } else {
                std::cout << command << ": command not found\n";
            }
        }
    }
    return 0;
}