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

// --- STAGE #NO5: CUSTOM DISPLAY HOOK ---
// This handles the formatting (dir/ vs file) and the double-tab listing.
void custom_display_matches(char** matches, int num_matches, int max_length) {
    std::cout << "\n";
    std::vector<std::string> display_list;
    
    // matches[0] is the common prefix, actual matches start at index 1
    for (int i = 1; i <= num_matches; ++i) {
        std::string m(matches[i]);
        // If it's a directory, ensure it has a trailing slash for the display list
        if (fs::exists(m) && fs::is_directory(m)) {
            if (m.back() != '/') m += "/";
        }
        display_list.push_back(m);
    }
    
    std::sort(display_list.begin(), display_list.end());
    
    for (size_t i = 0; i < display_list.size(); ++i) {
        std::cout << display_list[i] << (i == display_list.size() - 1 ? "" : "  ");
    }
    
    std::cout << "\n";
    rl_on_new_line();
    rl_redisplay();
}

// --- MASTER COMPLETION HOOK ---
char** my_completion(const char* text, int start, int end) {
    rl_completion_suppress_append = 0;
    rl_completion_append_character = ' ';

    char** matches = nullptr;
    if (start == 0) {
        rl_attempted_completion_over = 1;
        matches = rl_completion_matches(text, command_generator);
    } else {
        rl_attempted_completion_over = 1; 
        matches = rl_completion_matches(text, rl_filename_completion_function);
    }

    if (!matches || !matches[0]) {
        rl_ding(); 
        return nullptr;
    }

    // If ambiguous (multiple matches), Readline handles the bell 
    // because we didn't return a unique match. 
    
    // If unique match, handle the trailing character logic
    if (matches[0] && !matches[1]) {
        if (fs::exists(matches[0]) && fs::is_directory(matches[0])) {
            rl_completion_append_character = '/';
            rl_completion_suppress_append = 1; 
        }
    } else {
        // If multiple matches, don't append anything yet
        rl_completion_append_character = '\0';
    }

    return matches;
}

// --- TOKENIZER & MAIN ---
// (Ensure your parse_arguments and get_full_path are present as before)
// ...

int main() {
    std::cout << std::unitbuf;
    
    rl_attempted_completion_function = my_completion;
    rl_completion_display_matches_hook = custom_display_matches;
    
    // Configuration for Stage #NO5
    rl_variable_bind("show-all-if-ambiguous", "off"); // Ensures bell on 1st tab
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

        // ... Redirection and Builtin Execution Logic ...
        // (Keep the echo flush logic with std::endl)
        std::string command = args[0];
        if (command == "exit") return 0;
        else if (command == "echo") {
            for (size_t i = 1; i < args.size(); ++i) 
                std::cout << args[i] << (i == args.size() - 1 ? "" : " ");
            std::cout << std::endl;
        }
        // ... rest of builtins ...
    }
    return 0;
}