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
    mutable std::string status; 
};

std::vector<Job> job_list;
const std::vector<std::string> builtins_list = {"echo", "exit", "type", "pwd", "cd", "jobs"};

// --- HELPERS ---
std::string get_full_path(const std::string& cmd) {
    if (cmd.find('/') != std::string::npos) return fs::exists(cmd) ? cmd : "";
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    std::stringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        try {
            fs::path p = fs::path(dir) / cmd;
            if (fs::exists(p)) return p.string();
        } catch (...) {}
    }
    return "";
}

void reap_finished_jobs() {
    std::vector<Job> active;
    for (size_t i = 0; i < job_list.size(); ++i) {
        int status;
        if (waitpid(job_list[i].pid, &status, WNOHANG) > 0) {
            char marker = (i == job_list.size() - 1) ? '+' : (i == job_list.size() - 2 ? '-' : ' ');
            std::cout << "[" << job_list[i].id << "]" << marker << "  Done                    " << job_list[i].command << std::endl;
        } else { active.push_back(job_list[i]); }
    }
    job_list = active;
}

// --- AUTOCOMPLETE ENGINE ---
char* command_generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static size_t idx;
    if (!state) {
        matches.clear(); idx = 0;
        std::string prefix(text);

        for (const auto& b : builtins_list) if (b.find(prefix) == 0) matches.push_back(b);
        
        try {
            for (const auto& entry : fs::directory_iterator(".")) {
                std::string name = entry.path().filename().string();
                if (name == "." || name == "..") continue;
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
    rl_completion_append_character = ' '; 

    char** matches = rl_completion_matches(text, command_generator);

    if (matches && matches[0] != nullptr && matches[1] == nullptr) {
        if (std::string(matches[0]).back() == '/') rl_completion_append_character = '\0';
    }
    return matches;
}

// --- PARSER ---
std::vector<std::string> tokenize(std::string str) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string temp;
    while (ss >> temp) tokens.push_back(temp);
    return tokens;
}

// --- EXECUTION ENGINE ---
void execute_single_command(std::vector<std::string> args, bool is_bg, std::string raw) {
    if (args.empty()) return;
    const std::string& cmd = args[0];

    // Built-ins
    if (cmd == "exit") exit(0);
    else if (cmd == "pwd") std::cout << fs::current_path().string() << std::endl;
    else if (cmd == "cd") {
        std::string t = (args.size() > 1) ? args[1] : getenv("HOME");
        if (chdir(t.c_str()) != 0) std::cerr << "cd: " << t << ": No such file" << std::endl;
    } else if (cmd == "echo") {
        for (size_t i = 1; i < args.size(); ++i) std::cout << args[i] << (i == args.size()-1 ? "" : " ");
        std::cout << std::endl;
    } else if (cmd == "jobs") {
        for (auto& j : job_list) { int s; if (waitpid(j.pid, &s, WNOHANG) > 0) j.status = "Done"; }
        for (const auto& j : job_list) 
            std::cout << "[" << j.id << "]  " << std::left << std::setw(10) << j.status << j.command << std::endl;
    } else {
        // External Commands
        pid_t pid = fork();
        if (pid == 0) {
            std::string p = get_full_path(cmd);
            if (p.empty()) { std::cerr << cmd << ": command not found" << std::endl; exit(1); }
            std::vector<char*> ca; 
            for (auto& a : args) ca.push_back(const_cast<char*>(a.c_str()));
            ca.push_back(nullptr);
            execvp(p.c_str(), ca.data());
            exit(1);
        } else {
            if (is_bg) {
                int id = job_list.empty() ? 1 : job_list.back().id + 1;
                std::cout << "[" << id << "] " << pid << std::endl;
                job_list.push_back({id, pid, raw, "Running"});
            } else waitpid(pid, nullptr, 0);
        }
    }
}

void execute_pipeline(std::string line) {
    std::vector<std::string> stages;
    std::stringstream ss(line);
    std::string segment;
    while (std::getline(ss, segment, '|')) stages.push_back(segment);

    int num_stages = stages.size();
    int pipefds[2 * (num_stages - 1)];

    for (int i = 0; i < num_stages - 1; i++) pipe(pipefds + i * 2);

    for (int i = 0; i < num_stages; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            if (i > 0) dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
            if (i < num_stages - 1) dup2(pipefds[i * 2 + 1], STDOUT_FILENO);
            for (int j = 0; j < 2 * (num_stages - 1); j++) close(pipefds[j]);
            
            execute_single_command(tokenize(stages[i]), false, "");
            exit(0);
        }
    }

    for (int i = 0; i < 2 * (num_stages - 1); i++) close(pipefds[i]);
    for (int i = 0; i < num_stages; i++) wait(nullptr);
}

// --- MAIN LOOP ---
int main() {
    std::cout << std::unitbuf;
    rl_basic_word_break_characters = " \t\n\"\\'`@$><=;|&{("; 
    rl_attempted_completion_function = my_completion;

    while (true) {
        reap_finished_jobs();
        char* line = readline("$ ");
        if (!line) break;
        if (strlen(line) == 0) { free(line); continue; }
        add_history(line);

        std::string input(line);
        if (input.find('|') != std::string::npos) {
            execute_pipeline(input);
        } else {
            std::vector<std::string> args = tokenize(input);
            bool is_bg = (!args.empty() && args.back() == "&");
            if (is_bg) args.pop_back();
            execute_single_command(args, is_bg, input);
        }
        free(line);
    }
    return 0;
}
