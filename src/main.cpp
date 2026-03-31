#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <cstring>

#include <readline/readline.h>
#include <readline/history.h>

namespace fs = std::filesystem;

// --- GLOBALS & CONSTANTS ---
const std::vector<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};

// --- PATH RESOLUTION ---
std::string get_path(const std::string& cmd) {
    if (cmd.find('/') != std::string::npos) return fs::exists(cmd) ? cmd : "";
    char* env = std::getenv("PATH");
    if (!env) return "";
    std::stringstream ss(env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        fs::path p = fs::path(dir) / cmd;
        if (fs::exists(p)) return p.string();
    }
    return "";
}

// --- TOKENIZER (Handles spaces correctly) ---
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string word;
    while (ss >> word) tokens.push_back(word);
    return tokens;
}

// --- AUTOCOMPLETE (Fixed for Directories) ---
char* generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static size_t idx;
    if (!state) {
        matches.clear(); idx = 0;
        std::string pref(text);
        for (const auto& b : builtins) if (b.find(pref) == 0) matches.push_back(b);
        try {
            for (const auto& entry : fs::directory_iterator(".")) {
                std::string name = entry.path().filename().string();
                if (name == "." || name == "..") continue;
                if (name.find(pref) == 0) {
                    if (fs::is_directory(entry.path())) name += "/";
                    matches.push_back(name);
                }
            }
        } catch (...) {}
        std::sort(matches.begin(), matches.end());
        matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    }
    return (idx < matches.size()) ? strdup(matches[idx++].c_str()) : nullptr;
}

char** completion(const char* text, int start, int end) {
    rl_attempted_completion_over = 1;
    rl_completion_append_character = ' ';
    char** matches = rl_completion_matches(text, generator);
    if (matches && matches[0] && matches[1] == nullptr) {
        if (std::string(matches[0]).back() == '/') rl_completion_append_character = '\0';
    }
    return matches;
}

// --- CORE EXECUTION ---
void run_command(std::vector<std::string> args) {
    if (args.empty()) return;
    if (args[0] == "exit") exit(0);
    if (args[0] == "cd") {
        std::string path = args.size() > 1 ? args[1] : std::getenv("HOME");
        if (chdir(path.c_str()) != 0) std::cerr << "cd: " << path << ": No such file" << std::endl;
        return;
    }
    if (args[0] == "pwd") {
        std::cout << fs::current_path().string() << std::endl;
        return;
    }
    if (args[0] == "echo") {
        for (size_t i = 1; i < args.size(); ++i) std::cout << args[i] << (i == args.size()-1 ? "" : " ");
        std::cout << std::endl;
        return;
    }

    std::string full = get_path(args[0]);
    if (full.empty()) {
        std::cerr << args[0] << ": command not found" << std::endl;
        exit(1);
    }
    std::vector<char*> c_args;
    for (auto& a : args) c_args.push_back(const_cast<char*>(a.c_str()));
    c_args.push_back(nullptr);
    execvp(full.c_str(), c_args.data());
    exit(1);
}

// --- PIPELINE ENGINE (The Fix for #XK3) ---
void run_pipeline(const std::string& line) {
    std::vector<std::string> stages;
    std::stringstream ss(line);
    std::string seg;
    while (std::getline(ss, seg, '|')) stages.push_back(seg);

    int n = stages.size();
    int pipefds[2 * (n - 1)];
    for (int i = 0; i < n - 1; i++) pipe(pipefds + i * 2);

    std::vector<pid_t> pids;
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            if (i > 0) dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
            if (i < n - 1) dup2(pipefds[i * 2 + 1], STDOUT_FILENO);
            for (int j = 0; j < 2 * (n - 1); j++) close(pipefds[j]);
            run_command(tokenize(stages[i]));
            exit(0);
        }
        pids.push_back(pid);
    }

    for (int i = 0; i < 2 * (n - 1); i++) close(pipefds[i]);
    for (pid_t p : pids) waitpid(p, nullptr, 0);
}

int main() {
    std::cout << std::unitbuf;
    rl_attempted_completion_function = completion;
    rl_basic_word_break_characters = " \t\n\"\\'`@$><=;|&{(";

    while (true) {
        char* line = readline("$ ");
        if (!line) break;
        if (strlen(line) == 0) { free(line); continue; }
        add_history(line);

        std::string input(line);
        if (input.find('|') != std::string::npos) {
            run_pipeline(input);
        } else {
            std::vector<std::string> args = tokenize(input);
            // Handle built-ins in the parent process if no pipe
            if (!args.empty() && (args[0] == "cd" || args[0] == "exit")) {
                run_command(args);
            } else {
                pid_t pid = fork();
                if (pid == 0) run_command(args);
                else waitpid(pid, nullptr, 0);
            }
        }
        free(line);
    }
    return 0;
}