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
    int id; pid_t pid; std::string command; mutable std::string status; 
};

std::vector<Job> job_list;
const std::vector<std::string> builtins_list = {"echo", "exit", "type", "pwd", "cd", "jobs"};

// --- REAPING & PATH HELPERS ---
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

// --- COMPLETION LOGIC ---
char* command_generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static size_t idx;
    if (!state) {
        matches.clear(); idx = 0;
        std::string prefix(text);

        // 1. Built-ins
        for (const auto& b : builtins_list) if (b.find(prefix) == 0) matches.push_back(b);
        
        // 2. Current Directory (The fix for #LC6)
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

// --- EXECUTION & PARSING (Condensed) ---
void execute_command(std::vector<std::string> args, bool is_bg, std::string raw) {
    if (args.empty()) return;
    const std::string& cmd = args[0];

    if (cmd == "exit") exit(0);
    else if (cmd == "cd") {
        std::string t = (args.size() > 1) ? args[1] : getenv("HOME");
        if (chdir(t.c_str()) != 0) std::cerr << "cd: " << t << ": No such file" << std::endl;
    } else if (cmd == "jobs") {
        for (auto& j : job_list) { int s; if (waitpid(j.pid, &s, WNOHANG) > 0) j.status = "Done"; }
        for (size_t i = 0; i < job_list.size(); ++i) {
            char m = (i == job_list.size() - 1) ? '+' : (i == job_list.size() - 2 ? '-' : ' ');
            std::cout << "[" << job_list[i].id << "]" << m << "  " << std::left << std::setw(24) << job_list[i].status << job_list[i].command << std::endl;
        }
    } else {
        pid_t pid = fork();
        if (pid == 0) {
            std::string p = get_full_path(cmd);
            std::vector<char*> ca; for (auto& a : args) ca.push_back(const_cast<char*>(a.c_str())); ca.push_back(nullptr);
            execvp(p.c_str(), ca.data()); exit(1);
        } else {
            if (is_bg) { 
                int id = job_list.empty() ? 1 : job_list.back().id + 1;
                std::cout << "[" << id << "] " << pid << std::endl; 
                job_list.push_back({id, pid, raw, "Running"}); 
            } else waitpid(pid, nullptr, 0);
        }
    }
}

int main() {
    std::cout << std::unitbuf;
    
    // SET BREAK CHARACTERS BEFORE STARTING
    rl_basic_word_break_characters = " \t\n\"\\'`@$><=;|&{(";
    rl_attempted_completion_function = my_completion;

    while (true) {
        reap_finished_jobs();
        char* line = readline("$ ");
        if (!line) break;
        if (strlen(line) == 0) { free(line); continue; }
        add_history(line);

        std::string input(line);
        std::stringstream ss(input);
        std::string word;
        std::vector<std::string> args;
        while (ss >> word) args.push_back(word);

        bool is_bg = (!args.empty() && args.back() == "&");
        if (is_bg) args.pop_back();

        execute_command(args, is_bg, input);
        free(line);
    }
    return 0;
}
