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

std::vector<std::string> builtins_list = {"echo", "exit", "type", "pwd", "cd"};

// --- DYNAMIC PATH SCANNER ---
// This fills a vector with all executable names found in the PATH
std::vector<std::string> get_all_executables(const std::string& prefix) {
    std::set<std::string> matches;
    
    // 1. Check builtins first
    for (const auto& b : builtins_list) {
        if (b.compare(0, prefix.length(), prefix) == 0) {
            matches.insert(b);
        }
    }

    // 2. Scan PATH
    char* path_env = std::getenv("PATH");
    if (path_env) {
        std::stringstream ss(path_env);
        std::string dir_path;
        while (std::getline(ss, dir_path, ':')) {
            try {
                if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) continue;

                for (const auto& entry : fs::directory_iterator(dir_path)) {
                    std::string filename = entry.path().filename().string();
                    if (filename.compare(0, prefix.length(), prefix) == 0) {
                        // Check if it's a regular file and is executable
                        auto perms = entry.status().permissions();
                        if (fs::is_regular_file(entry) && 
                           (perms & fs::perms::owner_exec) != fs::perms::none) {
                            matches.insert(filename);
                        }
                    }
                }
            } catch (...) {
                // Handle cases where directories might be restricted or vanish
                continue;
            }
        }
    }
    return std::vector<std::string>(matches.begin(), matches.end());
}

// --- UPDATED GENERATOR ---
char* command_generator(const char* text, int state) {
    static std::vector<std::string> current_matches;
    static size_t match_index;

    if (!state) {
        current_matches = get_all_executables(text);
        match_index = 0;
    }

    if (match_index < current_matches.size()) {
        char* res = (char*)malloc(current_matches[match_index].length() + 1);
        std::strcpy(res, current_matches[match_index++].c_str());
        return res;
    }

    return nullptr; // Triggers bell if no matches found
}

char** my_completion(const char* text, int start, int end) {
    if (start == 0) {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(text, command_generator);
    }
    return nullptr; 
}

// --- TOKENIZER & PATH SEARCH (Unchanged from previous successful stages) ---
std::vector<std::string> parse_arguments(const std::string& input) {
    std::vector<std::string> args;
    std::string current_arg;
    bool in_single_quotes = false, in_double_quotes = false;
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

std::string get_full_path(std::string command) {
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
    rl_attempted_completion_function = my_completion;
    rl_variable_bind("bell-style", "audible");

    while (true) {
        char* line = readline("$ ");
        if (!line) break; 
        
        std::string input(line);
        if (input.empty()) { free(line); continue; }
        add_history(line);

        std::vector<std::string> args = parse_arguments(input);
        free(line);

        if (args.empty()) continue;

        std::string command = args[0];
        if (command == "exit") return 0;
        else if (command == "echo") {
            for (size_t i = 1; i < args.size(); ++i) 
                std::cout << args[i] << (i == args.size() - 1 ? "" : " ");
            std::cout << "\n";
        }
        else if (command == "type") {
            std::string target = args[1];
            if (std::find(builtins_list.begin(), builtins_list.end(), target) != builtins_list.end())
                std::cout << target << " is a shell builtin\n";
            else {
                std::string p = get_full_path(target);
                if (!p.empty()) std::cout << target << " is " << p << "\n";
                else std::cout << target << ": not found\n";
            }
        }
        else if (command == "pwd") std::cout << fs::current_path().string() << "\n";
        else {
            std::string full_path = get_full_path(command);
            if (!full_path.empty()) {
                pid_t pid = fork();
                if (pid == 0) {
                    std::vector<char*> c_args;
                    for (auto& arg : args) c_args.push_back(&arg[0]);
                    c_args.push_back(nullptr);
                    execvp(c_args[0], c_args.data());
                    exit(1);
                } else wait(nullptr);
            } else std::cout << command << ": command not found\n";
        }
    }
    return 0;
}