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

const std::vector<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};

// --- UTILS ---

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    size_t end = s.find_last_not_of(" \t");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

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

// Tokenizer with quote support
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;

    for (char c : s) {
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ' ' && !in_quotes) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

// --- BUILTINS ---

bool handle_builtin(const std::vector<std::string>& args) {
    if (args.empty()) return false;

    std::string cmd = args[0];

    if (cmd == "echo") {
        for (size_t i = 1; i < args.size(); ++i) {
            std::cout << args[i];
            if (i != args.size() - 1) std::cout << " ";
        }
        std::cout << std::endl;
        return true;
    }

    if (cmd == "type") {
        if (args.size() < 2) return true;
        std::string target = args[1];

        if (std::find(builtins.begin(), builtins.end(), target) != builtins.end()) {
            std::cout << target << " is a shell builtin" << std::endl;
        } else {
            std::string p = get_path(target);
            if (!p.empty()) std::cout << target << " is " << p << std::endl;
            else std::cout << target << ": not found" << std::endl;
        }
        return true;
    }

    if (cmd == "pwd") {
        std::cout << fs::current_path().string() << std::endl;
        return true;
    }

    return false;
}

// --- PIPELINE ENGINE ---

void run_pipeline(const std::string& line, bool is_background, int& job_id) {
    std::vector<std::string> stages;
    std::stringstream ss(line);
    std::string seg;

    while (std::getline(ss, seg, '|')) {
        stages.push_back(trim(seg));
    }

    int n = stages.size();
    int prev_read_fd = -1;
    std::vector<pid_t> pids;

    for (int i = 0; i < n; i++) {
        int fds[2];

        if (i < n - 1) {
            if (pipe(fds) < 0) return;
        }

        pid_t pid = fork();

        if (pid == 0) {
            // CHILD

            if (i > 0) {
                dup2(prev_read_fd, STDIN_FILENO);
            }

            if (i < n - 1) {
                dup2(fds[1], STDOUT_FILENO);
            }

            if (prev_read_fd != -1) close(prev_read_fd);
            if (i < n - 1) {
                close(fds[0]);
                close(fds[1]);
            }

            std::vector<std::string> args = tokenize(stages[i]);
            if (args.empty()) exit(0);

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
            // PARENT

            pids.push_back(pid);

            if (prev_read_fd != -1) close(prev_read_fd);

            if (i < n - 1) {
                prev_read_fd = fds[0];
                close(fds[1]);
            }
        }
    }

    if (is_background) {
        std::cout << "[" << job_id++ << "] " << pids.back() << std::endl;
    } else {
        for (pid_t p : pids) {
            waitpid(p, nullptr, 0);
        }
    }

    std::cout << std::flush;
}

// --- MAIN ---

int main() {
    std::cout << std::unitbuf;

    // Prevent zombie processes
    signal(SIGCHLD, SIG_IGN);

    int job_id = 1;

    while (true) {
        char* line = readline("$ ");
        if (!line) break;

        if (strlen(line) == 0) {
            free(line);
            continue;
        }

        add_history(line);
        std::string input(line);

        // Detect background job
        bool is_background = false;
        if (!input.empty() && input.back() == '&') {
            is_background = true;
            input.pop_back();
            input = trim(input);
        }

        if (input.find('|') != std::string::npos) {
            run_pipeline(input, is_background, job_id);
        } else {
            std::vector<std::string> args = tokenize(input);
            if (args.empty()) {
                free(line);
                continue;
            }

            if (args[0] == "exit") exit(0);

            if (args[0] == "cd") {
                std::string p = args.size() > 1 ? args[1] : std::getenv("HOME");
                if (chdir(p.c_str()) != 0) {
                    std::cerr << "cd: " << p << ": No such file" << std::endl;
                }
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
                } else {
                    if (is_background) {
                        std::cout << "[" << job_id++ << "] " << pid << std::endl;
                    } else {
                        waitpid(pid, nullptr, 0);
                    }
                    std::cout << std::flush;
                }
            }
        }

        free(line);
    }

    return 0;
}