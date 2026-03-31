#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h> // For open() and O_WRONLY

namespace fs = std::filesystem;

// (Keep your parse_arguments and get_path functions from the previous stage)
std::vector<std::string> parse_arguments(const std::string& input) {
    std::vector<std::string> args;
    std::string current_arg;
    bool in_single_quotes = false;
    bool in_double_quotes = false;
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (c == '\\' && !in_single_quotes && !in_double_quotes) {
            if (i + 1 < input.length()) current_arg += input[++i];
            continue;
        }
        if (c == '\\' && in_double_quotes) {
            if (i + 1 < input.length()) {
                char next = input[i + 1];
                if (next == '\"' || next == '\\' || next == '$') { current_arg += next; i++; }
                else current_arg += c;
            } else current_arg += c;
            continue;
        }
        if (c == '\'' && !in_double_quotes) in_single_quotes = !in_single_quotes;
        else if (c == '\"' && !in_single_quotes) in_double_quotes = !in_double_quotes;
        else if (c == ' ' && !in_single_quotes && !in_double_quotes) {
            if (!current_arg.empty()) { args.push_back(current_arg); current_arg.clear(); }
        } else current_arg += c;
    }
    if (!current_arg.empty()) args.push_back(current_arg);
    return args;
}

std::string get_path(std::string command) {
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    std::stringstream ss(path_env);
    std::string path_dir;
    while (std::getline(ss, path_dir, ':')) {
        fs::path p = fs::path(path_dir) / command;
        if (fs::exists(p)) return p.string();
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

        // --- NEW: REDIRECTION PARSING ---
        std::string redirect_file = "";
        int redirect_idx = -1;

        for (int i = 0; i < args.size(); ++i) {
            if (args[i] == ">" || args[i] == "1>") {
                if (i + 1 < args.size()) {
                    redirect_file = args[i + 1];
                    redirect_idx = i;
                    break;
                }
            }
        }

        // Remove redirection tokens from arguments so the command doesn't see them
        std::vector<std::string> cmd_args = args;
        if (redirect_idx != -1) {
            cmd_args.erase(cmd_args.begin() + redirect_idx, cmd_args.end());
        }

        std::string command = cmd_args[0];

        // Builtins (Updated to use cmd_args)
        if (command == "exit") return 0;
        else if (command == "echo") {
            // Check if we need to redirect builtin output
            std::streambuf* old_cout = nullptr;
            std::ofstream out_file;
            if (!redirect_file.empty()) {
                out_file.open(redirect_file);
                old_cout = std::cout.rdbuf(out_file.rdbuf());
            }

            for (size_t i = 1; i < cmd_args.size(); ++i) {
                std::cout << cmd_args[i] << (i == cmd_args.size() - 1 ? "" : " ");
            }
            std::cout << "\n";

            if (old_cout) std::cout.rdbuf(old_cout); // Restore
        }
        else if (command == "pwd") {
             if (!redirect_file.empty()) {
                std::ofstream out(redirect_file);
                out << fs::current_path().string() << "\n";
             } else std::cout << fs::current_path().string() << "\n";
        }
        else if (command == "cd") { /* Same cd logic as before */ }
        else if (command == "type") { /* Same type logic as before using cmd_args */ }
        // --- EXTERNAL PROGRAMS WITH REDIRECTION ---
        else {
            std::string full_path = get_path(command);
            if (!full_path.empty()) {
                pid_t pid = fork();
                if (pid == 0) { // Child
                    if (!redirect_file.empty()) {
                        // Open file: Write only, create if missing, truncate if exists
                        int fd = open(redirect_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        dup2(fd, STDOUT_FILENO); // Redirect stdout (1) to file
                        close(fd);
                    }
                    std::vector<char*> c_args;
                    for (auto& arg : cmd_args) c_args.push_back(&arg[0]);
                    c_args.push_back(nullptr);
                    execvp(c_args[0], c_args.data());
                    exit(1);
                } else wait(nullptr);
            } else std::cout << command << ": command not found\n";
        }
    }
    return 0;
}