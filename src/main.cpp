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
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>

namespace fs = std::filesystem;

// --- UTILS ---
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    size_t end = s.find_last_not_of(" \t");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
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

std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;
    for (char c : s) {
        if (c == '"') in_quotes = !in_quotes;
        else if (c == ' ' && !in_quotes) {
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
        } else current += c;
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

// --- BUILTINS ---
const std::vector<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};

bool handle_builtin(const std::vector<std::string>& args) {
    if (args.empty()) return false;
    std::string cmd = args[0];
    if (cmd == "echo") {
        for (size_t i = 1; i < args.size(); ++i) 
            std::cout << args[i] << (i == args.size() - 1 ? "" : " ");
        std::cout << std::endl;
        return true;
    }
    if (cmd == "type") {
        if (args.size() < 2) return true;
        if (std::find(builtins.begin(), builtins.end(), args[1]) != builtins.end())
            std::cout << args[1] << " is a shell builtin" << std::endl;
        else {
            std::string p = get_path(args[1]);
            if (!p.empty()) std::cout << args[1] << " is " << p << std::endl;
            else std::cout << args[1] << ": not found" << std::endl;
        }
        return true;
    }
    if (cmd == "pwd") { std::cout << fs::current_path().string() << std::endl; return true; }
    return false;
}

// --- PIPELINE ENGINE (Fixed for #XK3) ---
void run_pipeline(const std::string& line) {
    std::vector<std::string> stages;
    std::stringstream ss(line);
    std::string seg;
    while (std::getline(ss, seg, '|')) stages.push_back(trim(seg));

    int n = stages.size();
    int in_fd = STDIN_FILENO;
    std::vector<pid_t> pids;
    std::vector<int> pipe_fds; // Track all fds to close in children

    for (int i = 0; i < n; i++) {
        int fds[2];
        if (i < n - 1) {
            if (pipe(fds) < 0) return;
            pipe_fds.push_back(fds[0]);
            pipe_fds.push_back(fds[1]);
        }

        pid_t pid = fork();
        if (pid == 0) {
            if (in_fd != STDIN_FILENO) {
                dup2(in_fd, STDIN_FILENO);
            }
            if (i < n - 1) {
                dup2(fds[1], STDOUT_FILENO);
            }

            // THE FIX: Close EVERY pipe end created so far
            for (int fd : pipe_fds) close(fd);
            if (in_fd != STDIN_FILENO) close(in_fd);

            std::vector<std::string> args = tokenize(stages[i]);
            if (handle_builtin(args)) exit(0);

            std::string full = get_path(args[0]);
            if (!full.empty()) {
                std::vector<char*> ca;
                for (auto& a : args) ca.push_back(const_cast<char*>(a.c_str()));
                ca.push_back(nullptr);
                execvp(full.c_str(), ca.data());
            }
            exit(1);
        } else {
            pids.push_back(pid);
            if (in_fd != STDIN_FILENO) close(in_fd);
            if (i < n - 1) {
                close(fds[1]); // Parent MUST close write-end
                in_fd = fds[0];
            }
        }
    }

    for (pid_t p : pids) waitpid(p, nullptr, 0);
    std::cout << std::flush;
}

// --- MAIN ---
int main() {
    std::cout << std::unitbuf;
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
            } else if (!handle_builtin(args)) {
                pid_t pid = fork();
                if (pid == 0) {
                    std::string full = get_path(args[0]);
                    if (!full.empty()) {
                        std::vector<char*> ca;
                        for (auto& a : args) ca.push_back(const_cast<char*>(a.c_str()));
                        ca.push_back(nullptr);
                        execvp(full.c_str(), ca.data());
                    }
                    exit(1);
                } else waitpid(pid, nullptr, 0);
            }
        }
        free(line);
    }
    return 0;
}