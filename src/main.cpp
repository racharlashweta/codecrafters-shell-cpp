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

// --- UTILS ---
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string word;
    while (ss >> word) tokens.push_back(word);
    return tokens;
}

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

// --- COMPLETION ---
char* generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static size_t idx;
    if (!state) {
        matches.clear(); idx = 0;
        std::string pref(text);
        try {
            for (const auto& entry : fs::directory_iterator(".")) {
                std::string name = entry.path().filename().string();
                if (name.find(pref) == 0) {
                    if (fs::is_directory(entry.path())) name += "/";
                    matches.push_back(name);
                }
            }
        } catch (...) {}
        std::sort(matches.begin(), matches.end());
    }
    return (idx < matches.size()) ? strdup(matches[idx++].c_str()) : nullptr;
}

char** completion(const char* text, int start, int end) {
    rl_attempted_completion_over = 1;
    rl_completion_append_character = ' ';
    char** matches = rl_completion_matches(text, generator);
    if (matches && matches[0] && matches[1] == nullptr && std::string(matches[0]).back() == '/')
        rl_completion_append_character = '\0';
    return matches;
}

// --- EXECUTION ---
void run_command(std::vector<std::string> args) {
    if (args.empty()) exit(0);
    if (args[0] == "echo") {
        for (size_t i = 1; i < args.size(); ++i) std::cout << args[i] << (i == args.size()-1 ? "" : " ");
        std::cout << std::endl;
        exit(0);
    }
    std::string full = get_path(args[0]);
    if (full.empty()) { std::cerr << args[0] << ": command not found" << std::endl; exit(1); }
    std::vector<char*> c_args;
    for (auto& a : args) c_args.push_back(const_cast<char*>(a.c_str()));
    c_args.push_back(nullptr);
    execvp(full.c_str(), c_args.data());
    exit(1);
}

// --- THE FIX FOR #XK3 ---
void run_pipeline(const std::string& line) {
    std::vector<std::string> stages;
    std::stringstream ss(line);
    std::string seg;
    while (std::getline(ss, seg, '|')) stages.push_back(seg);

    int n = stages.size();
    int in_fd = STDIN_FILENO; 
    std::vector<pid_t> pids;

    for (int i = 0; i < n; i++) {
        int fds[2];
        if (i < n - 1) pipe(fds);

        pid_t pid = fork();
        if (pid == 0) {
            // Child: Redirect input from the previous stage
            if (in_fd != STDIN_FILENO) {
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }
            // Child: Redirect output to the next stage
            if (i < n - 1) {
                close(fds[0]); // Don't need read end
                dup2(fds[1], STDOUT_FILENO);
                close(fds[1]);
            }
            run_command(tokenize(stages[i]));
        } else {
            // Parent: Cleanup descriptors
            pids.push_back(pid);
            if (in_fd != STDIN_FILENO) close(in_fd);
            if (i < n - 1) {
                close(fds[1]); // Close write end so child gets EOF
                in_fd = fds[0]; // Save read end for the next child
            }
        }
    }

    // Wait for ALL processes to finish
    for (pid_t p : pids) waitpid(p, nullptr, 0);
    // Explicitly flush to ensure everything hits the screen before the prompt
    std::fflush(stdout);
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
            if (args.empty()) { free(line); continue; }
            if (args[0] == "exit") exit(0);
            if (args[0] == "cd") {
                std::string p = args.size() > 1 ? args[1] : std::getenv("HOME");
                if (chdir(p.c_str()) != 0) std::cerr << "cd: " << p << ": No such file" << std::endl;
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