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

// ... (Keep Job struct, format_cmd_for_display, get_full_path, get_next_available_id, reap_finished_jobs, and completion functions as they were) ...

// Helper to handle redirection and return "cleaned" arguments
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
            int fd = open(filename.c_str(), O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
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
    if (clean_args.empty()) return;

    if (out_fd != -1) { dup2(out_fd, STDOUT_FILENO); close(out_fd); }
    if (err_fd != -1) { dup2(err_fd, STDERR_FILENO); close(err_fd); }

    const std::string& cmd = clean_args[0];

    // Handle Builtins
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
        // ... (Keep your jobs logic here, just use clean_args)
    }
    else if (cmd == "type") {
        // ... (Keep your type logic here, just use clean_args)
    }
    // Handle Externals
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
            } else {
                waitpid(pid, nullptr, 0);
            }
        }
    }

    // Restore original streams
    dup2(saved_stdout, STDOUT_FILENO);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stdout);
    close(saved_stderr);
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
        std::string raw_input(line);

        std::vector<std::string> args;
        std::stringstream ss(raw_input);
        std::string tmp;
        while (ss >> tmp) args.push_back(tmp);

        bool is_bg = (!args.empty() && args.back() == "&");
        if (is_bg) args.pop_back();

        // Check for pipes first (Pipes are a bit special, keep your pipe logic or integrate it)
        auto pipe_it = std::find(args.begin(), args.end(), "|");
        if (pipe_it != args.end()) {
            // ... (Your existing pipe logic)
        } else {
            execute_command(args, is_bg, raw_input);
        }
        free(line);
    }
    return 0;
}