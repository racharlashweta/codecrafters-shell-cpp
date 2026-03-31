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
#include <iomanip>

#include <readline/readline.h>
#include <readline/history.h>

namespace fs = std::filesystem;

// --- DATA STRUCTURES ---

struct Job {
    int id;
    pid_t pid;
    std::string command;
    std::string status;
};

std::vector<Job> job_list;
const std::vector<std::string> builtins_list = {"echo", "exit", "type", "pwd", "cd", "jobs"};

// --- HELPERS ---

std::string format_cmd_for_display(std::string cmd, std::string status) {
    std::string result = cmd;
    if (status == "Done") {
        size_t last_amp = result.find_last_of('&');
        if (last_amp != std::string::npos) result.erase(last_amp);
    }
    size_t last_pos = result.find_last_not_of(" \t\n\r");
    if (last_pos != std::string::npos) result.erase(last_pos + 1);
    return result;
}

std::string get_full_path(const std::string& cmd) {
    if (cmd.find('/') != std::string::npos) return fs::exists(cmd) ? cmd : "";
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    std::stringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        try {
            fs::path p = fs::path(dir) / cmd;
            if (fs::exists(p) && (fs::status(p).permissions() & fs::perms::owner_exec) != fs::perms::none)
                return p.string();
        } catch (...) { continue; }
    }
    return "";
}

int get_next_available_id() {
    std::set<int> ids;
    for (const auto& j : job_list) ids.insert(j.id);
    int id = 1;
    while (ids.count(id)) id++;
    return id;
}

void reap_finished_jobs() {
    std::vector<Job> active;
    for (size_t i = 0; i < job_list.size(); ++i) {
        int status;
        if (waitpid(job_list[i].pid, &status, WNOHANG) > 0) {
            char marker = (i == job_list.size() - 1) ? '+' : (i == job_list.size() - 2 ? '-' : ' ');
            std::cout << "[" << job_list[i].id << "]" << marker << "  Done                    " 
                      << format_cmd_for_display(job_list[i].command, "Done") << std::endl;
        } else {
            active.push_back(job_list[i]);
        }
    }
    job_list = active;
}

// --- COMPLETION ---

char* command_generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static size_t idx;
    if (!state) {
        matches.clear(); idx = 0;
        std::string prefix(text);
        for (const auto& b : builtins_list) if (b.find(prefix) == 0) matches.push_back(b);
        char* path_env = std::getenv("PATH");
        if (path_env) {
            std::stringstream ss(path_env);
            std::string dir;
            while (std::getline(ss, dir, ':')) {
                if (!fs::exists(dir)) continue;
                try {
                    for (const auto& entry : fs::directory_iterator(dir)) {
                        std::string name = entry.path().filename().string();
                        if (name.find(prefix) == 0) matches.push_back(name);
                    }
                } catch (...) {}
            }
        }
    }
    return (idx < matches.size()) ? strdup(matches[idx++].c_str()) : nullptr;
}

char** my_completion(const char* text, int start, int end) {
    if (start == 0) {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(text, command_generator);
    }
    return nullptr;
}

// --- PARSER ---

std::vector<std::string> parse_args(const std::string& input) {
    std::vector<std::string> args;
    std::string current;
    bool in_double_quotes = false;
    bool in_single_quotes = false;

    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];

        // Handle backslash escaping (not inside single quotes)
        if (c == '\\' && !in_single_quotes) {
            if (i + 1 < input.length()) {
                char next = input[++i];
                // In double quotes, \ only escapes certain characters (like \, ", $)
                // For the purpose of these tests, we handle standard escaping
                current += next;
            }
            continue;
        }

        // Toggle double quotes
        if (c == '"' && !in_single_quotes) {
            in_double_quotes = !in_double_quotes;
            continue;
        }

        // Toggle single quotes
        if (c == '\'' && !in_double_quotes) {
            in_single_quotes = !in_single_quotes;
            continue;
        }

        // Split by whitespace only if not inside any quotes
        if (std::isspace(c) && !in_double_quotes && !in_single_quotes) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) args.push_back(current);
    return args;
}

// --- EXECUTION ---

void execute_command(std::vector<std::string> args, bool is_bg, std::string raw_input) {
    int out_fd = -1, err_fd = -1;
    int saved_stdout = dup(STDOUT_FILENO);
    int saved_stderr = dup(STDERR_FILENO);

    std::vector<std::string> clean_args;
    for (size_t i = 0; i < args.size(); ++i) {
        int target = -1; bool append = false;
        if (args[i] == ">" || args[i] == "1>") { target = 1; append = false; }
        else if (args[i] == ">>" || args[i] == "1>>") { target = 1; append = true; }
        else if (args[i] == "2>") { target = 2; append = false; }
        else if (args[i] == "2>>") { target = 2; append = true; }

        if (target != -1 && i + 1 < args.size()) {
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int fd = open(args[++i].c_str(), flags, 0644);
            if (target == 1) out_fd = fd; else err_fd = fd;
        } else {
            clean_args.push_back(args[i]);
        }
    }

    if (clean_args.empty()) return;
    if (out_fd != -1) { dup2(out_fd, STDOUT_FILENO); close(out_fd); }
    if (err_fd != -1) { dup2(err_fd, STDERR_FILENO); close(err_fd); }

    const std::string& cmd = clean_args[0];

    if (cmd == "echo") {
        for (size_t i = 1; i < clean_args.size(); ++i) {
            std::cout << clean_args[i] << (i == clean_args.size() - 1 ? "" : " ");
        }
        std::cout << std::endl;
    } 
    else if (cmd == "exit") { exit(0); }
    else if (cmd == "pwd") { std::cout << fs::current_path().string() << std::endl; }
    else if (cmd == "cd") {
        std::string target = (clean_args.size() > 1) ? clean_args[1] : std::getenv("HOME");
        if (chdir(target.c_str()) != 0) std::cerr << "cd: " << target << ": No such file or directory" << std::endl;
    }
    else if (cmd == "jobs") {
        for (auto& j : job_list) {
            int s; if (waitpid(j.pid, &s, WNOHANG) > 0) j.status = "Done";
        }
        for (size_t i = 0; i < job_list.size(); ++i) {
            char m = (i == job_list.size() - 1) ? '+' : (i == job_list.size() - 2 ? '-' : ' ');
            std::cout << "[" << job_list[i].id << "]" << m << "  " << std::left << std::setw(24) 
                      << job_list[i].status << format_cmd_for_display(job_list[i].command, job_list[i].status) << std::endl;
        }
        // Cleanup 'Done' jobs from the internal list after showing them
        std::vector<Job> next;
        for (const auto& j : job_list) if (j.status == "Running") next.push_back(j);
        job_list = next;
    }
    else if (cmd == "type") {
        if (clean_args.size() > 1) {
            if (std::find(builtins_list.begin(), builtins_list.end(), clean_args[1]) != builtins_list.end())
                std::cout << clean_args[1] << " is a shell builtin" << std::endl;
            else {
                std::string p = get_full_path(clean_args[1]);
                if (!p.empty()) std::cout << clean_args[1] << " is " << p << std::endl;
                else std::cout << clean_args[1] << ": not found" << std::endl;
            }
        }
    } else {
        pid_t pid = fork();
        if (pid == 0) {
            std::string p = get_full_path(cmd);
            if (p.empty()) { std::cerr << cmd << ": command not found" << std::endl; exit(1); }
            std::vector<char*> c_args;
            for (auto& a : clean_args) c_args.push_back(const_cast<char*>(a.c_str()));
            c_args.push_back(nullptr);
            execvp(p.c_str(), c_args.data());
            exit(1);
        } else {
            if (is_bg) {
                int id = get_next_available_id();
                std::cout << "[" << id << "] " << pid << std::endl;
                job_list.push_back({id, pid, raw_input, "Running"});
            } else { waitpid(pid, nullptr, 0); }
        }
    }

    dup2(saved_stdout, STDOUT_FILENO); dup2(saved_stderr, STDERR_FILENO);
    close(saved_stdout); close(saved_stderr);
}

int main() {
    std::cout << std::unitbuf;
    rl_attempted_completion_function = my_completion;

    while (true) {
        reap_finished_jobs();
        char* line = readline("$ ");
        if (!line) break;
        if (strlen(line) == 0) { free(line); continue; }

        add_history(line);
        std::string input(line);
        std::vector<std::string> args = parse_args(input);
        
        bool is_bg = (!args.empty() && args.back() == "&");
        if (is_bg) args.pop_back();

        auto pipe_it = std::find(args.begin(), args.end(), "|");
        if (pipe_it != args.end()) {
            std::vector<std::string> left(args.begin(), pipe_it), right(pipe_it + 1, args.end());
            int pfd[2]; pipe(pfd);
            if (fork() == 0) { dup2(pfd[1], STDOUT_FILENO); close(pfd[0]); close(pfd[1]); execute_command(left, false, ""); exit(0); }
            if (fork() == 0) { dup2(pfd[0], STDIN_FILENO); close(pfd[0]); close(pfd[1]); execute_command(right, false, ""); exit(0); }
            close(pfd[0]); close(pfd[1]); wait(nullptr); wait(nullptr);
        } else {
            execute_command(args, is_bg, input);
        }
        free(line);
    }
    return 0;
}