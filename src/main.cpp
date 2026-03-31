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
#include <algorithm>
#include <cstring>

// Readline headers
#include <readline/readline.h>
#include <readline/history.h>

namespace fs = std::filesystem;

// List of builtins for autocompletion
std::vector<std::string> builtins_list = {"echo", "exit", "type", "pwd", "cd"};

// --- COMPLETION GENERATOR ---
char* command_generator(const char* text, int state) {
    static size_t list_index, len;
    std::string name(text);

    if (!state) {
        list_index = 0;
        len = name.length();
    }

    while (list_index < builtins_list.size()) {
        std::string cur = builtins_list[list_index++];
        if (cur.compare(0, len, name) == 0) {
            // Readline will free this memory
            char* res = (char*)malloc(cur.length() + 1);
            std::strcpy(res, cur.c_str());
            return res;
        }
    }
    return nullptr;
}

// --- COMPLETION HOOK ---
char** my_completion(const char* text, int start, int end) {
    // Only attempt completion if we are at the start of the line (the command)
    if (start == 0) {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(text, command_generator);
    }
    // Return nullptr for arguments to allow default filename completion 
    // or no completion at all.
    return nullptr; 
}

// --- TOKENIZER (Handles quotes and escapes) ---
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
                if (next == '\"' || next == '\\' || next == '$') {
                    current_arg += next; i++;
                } else current_arg += c;
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
        if (fs::exists(p)) {
            auto perms = fs::status(p).permissions();
            if ((perms & fs::perms::owner_exec) != fs::perms::none) return p.string();
        }
    }
    return "";
}

int main() {
    // Initialize Readline completion
    rl_attempted_completion_function = my_completion;

    while (true) {
        char* line = readline("$ ");
        if (!line) break; // Exit on Ctrl+D
        
        std::string input(line);
        if (input.empty()) {
            free(line);
            continue;
        }
        add_history(line);

        std::vector<std::string> args = parse_arguments(input);
        free(line);

        if (args.empty()) continue;

        // --- REDIRECTION PARSING (Combined logic) ---
        std::string stdout_file = "";
        std::string stderr_file = "";
        bool stdout_append = false;
        bool stderr_append = false;
        int redirect_idx = -1;

        for (int i = 0; i < (int)args.size(); ++i) {
            if (args[i] == ">" || args[i] == "1>") {
                stdout_file = args[i + 1]; stdout_append = false; redirect_idx = i; break;
            } else if (args[i] == ">>" || args[i] == "1>>") {
                stdout_file = args[i + 1]; stdout_append = true; redirect_idx = i; break;
            } else if (args[i] == "2>") {
                stderr_file = args[i + 1]; stderr_append = false; redirect_idx = i; break;
            } else if (args[i] == "2>>") {
                stderr_file = args[i + 1]; stderr_append = true; redirect_idx = i; break;
            }
        }

        std::vector<std::string> cmd_args = args;
        if (redirect_idx != -1) cmd_args.erase(cmd_args.begin() + redirect_idx, cmd_args.end());
        std::string command = cmd_args[0];

        // Ensure files exist for builtins/externals
        if (!stdout_file.empty()) { std::ofstream(stdout_file, std::ios::app); }
        if (!stderr_file.empty()) { std::ofstream(stderr_file, std::ios::app); }

        // --- EXECUTION ---
        if (command == "exit") return 0;
        else if (command == "echo") {
            std::ostream* out = &std::cout;
            std::ofstream file_out;
            if (!stdout_file.empty()) {
                file_out.open(stdout_file, stdout_append ? std::ios::app : std::ios::out);
                out = &file_out;
            }
            for (size_t i = 1; i < cmd_args.size(); ++i) {
                *out << cmd_args[i] << (i == cmd_args.size() - 1 ? "" : " ");
            }
            *out << "\n";
        }
        else if (command == "type") {
            std::string target = cmd_args[1];
            if (std::find(builtins_list.begin(), builtins_list.end(), target) != builtins_list.end()) {
                std::cout << target << " is a shell builtin\n";
            } else {
                std::string p = get_path(target);
                if (!p.empty()) std::cout << target << " is " << p << "\n";
                else std::cout << target << ": not found\n";
            }
        }
        else if (command == "pwd") {
            std::cout << fs::current_path().string() << "\n";
        }
        else if (command == "cd") {
            std::string path = cmd_args.size() > 1 ? cmd_args[1] : "";
            if (path == "~") path = std::getenv("HOME");
            if (fs::exists(path)) fs::current_path(path);
            else std::cout << "cd: " << path << ": No such file or directory\n";
        }
        else {
            std::string full_path = get_path(command);
            if (!full_path.empty()) {
                pid_t pid = fork();
                if (pid == 0) {
                    if (!stdout_file.empty()) {
                        int flags = O_WRONLY | O_CREAT | (stdout_append ? O_APPEND : O_TRUNC);
                        int fd = open(stdout_file.c_str(), flags, 0644);
                        dup2(fd, STDOUT_FILENO); close(fd);
                    }
                    if (!stderr_file.empty()) {
                        int flags = O_WRONLY | O_CREAT | (stderr_append ? O_APPEND : O_TRUNC);
                        int fd = open(stderr_file.c_str(), flags, 0644);
                        dup2(fd, STDERR_FILENO); close(fd);
                    }
                    std::vector<char*> c_args;
                    for (auto& arg : cmd_args) c_args.push_back(&arg[0]);
                    c_args.push_back(nullptr);
                    execvp(c_args[0], c_args.data());
                    exit(1);
                } else wait(nullptr);
            } else {
                std::cout << command << ": command not found\n";
            }
        }
    }
    return 0;
}