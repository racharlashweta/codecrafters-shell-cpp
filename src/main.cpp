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

struct Job {
    int id;
    pid_t pid;
    std::string command;
    mutable std::string status; 
};

std::vector<Job> job_list;
const std::vector<std::string> builtins_list = {"echo", "exit", "type", "pwd", "cd", "jobs"};

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
        } else { active.push_back(job_list[i]); }
    }
    job_list = active;
}

char* command_generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static size_t idx;
    if (!state) {
        matches.clear();
        idx = 0;
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
        try {
            for (const auto& entry : fs::directory_iterator(".")) {
                std::string name = entry.path().filename().string();
                if (name.find(prefix) == 0) {
                    if (fs::is_directory(entry.path())) name += "/";
                    matches.push_back(name);
                }
            }
        } catch (...) {}
        std::sort(matches.begin(), matches.end());
        matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    }
    if (idx < matches.size()) return strdup(matches[idx++].c_str());
    return nullptr;
}

char** my_completion(const char* text, int start, int end) {
    rl_attempted_completion_over = 1; 
    return rl_completion_matches(text, command_generator);
}

std::vector<std::string> parse_args(const std::string& input) {
    std::vector<std::string> args;
    std::string current;
    bool in_double_quotes = false;
    bool in_single_quotes = false;
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (c == '\\' && !in_single_quotes) {
            if (i + 1 < input.length()) {
                char next = input[i + 1];
                if (in_double_quotes) {
                    if (next == '$' || next == '`' || next == '"' || next == '\\' || next == '\n') {
                        current += next; i++;
                    } else current += c;
                } else { current += next; i++; }
            }
            continue;
        }
        if (c == '"' && !in_single_quotes) { in_double_quotes = !in_double_quotes; continue; }
        if (c == '\'' && !in_double_quotes) { in_single_quotes = !in_single_quotes; continue; }
        if (std::isspace(c) && !in_double_quotes && !in_single_quotes) {
            if (!current.empty()) { args.push_back(current); current.clear(); }
        } else current += c;
    }
    if (!current.empty()) args.push_back(current);
    return args;
}

void execute_command(std::vector<std::string> args, bool is_bg, std::string raw_input) {
    int out_fd = -1, err_fd = -1, saved_out = dup(STDOUT_FILENO), saved_err = dup(STDERR_FILENO);
    std::vector<std::string> clean;
    for (size_t i = 0; i < args.size(); ++i) {
        int target = -1; bool append = false;
        if (args[i] == ">" || args[i] == "1>") { target = 1; }
        else if (args[i] == ">>" || args[i] == "1>>") { target = 1; append = true; }
        else if (args[i] == "2>") { target = 2; }
        else if (args[i] == "2>>") { target = 2; append = true; }

        if (target != -1 && i + 1 < args.size()) {
            int f = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int fd = open(args[++i].c_str(), f, 0644);
            if (target == 1) out_fd = fd; else err_fd = fd;
        } else clean.push_back(args[i]);
    }
    if (clean.empty()) return;
    if (out_fd != -1) { dup2(out_fd, STDOUT_FILENO); close(out_fd); }
    if (err_fd != -1) { dup2(err_fd, STDERR_FILENO); close(err_fd); }

    const std::string& cmd = clean[0];
    if (cmd == "echo") {
        for (size_t i = 1; i < clean.size(); ++i) std::cout << clean[i] << (i == clean.size() - 1 ? "" : " ");
        std::cout << std::endl;
    } else if (cmd == "exit") exit(0);
    else if (cmd == "pwd") std::cout << fs::current_path().string() << std::endl;
    else if (cmd == "cd") {
        std::string t = (clean.size() > 1) ? clean[1] : "~";
        if (t == "~") { char* h = getenv("HOME"); t = h ? h : "/"; }
        if (chdir(t.c_str()) != 0) std::cerr << "cd: " << t << ": No such file" << std::endl;
    } else if (cmd == "jobs") {
        for (auto& j : job_list) { int s; if (waitpid(j.pid, &s, WNOHANG) > 0) j.status = "Done"; }
        for (size_t i = 0; i < job_list.size(); ++i) {
            char m = (i == job_list.size() - 1) ? '+' : (i == job_list.size() - 2 ? '-' : ' ');
            std::cout << "[" << job_list[i].id << "]" << m << "  " << std::left << std::setw(24) << job_list[i].status << format_cmd_for_display(job_list[i].command, job_list[i].status) << std::endl;
        }
        std::vector<Job> n; for (const auto& j : job_list) if (j.status == "Running") n.push_back(j); job_list = n;
    } else if (cmd == "type") {
        if (clean.size() > 1) {
            if (std::find(builtins_list.begin(), builtins_list.end(), clean[1]) != builtins_list.end()) std::cout << clean[1] << " is a shell builtin" << std::endl;
            else { std::string p = get_full_path(clean[1]); if (!p.empty()) std::cout << clean[1] << " is " << p << std::endl; else std::cout << clean[1] << ": not found" << std::endl; }
        }
    } else {
        pid_t pid = fork();
        if (pid == 0) {
            std::string p = get_full_path(cmd); if (p.empty()) { std::cerr << cmd << ": command not found" << std::endl; exit(1); }
            std::vector<char*> ca; for (auto& a : clean) ca.push_back(const_cast<char*>(a.c_str())); ca.push_back(nullptr);
            execvp(p.c_str(), ca.data()); exit(1);
        } else {
            if (is_bg) { int id = get_next_available_id(); std::cout << "[" << id << "] " << pid << std::endl; job_list.push_back({id, pid, raw_input, "Running"}); }
            else waitpid(pid, nullptr, 0);
        }
    }
    dup2(saved_out, STDOUT_FILENO); dup2(saved_err, STDERR_FILENO); close(saved_out); close(saved_err);
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
        bool is_bg = (!args.empty() && args.back() == "&"); if (is_bg) args.pop_back();

        std::vector<std::vector<std::string>> commands;
        std::vector<std::string> current_cmd;
        for (const auto& arg : args) { if (arg == "|") { commands.push_back(current_cmd); current_cmd.clear(); } else current_cmd.push_back(arg); }
        commands.push_back(current_cmd);

        if (commands.size() > 1) {
            int num_cmds = commands.size(), prev_read = -1;
            std::vector<pid_t> pids;
            for (int i = 0; i < num_cmds; ++i) {
                int pfd[2]; if (i < num_cmds - 1) pipe(pfd);
                pid_t pid = fork();
                if (pid == 0) {
                    if (prev_read != -1) { dup2(prev_read, STDIN_FILENO); close(prev_read); }
                    if (i < num_cmds - 1) { dup2(pfd[1], STDOUT_FILENO); close(pfd[0]); close(pfd[1]); }
                    execute_command(commands[i], false, ""); exit(0);
                }
                pids.push_back(pid); if (prev_read != -1) close(prev_read);
                if (i < num_cmds - 1) { close(pfd[1]); prev_read = pfd[0]; }
            }
            for (pid_t p : pids) waitpid(p, nullptr, 0);
        } else execute_command(args, is_bg, input);
        free(line);
    }
    return 0;
}