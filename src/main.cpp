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

// --- PATH SCANNER ---
std::vector<std::string> get_all_matches(const std::string& prefix) {
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

// --- READLINE GENERATOR ---
char* command_generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static size_t match_index;
    if (!state) {
        matches = get_all_matches(text);
        match_index = 0;
    }
    if (match_index < matches.size()) {
        char* res = (char*)malloc(matches[match_index].length() + 1);
        std::strcpy(res, matches[match_index++].c_str());
        return res;
    }
    return nullptr;
}

// --- CUSTOM DISPLAY HOOK ---
// This handles the <TAB><TAB> display requirement
void display_matches(char** matches, int num_matches, int max_length) {
    std::cout << "\n";
    std::vector<std::string> sorted_matches;
    // matches[0] is the common prefix, matches[1...num_matches] are the actual strings
    for (int i = 1; i <= num_matches; ++i) {
        sorted_matches.push_back(matches[i]);
    }
    std::sort(sorted_matches.begin(), sorted_matches.end());

    for (size_t i = 0; i < sorted_matches.size(); ++i) {
        std::cout << sorted_matches[i] << (i == sorted_matches.size() - 1 ? "" : "  ");
    }
    std::cout << "\n";
    
    // Force readline to redisplay the prompt and current buffer
    rl_on_new_line();
    rl_redisplay();
}

char** my_completion(const char* text, int start, int end) {
    if (start == 0) {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(text, command_generator);
    }
    return nullptr; 
}

// ... [Keep your parse_arguments and get_full_path functions from the previous stage] ...

int main() {
    std::cout << std::unitbuf;
    
    // Readline Config
    rl_attempted_completion_function = my_completion;
    rl_completion_display_matches_hook = display_matches;
    
    // These settings help match the "Bell on first, List on second" requirement
    rl_variable_bind("show-all-if-ambiguous", "off"); 
    rl_variable_bind("bell-style", "audible");

    while (true) {
        char* line = readline("$ ");
        if (!line) break; 
        std::string input(line);
        if (input.empty()) { free(line); continue; }
        add_history(line);

        std::vector<std::string> args = parse_arguments(input); // Your existing tokenizer
        free(line);
        if (args.empty()) continue;

        // ... [Insert the rest of your Command Execution logic here] ...
        // Ensure you keep the "Pre-create files" and "Redirection removal" logic
        // from the previous stage to maintain passes on #UN3 and #VZ4.
    }
    return 0;
}