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

// --- DATA STRUCTURES & GLOBALS ---

struct Job {
    int id;
    pid_t pid;
    std::string command;
    std::string status;
};

std::vector<Job> job_list;
const std::vector<std::string> builtins_list = {"echo", "exit", "type", "pwd", "cd", "jobs"};

// --- HELPER FUNCTIONS ---

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
            std::string d_cmd = format_cmd_for_display(job_list[i].command, "Done");
            std::cout << "[" << job_list[i].id << "]" << marker << "  Done                    " << d_cmd << std::endl;
        } else {
            active.push_back(job_list[i]);
        }
    }
    job_list = active;
}

// --- COMPLETION LOGIC ---

std::set<std::string> get_all_matches(const std::string& prefix) {
    std::set<std::string> matches;
    for (const auto& b : builtins_list) if (b.find(prefix) == 0) matches.insert(b);
    char* path_env = std::getenv("PATH");
    if (path_env) {
        std::stringstream ss(path_env);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            if (!fs::exists(dir)) continue;
            try {
                for (const auto& entry : fs::directory_iterator(dir)) {
                    std::string name = entry.path().filename().string();
                    if (name.find(prefix) == 0) matches.insert(name);
                }
            } catch (...) {}
        }
    }
    return matches;
}

char* command_generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static size_t idx;
    if (!state) {
        matches.clear(); idx = 0;
        std::set<std::string> m_set = get_all_matches(text);
        for (const auto& s : m_set) matches.push_back(s);
    }
    return (idx < matches.size()) ? strdup(matches[idx++].c_str()) : nullptr;
}

char** my_completion(const char* text, int start, int end) {
    if (start == 0) {
        rl_attempted_completion_over = 1;
        std::set<std::string> m_set = get_all_matches(text);
        if (m_set.empty()) { rl_ding(); return nullptr; }
        rl_completion_append_character = (m_set.size() == 1) ? ' ' : '\0';
        return rl_completion_matches(text, command_generator);
    }
    rl_attempted_completion_over = 0;
    return nullptr;
}

// --- REDIRECTION & EXECUTION ---

std::vector<std::string> handle_redirection(const std::vector<std::string>& args, int& out_fd, int& err_fd) {
    std::vector<std::string> clean_args;
    for (size_t i = 0; i < args.size(); ++i) {
        bool append = false;
        int target_fd = -1;

        if (args[i] == ">" || args[i] == "1>") { target_fd = STDOUT_FILENO; append = false; }
        else if (args[i] == ">>" || args[i] == "1>>") { target_fd = STDOUT_FILENO; append = true; }
        else if (args[i] == "2>") { target_fd = STDERR_FILENO; append = false; }
        else if (args[i] == "2>>") { target_fd = STDERR_FILENO; append = true; }

        if (target_fd != -1 && i + 1 < args.size()) {
            std::string filename = args[++i];
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int fd = open(filename.c_str(), flags, 0644);
            if (fd != -1) {
                if (target_fd == STDOUT_FILENO) out_fd = fd;
                else err_fd = fd;
            }
        } else {
            clean_args.push_back(args[i]);
        }
    }
    return clean_args;
}

void execute_command(std::vector<std::string> args, bool is_bg, std::string raw_input) {
    int out_fd = -1, err_fd = -1;
    int saved_stdout = dup(STDOUT_FILENO);
    int saved_stderr = dup(STDERR_FILENO);

    std::vector<std::string> clean_args = handle_redirection(args, out_fd, err_fd);
    if (clean_args.empty()) {
        close(saved_stdout); close(saved_stderr);
        return;
    }

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
        for (auto& job : job_list) {
            int status;
            if (waitpid(job.pid, &status, WNOHANG) > 0) job.status = "Done";
        }
        for (size_t i = 0; i < job_list.size(); ++i) {
            char m = (i == job_list.size() - 1) ? '+' : (i == job_list.size() - 2 ? '-' : ' ');
            std::cout << "[" << job_list[i].id << "]" << m << "  " << std::left << std::setw(24) << job_list[i].status << format_cmd_for_display(job_list[i].command, job_list[i].status) << std::endl;
        }
        // Cleanup 'Done' jobs after showing them
        std::vector<Job> active;
        for (const auto& j : job_list) if (j.status == "Running") active.push_back(j);
        job_list = active;
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
    }
    else {
        pid_t pid = fork();
        if (pid == 0) {
            std::string path = get_full_path(cmd);
            if (path.empty()) {
                std::cerr << cmd << ": command not found" << std::endl;
                exit(1);
            }
            std::vector<char*> c_args;
            for (auto& a : clean_args) c_args.push_back(const_cast<char*>(a.c_str()));
            c_args.push_back(nullptr);
            execvp(path.c_str(), c_args.data());
            exit(1);
        } else {
            if (is_bg) {
                int id = get_next_available_id();
                std::cout << "[" << id << "] " << pid << std::endl;
                job_list.push_back({id, pid, raw_input, "Running"});
                std::sort(job_list.begin(), job_list.end(), [](const Job& a, const Job& b) { return a.id < b.id; });
            } else {
                waitpid(pid, nullptr, 0);
            }
        }
    }

    dup2(saved_stdout, STDOUT_FILENO);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stdout);
    close(saved_stderr);
}

// --- MAIN LOOP ---

int main() {
    std::cout << std::unitbuf;
    rl_attempted_completion_function = my_completion;

    while (true) {
        reap_finished_jobs();
        char* line = readline("$ ");
        if (!line) break;
        if (strlen(line) == 0) { free(line); continue; }
        add_history(line);
        std::string raw_input(line);

        std::vector<std::string> args;
        std::stringstream ss(raw_input);
        std::string tmp;
        while (ss >> tmp) args.push_back(tmp);

        bool is_bg = (!args.empty() && args.back() == "&");
        if (is_bg) args.pop_back();

        auto pipe_it = std::find(args.begin(), args.end(), "|");
        if (pipe_it != args.end()) {
            std::vector<std::string> left_args(args.begin(), pipe_it);
            std::vector<std::string> right_args(pipe_it + 1, args.end());
            int pfd[2];
            if (pipe(pfd) == -1) continue;
            pid_t p1 = fork();
            if (p1 == 0) { dup2(pfd[1], STDOUT_FILENO); close(pfd[0]); close(pfd[1]); execute_command(left_args, false, ""); exit(0); }
            pid_t p2 = fork();
            if (p2 == 0) { dup2(pfd[0], STDIN_FILENO); close(pfd[0]); close(pfd[1]); execute_command(right_args, false, ""); exit(0); }
            close(pfd[0]); close(pfd[1]);
            waitpid(p1, nullptr, 0); waitpid(p2, nullptr, 0);
        } else {
            execute_command(args, is_bg, raw_input);
        }
        free(line);
    }
    return 0;
}