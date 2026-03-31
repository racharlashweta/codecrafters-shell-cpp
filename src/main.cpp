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

#include <readline/readline.h>
#include <readline/history.h>

namespace fs = std::filesystem;

std::vector<std::string> builtins_list = {"echo", "exit", "type", "pwd", "cd"};

// --- COMMAND GENERATOR ---
std::vector<std::string> get_command_matches(const std::string& prefix) {
    std::set<std::string> matches;
    for (const auto& b : builtins_list) {
        if (b.compare(0, prefix.length(), prefix) == 0) matches.insert(b);
    }
    char* path_env = std::getenv("PATH");
    if (path_env) {
        std::stringstream ss(path_env);
        std::string dir_path;
        while (std::getline(ss, dir_path, ':')) {
            if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) continue;
            try {
                for (const auto& entry : fs::directory_iterator(dir_path)) {
                    std::string filename = entry.path().filename().string();
                    if (filename.compare(0, prefix.length(), prefix) == 0) {
                        auto perms = entry.status().permissions();
                        if (fs::is_regular_file(entry) && (perms & fs::perms::owner_exec) != fs::perms::none) {
                            matches.insert(filename);
                        }
                    }
                }
            } catch (...) { continue; }
        }
    }
    return std::vector<std::string>(matches.begin(), matches.end());
}

char* command_generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static size_t match_index;
    if (!state) {
        matches = get_command_matches(text);
        match_index = 0;
    }
    if (match_index < matches.size()) {
        char* res = (char*)malloc(matches[match_index].length() + 1);
        std::strcpy(res, matches[match_index++].c_str());
        return res;
    }
    return nullptr;
}

// --- UPDATED COMPLETION HOOK WITH BELL ---
char** my_completion(const char* text, int start, int end) {
    char** matches = nullptr;

    if (start == 0) {
        rl_attempted_completion_over = 1;
        matches = rl_completion_matches(text, command_generator);
    } else {
        rl_attempted_completion_over = 1; 
        matches = rl_completion_matches(text, rl_filename_completion_function);
    }

    // STAGE #VS5 FIX: If no matches found, trigger the bell
    if (!matches || !matches[0]) {
        rl_ding(); // This sends \x07 to stdout
        return nullptr;
    }

    // Handle trailing slash vs space logic from previous stages
    if (matches && matches[0] && !matches[1]) {
        std::string path(matches[0]);
        if (fs::exists(path) && fs::is_directory(path)) {
            rl_completion_append_character = '/';
            rl_completion_suppress_append = 1; 
        } else {
            rl_completion_append_character = ' ';
            rl_completion_suppress_append = 0;
        }
    }

    return matches;
}

// --- TOKENIZER ---
std::vector<std::string> parse_arguments(const std::string& input) {
    std::vector<std::string> args;
    std::string current;
    bool in_s_quote = false, in_d_quote = false;
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (c == '\\' && !in_s_quote && !in_d_quote) {
            if (i + 1 < input.length()) current += input[++i];
            continue;
        }
        if (c == '\\' && in_d_quote) {
            if (i + 1 < input.length()) {
                char n = input[i+1];
                if (n == '\"' || n == '\\' || n == '$') { current += n; i++; }
                else current += c;
            } else current += c;
            continue;
        }
        if (c == '\'' && !in_d_quote) in_s_quote = !in_s_quote;
        else if (c == '\"' && !in_s_quote) in_d_quote = !in_d_quote;
        else if (c == ' ' && !in_s_quote && !in_d_quote) {
            if (!current.empty()) { args.push_back(current); current.clear(); }
        } else current += c;
    }
    if (!current.empty()) args.push_back(current);
    return args;
}

std::string get_full_path(std::string cmd) {
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    std::stringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        fs::path p = fs::path(dir) / cmd;
        if (fs::exists(p)) {
            auto perms = fs::status(p).permissions();
            if ((perms & fs::perms::owner_exec) != fs::perms::none) return p.string();
        }
    }
    return "";
}

int main() {
    std::cout << std::unitbuf;
    
    // Readline Global Config
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

        // --- EXECUTION & REDIRECTION LOGIC ---
        // (Keep your existing redirection/builtin/exec logic here)
        // Ensure you're handling builtins like 'exit', 'echo', 'cd', etc.
        std::string command = args[0];
        if (command == "exit") return 0;
        else if (command == "pwd") std::cout << fs::current_path().string() << "\n";
        // ... rest of execution logic ...
    }
    return 0;
}