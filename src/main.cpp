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

#include <readline/readline.h>
#include <readline/history.h>

namespace fs = std::filesystem;

std::vector<std::string> builtins_list = {"echo", "exit", "type", "pwd", "cd", "jobs"};

// --- TOKENIZER ---
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
            if (i + 1 < input.length()) {
                char n = input[i+1];
                if (n == '\"' || n == '\\' || n == '$') { current += n; i++; }
                else current += c;
            } else current += c;
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

// --- PATH UTILS ---
std::string get_full_path(std::string cmd) {
    char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    std::stringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        fs::path p = fs::path(dir) / cmd;
        if (fs::exists(p)) {
            auto perms = fs::status(p).permissions();
            if ((perms & fs::perms::owner_exec) != fs::perms::none) return p.string();
        }
    }
    return "";
}

// --- COMPLETION GENERATORS ---
std::vector<std::string> get_command_matches(const std::string& prefix) {
    std::set<std::string> matches;
    for (const auto& b : builtins_list) {
        if (b.compare(0, prefix.length(), prefix) == 0) matches.insert(b);
    }
    char* path_env = std::getenv("PATH");
    if (path_env) {
        std::stringstream ss(path_env);
        std::string dir_path;
        while (std::getline(ss, dir_path, ':')) {
            if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) continue;
            try {
                for (const auto& entry : fs::directory_iterator(dir_path)) {
                    std::string filename = entry.path().filename().string();
                    if (filename.compare(0, prefix.length(), prefix) == 0) {
                        auto perms = entry.status().permissions();
                        if (fs::is_regular_file(entry) && (perms & fs::perms::owner_exec) != fs::perms::none)
                            matches.insert(filename);
                    }
                }
            } catch (...) { continue; }
        }
    }
    return std::vector<std::string>(matches.begin(), matches.end());
}

char* command_generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static size_t match_index;
    if (!state) { matches = get_command_matches(text); match_index = 0; }
    if (match_index < matches.size()) {
        char* res = (char*)malloc(matches[match_index].length() + 1);
        std::strcpy(res, matches[match_index++].c_str());
        return res;
    }
    return nullptr;
}

void custom_display_matches(char** matches, int num_matches, int max_length) {
    std::cout << "\n";
    std::vector<std::string> display_list;
    for (int i = 1; i <= num_matches; ++i) {
        std::string m(matches[i]);
        if (fs::exists(m) && fs::is_directory(m)) { if (m.back() != '/') m += "/"; }
        display_list.push_back(m);
    }
    std::sort(display_list.begin(), display_list.end());
    for (size_t i = 0; i < display_list.size(); ++i)
        std::cout << display_list[i] << (i == display_list.size() - 1 ? "" : "  ");
    std::cout << "\n";
    rl_on_new_line(); rl_redisplay();
}

char** my_completion(const char* text, int start, int end) {
    rl_completion_suppress_append = 0;
    rl_completion_append_character = ' ';
    char** matches = (start == 0) ? rl_completion_matches(text, command_generator) 
                                  : rl_completion_matches(text, rl_filename_completion_function);
    rl_attempted_completion_over = 1;
    if (!matches || !matches[0]) { rl_ding(); return nullptr; }
    if (matches[1] && matches[2]) {
        rl_completion_append_character = '\0';
        if (std::strcmp(matches[0], text) == 0) rl_ding();
    } else {
        if (fs::exists(matches[0]) && fs::is_directory(matches[0])) {
            rl_completion_append_character = '/'; rl_completion_suppress_append = 1;
        }
    }
    return matches;
}

// --- MAIN SHELL ---
int main() {
    std::cout << std::unitbuf;
    rl_attempted_completion_function = my_completion;
    rl_completion_display_matches_hook = custom_display_matches;
    rl_variable_bind("show-all-if-ambiguous", "off");
    rl_variable_bind("bell-style", "audible");

    int job_count = 0;

    while (true) {
        char* line = readline("$ ");
        if (!line) break;
        std::string input(line);
        if (input.empty()) { free(line); continue; }
        add_history(line);

        std::vector<std::string> args = parse_arguments(input);
        free(line);
        if (args.empty()) continue;

        bool is_background = (args.back() == "&");
        if (is_background) args.pop_back();

        // Redirection Parsing
        std::string out_f = "", err_f = "";
        bool out_app = false, err_app = false;
        std::vector<std::string> cmd_args;
        for (int i = 0; i < (int)args.size(); ++i) {
            if (args[i] == ">" || args[i] == "1>") { out_f = args[++i]; out_app = false; }
            else if (args[i] == ">>" || args[i] == "1>>") { out_f = args[++i]; out_app = true; }
            else if (args[i] == "2>") { err_f = args[++i]; err_app = false; }
            else if (args[i] == "2>>") { err_f = args[++i]; err_app = true; }
            else { cmd_args.push_back(args[i]); }
        }

        if (cmd_args.empty()) continue;

        // CRITICAL FIX: Always ensure redirection files are created, even for builtins
        if (!out_f.empty()) {
            fs::path p(out_f);
            if (p.has_parent_path()) fs::create_directories(p.parent_path());
            std::ofstream(out_f, out_app ? std::ios::app : std::ios::out).close();
        }
        if (!err_f.empty()) {
            fs::path p(err_f);
            if (p.has_parent_path()) fs::create_directories(p.parent_path());
            std::ofstream(err_f, err_app ? std::ios::app : std::ios::out).close();
        }

        std::string command = cmd_args[0];

        // Builtin Logic Helper
        auto get_out_stream = [&](std::ofstream &f) -> std::ostream* {
            if (!out_f.empty()) { f.open(out_f, out_app ? std::ios::app : std::ios::out); return &f; }
            return &std::cout;
        };

        if (command == "exit") return 0;
        else if (command == "echo") {
            std::ofstream f; std::ostream* out = get_out_stream(f);
            for (size_t i = 1; i < cmd_args.size(); ++i) *out << cmd_args[i] << (i == cmd_args.size()-1 ? "" : " ");
            *out << std::endl;
        }
        else if (command == "type") {
            std::ofstream f; std::ostream* out = get_out_stream(f);
            std::string target = cmd_args[1];
            if (std::find(builtins_list.begin(), builtins_list.end(), target) != builtins_list.end())
                *out << target << " is a shell builtin" << std::endl;
            else {
                std::string p = get_full_path(target);
                if (!p.empty()) *out << target << " is " << p << std::endl;
                else *out << target << ": not found" << std::endl;
            }
        }
        else if (command == "pwd") {
            std::ofstream f; std::ostream* out = get_out_stream(f);
            *out << fs::current_path().string() << std::endl;
        }
        else if (command == "cd") {
            std::string path = cmd_args.size() > 1 ? cmd_args[1] : "";
            if (path == "~") path = std::getenv("HOME");
            if (fs::exists(path)) fs::current_path(path);
            else std::cout << "cd: " << path << ": No such file or directory" << std::endl;
        }
        else if (command == "jobs") { /* Placeholder */ }
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
                        job_count++;
                        std::cout << "[" << job_count << "] " << pid << std::endl;
                    } else waitpid(pid, nullptr, 0);
                }
            } else std::cout << command << ": command not found" << std::endl;
        }
    }
    return 0;
}