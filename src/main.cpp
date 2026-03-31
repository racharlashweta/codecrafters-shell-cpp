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
#include <algorithm>
#include <cstring>

#include <readline/readline.h>
#include <readline/history.h>

namespace fs = std::filesystem;

// --- GLOBALS ---
const std::vector<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};

// --- PATH RESOLUTION ---
std::string get_path(const std::string& cmd) {
    if (cmd.find('/') != std::string::npos) return fs::exists(cmd) ? cmd : "";
    char* env = std::getenv("PATH");
    if (!env) return "";
    std::stringstream ss(env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        try {
            fs::path p = fs::path(dir) / cmd;
            if (fs::exists(p)) return p.string();
        } catch (...) {}
    }
    return "";
}

// --- TOKENIZER ---
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string word;
    while (ss >> word) tokens.push_back(word);
    return tokens;
}

// --- AUTOCOMPLETE (Directory Logic for #LC6) ---
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

// --- COMMAND EXECUTION ---
// This runs inside a child process for external commands
void exec_cmd_external(std::vector<std::string> args) {
    if (args.empty()) exit(0);
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

// --- PIPELINE ENGINE ---
void run_pipeline(const std::string& line) {
    std::vector<std::string> stages;
    std::stringstream ss(line);
    std::string seg;
    while (std::getline(ss, seg, '|')) stages.push_back(seg);

    int n = stages.size();
    int prev_pipe_read = -1;
    std::vector<pid_t> pids;

    for (int i = 0; i < n; i++) {
        int fds[2];
        if (i < n - 1) {
            if (pipe(fds) < 0) return;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // Setup Input
            if (i > 0) {
                dup2(prev_pipe_read, STDIN_FILENO);
                close(prev_pipe_read);
            }
            // Setup Output
            if (i < n - 1) {
                close(fds[0]); // Child doesn't read from its own pipe
                dup2(fds[1], STDOUT_FILENO);
                close(fds[1]);
            }
            
            std::vector<std::string> args = tokenize(stages[i]);
            if (args.empty()) exit(0);

            // Handle built-ins inside the pipe stage
            if (args[0] == "pwd") {
                std::cout << fs::current_path().string() << std::endl;
                exit(0);
            } else if (args[0] == "echo") {
                for (size_t k = 1; k < args.size(); ++k) 
                    std::cout << args[k] << (k == args.size()-1 ? "" : " ");
                std::cout << std::endl;
                exit(0);
            }
            
            exec_cmd_external(args);
        }

        // Parent cleanup
        if (prev_pipe_read != -1) close(prev_pipe_read);
        if (i < n - 1) {
            close(fds[1]); // Parent doesn't write to the pipe
            prev_pipe_read = fds[0];
        }
        pids.push_back(pid);
    }

    for (pid_t p : pids) waitpid(p, nullptr, 0);
}

// --- MAIN LOOP ---
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

            // Parent-level built-ins (must affect shell state)
            if (args[0] == "cd") {
                std::string path = args.size() > 1 ? args[1] : std::getenv("HOME");
                if (chdir(path.c_str()) != 0) std::cerr << "cd: " << path << ": No such file" << std::endl;
            } else if (args[0] == "exit") {
                exit(0);
            } else {
                pid_t pid = fork();
                if (pid == 0) {
                    if (args[0] == "pwd") {
                        std::cout << fs::current_path().string() << std::endl;
                        exit(0);
                    } else if (args[0] == "echo") {
                        for (size_t k = 1; k < args.size(); ++k) 
                            std::cout << args[k] << (k == args.size()-1 ? "" : " ");
                        std::cout << std::endl;
                        exit(0);
                    }
                    exec_cmd_external(args);
                } else {
                    waitpid(pid, nullptr, 0);
                }
            }
        }
        free(line);
    }
    return 0;
}