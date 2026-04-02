#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <map>
#include <set>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

// --- Structures ---
struct Job {
    pid_t pid;
    std::string command;
    std::string status;
};

// --- Global State ---
std::map<int, Job> jobs;
std::set<int> free_nums = {1};
std::vector<std::string> builtins = {"echo", "cd", "exit", "pwd", "history", "type", "jobs"};
std::vector<std::string> env_paths;

// --- Helper: Split Path ---
std::vector<std::string> split_path(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) tokens.push_back(token);
    }
    return tokens;
}

// --- Job Management ---
void reap_jobs() {
    for (auto it = jobs.begin(); it != jobs.end(); ) {
        int status;
        pid_t result = waitpid(it->second.pid, &status, WNOHANG);

        if (result > 0 || (result == -1 && errno == ECHILD)) {
            std::cout << "\n[" << it->first << "]+  Done\t" << it->second.command << std::endl;
            free_nums.insert(it->first);
            it = jobs.erase(it);
        } else {
            ++it;
        }
    }
}

// --- Builtin: Type ---
void do_type(const std::string& cmd) {
    if (std::find(builtins.begin(), builtins.end(), cmd) != builtins.end()) {
        std::cout << cmd << " is a shell builtin" << std::endl;
        return;
    }
    for (const auto& path : env_paths) {
        std::string full_path = path + "/" + cmd;
        if (access(full_path.c_str(), X_OK) == 0) {
            std::cout << cmd << " is " << full_path << std::endl;
            return;
        }
    }
    std::cout << cmd << ": not found" << std::endl;
}

// --- Command Execution ---
void execute_command(std::string input, bool background) {
    // Basic Redirection Check (simplified for stability)
    size_t redir_pos = input.find('>');
    std::string outfile = "";
    bool append = false;

    if (redir_pos != std::string::npos) {
        if (redir_pos + 1 < input.length() && input[redir_pos + 1] == '>') {
            append = true;
            outfile = input.substr(redir_pos + 2);
        } else {
            outfile = input.substr(redir_pos + 1);
        }
        input = input.substr(0, redir_pos);
        // Trim filename
        outfile.erase(0, outfile.find_first_not_of(" "));
        outfile.erase(outfile.find_last_not_of(" ") + 1);
    }

    // Tokenize
    std::vector<std::string> args;
    std::istringstream iss(input);
    std::string tmp;
    while (iss >> tmp) args.push_back(tmp);
    if (args.empty()) return;

    pid_t pid = fork();
    if (pid == 0) {
        // Handle Redirection in Child
        if (!outfile.empty()) {
            int fd = open(outfile.c_str(), O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        std::vector<char*> c_args;
        for (auto& s : args) c_args.push_back(&s[0]);
        c_args.push_back(nullptr);

        execvp(c_args[0], c_args.data());
        std::cerr << args[0] << ": command not found" << std::endl;
        exit(1);
    } else {
        if (background) {
            int job_id = *free_nums.begin();
            free_nums.erase(free_nums.begin());
            if (free_nums.empty()) free_nums.insert(job_id + 1);
            jobs[job_id] = {pid, input, "Running"};
            std::cout << "[" << job_id << "] " << pid << std::endl;
        } else {
            waitpid(pid, nullptr, 0);
        }
    }
}

int main() {
    std::cout << std::unitbuf;
    const char* path_env = std::getenv("PATH");
    if (path_env) env_paths = split_path(path_env, ':');

    while (true) {
        reap_jobs();
        char* raw_line = readline("$ ");
        if (!raw_line) break; 

        std::string input(raw_line);
        if (input.empty()) {
            free(raw_line);
            continue;
        }
        add_history(raw_line);
        free(raw_line);

        // Background check
        bool background = false;
        if (input.back() == '&') {
            background = true;
            input.pop_back();
        }

        // Parse first token for builtins
        std::istringstream iss(input);
        std::string first_token;
        iss >> first_token;

        if (first_token == "exit") {
            break;
        } else if (first_token == "cd") {
            std::string target;
            iss >> target;
            if (target.empty() || target == "~") target = std::getenv("HOME");
            if (chdir(target.c_str()) != 0) perror("cd");
        } else if (first_token == "pwd") {
            std::cout << std::filesystem::current_path().string() << std::endl;
        } else if (first_token == "type") {
            std::string cmd;
            iss >> cmd;
            do_type(cmd);
        } else if (first_token == "jobs") {
            for (auto const& [id, job] : jobs) {
                std::cout << "[" << id << "] " << job.status << "\t" << job.command << std::endl;
            }
        } else if (first_token == "echo") {
            // Use your custom echo logic for quotes
            std::string line = input.substr(input.find("echo") + 4);
            bool in_sq = false, in_dq = false;
            std::string buffer;
            for (size_t i = 0; i < line.length(); ++i) {
                if (line[i] == '\'' && !in_dq) in_sq = !in_sq;
                else if (line[i] == '\"' && !in_sq) in_dq = !in_dq;
                else if (line[i] == '\\' && i + 1 < line.length() && !in_sq) buffer += line[++i];
                else buffer += line[i];
            }
            std::cout << buffer << std::endl;
        } else {
            execute_command(input, background);
        }
    }
    return 0;
}
