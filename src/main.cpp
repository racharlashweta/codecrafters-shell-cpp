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
#include <fstream>
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
    std::string status;
};

std::vector<Job> job_list;
std::vector<std::string> builtins_list = {"echo", "exit", "type", "pwd", "cd", "jobs"};

// --- REAPING LOGIC FOR PROMPT ---
// This is used BEFORE the prompt to catch jobs that finished during/after commands.
void reap_finished_jobs() {
    std::vector<Job> active_jobs;
    for (size_t i = 0; i < job_list.size(); ++i) {
        int status;
        if (waitpid(job_list[i].pid, &status, WNOHANG) > 0) {
            char marker = ' ';
            if (i == job_list.size() - 1) marker = '+';
            else if (i == job_list.size() - 2) marker = '-';

            std::string cmd = job_list[i].command;
            size_t amp = cmd.find_last_of('&');
            if (amp != std::string::npos) {
                cmd.erase(amp);
                cmd.erase(cmd.find_last_not_of(" \t") + 1);
            }

            std::cout << "[" << job_list[i].id << "]" << marker << "  " 
                      << std::left << std::setw(24) << "Done" << cmd << std::endl;
        } else {
            active_jobs.push_back(job_list[i]);
        }
    }
    job_list = active_jobs;
}

// --- TOKENIZER & PATH UTILS ---
std::vector<std::string> parse_arguments(const std::string& input) {
    std::vector<std::string> args;
    std::string current;
    bool in_s_quote = false, in_d_quote = false;
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (c == '\\' && !in_s_quote && !in_d_quote) {
            if (i + 1 < input.length()) current += input[++i];
            continue;
        }
        if (c == '\\' && in_d_quote) {
            if (i + 1 < input.length() && (input[i+1] == '\"' || input[i+1] == '\\' || input[i+1] == '$'))
                { current += input[++i]; }
            else current += c;
            continue;
        }
        if (c == '\'' && !in_d_quote) in_s_quote = !in_s_quote;
        else if (c == '\"' && !in_s_quote) in_d_quote = !in_d_quote;
        else if (c == ' ' && !in_s_quote && !in_d_quote) {
            if (!current.empty()) { args.push_back(current); current.clear(); }
        } else current += c;
    }
    if (!current.empty()) args.push_back(current);
    return args;
}

std::string get_full_path(std::string cmd) {
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    std::stringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        fs::path p = fs::path(dir) / cmd;
        if (fs::exists(p) && (fs::status(p).permissions() & fs::perms::owner_exec) != fs::perms::none)
            return p.string();
    }
    return "";
}

// --- MAIN ---
int main() {
    std::cout << std::unitbuf;
    int next_job_id = 1;

    while (true) {
        reap_finished_jobs(); // Reap before printing prompt

        char* line = readline("$ ");
        if (!line) break;
        std::string input(line);
        if (input.empty()) { free(line); continue; }
        add_history(line);

        std::vector<std::string> args = parse_arguments(input);
        if (args.empty()) { free(line); continue; }

        bool is_background = (args.back() == "&");
        std::string raw_cmd = input;
        if (is_background) args.pop_back();

        // Redirection
        std::string out_f = "", err_f = "";
        bool out_app = false, err_app = false;
        std::vector<std::string> cmd_args;
        for (int i = 0; i < (int)args.size(); ++i) {
            if (args[i] == ">" || args[i] == "1>") { out_f = args[++i]; out_app = false; }
            else if (args[i] == ">>" || args[i] == "1>>") { out_f = args[++i]; out_app = true; }
            else if (args[i] == "2>") { err_f = args[++i]; err_app = false; }
            else if (args[i] == "2>>") { err_f = args[++i]; err_app = true; }
            else cmd_args.push_back(args[i]);
        }

        if (cmd_args.empty()) { free(line); continue; }
        std::string command = cmd_args[0];

        if (command == "exit") { free(line); return 0; }
        
        else if (command == "jobs") {
            // UNIFIED JOBS LOGIC: Check status then print in order
            std::vector<Job> updated_list;
            for (size_t i = 0; i < job_list.size(); ++i) {
                int status;
                if (job_list[i].status == "Running" && waitpid(job_list[i].pid, &status, WNOHANG) > 0) {
                    job_list[i].status = "Done";
                    size_t amp = job_list[i].command.find_last_of('&');
                    if (amp != std::string::npos) {
                        job_list[i].command.erase(amp);
                        job_list[i].command.erase(job_list[i].command.find_last_not_of(" \t") + 1);
                    }
                }

                char marker = ' ';
                if (i == job_list.size() - 1) marker = '+';
                else if (i == job_list.size() - 2) marker = '-';

                std::cout << "[" << job_list[i].id << "]" << marker << "  " 
                          << std::left << std::setw(24) << job_list[i].status 
                          << job_list[i].command << std::endl;

                if (job_list[i].status == "Running") updated_list.push_back(job_list[i]);
            }
            job_list = updated_list;
        }
        else if (command == "echo") {
            for (size_t i = 1; i < cmd_args.size(); ++i) 
                std::cout << cmd_args[i] << (i == cmd_args.size()-1 ? "" : " ");
            std::cout << std::endl;
        }
        else if (command == "type") {
            std::string target = cmd_args[1];
            if (std::find(builtins_list.begin(), builtins_list.end(), target) != builtins_list.end())
                std::cout << target << " is a shell builtin" << std::endl;
            else {
                std::string p = get_full_path(target);
                if (!p.empty()) std::cout << target << " is " << p << std::endl;
                else std::cout << target << ": not found" << std::endl;
            }
        }
        else if (command == "pwd") { std::cout << fs::current_path().string() << std::endl; }
        else if (command == "cd") {
            std::string path = (cmd_args.size() > 1) ? cmd_args[1] : "";
            if (path == "~") path = std::getenv("HOME");
            if (fs::exists(path)) fs::current_path(path);
            else std::cout << "cd: " << path << ": No such file or directory" << std::endl;
        }
        else {
            std::string full_path = get_full_path(command);
            if (!full_path.empty()) {
                pid_t pid = fork();
                if (pid == 0) {
                    if (!out_f.empty()) {
                        int fd = open(out_f.c_str(), O_WRONLY | O_CREAT | (out_app ? O_APPEND : O_TRUNC), 0644);
                        dup2(fd, STDOUT_FILENO); close(fd);
                    }
                    if (!err_f.empty()) {
                        int fd = open(err_f.c_str(), O_WRONLY | O_CREAT | (err_app ? O_APPEND : O_TRUNC), 0644);
                        dup2(fd, STDERR_FILENO); close(fd);
                    }
                    std::vector<char*> c_args;
                    for (auto& a : cmd_args) c_args.push_back(&a[0]);
                    c_args.push_back(nullptr);
                    execvp(c_args[0], c_args.data());
                    exit(1);
                } else {
                    if (is_background) {
                        std::cout << "[" << next_job_id << "] " << pid << std::endl;
                        job_list.push_back({next_job_id++, pid, raw_cmd, "Running"});
                    } else waitpid(pid, nullptr, 0);
                }
            } else std::cout << command << ": command not found" << std::endl;
        }
        free(line);
    }
    return 0;
}