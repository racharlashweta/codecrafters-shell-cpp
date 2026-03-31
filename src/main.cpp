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

std::vector<std::string> builtins_list = {"echo", "exit", "type", "pwd", "cd"};

std::vector<std::string> get_all_matches(const std::string& prefix) {
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
                        if (fs::is_regular_file(entry) && (perms & fs::perms::owner_exec) != fs::perms::none) {
                            matches.insert(filename);
                        }
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
    if (!state) {
        matches = get_all_matches(text);
        match_index = 0;
    }
    if (match_index < matches.size()) {
        char* res = (char*)malloc(matches[match_index].length() + 1);
        std::strcpy(res, matches[match_index++].c_str());
        return res;
    }
    return nullptr;
}

char** my_completion(const char* text, int start, int end) {
    if (start == 0) {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(text, command_generator);
    }
    return nullptr; 
}

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
                char n = input[i + 1];
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

int main() {
    std::cout << std::unitbuf;
    rl_attempted_completion_function = my_completion;
    rl_variable_bind("bell-style", "audible");

    while (true) {
        char* line = readline("$ ");
        if (!line) break; 
        std::string input(line);
        if (input.empty()) { free(line); continue; }
        add_history(line);

        std::vector<std::string> args = parse_arguments(input);
        free(line);
        if (args.empty()) continue;

        std::string out_f = "", err_f = "";
        bool out_app = false, err_app = false;
        int redirect_idx = -1;

        for (int i = 0; i < (int)args.size(); ++i) {
            if (args[i] == ">" || args[i] == "1>") { out_f = args[i+1]; out_app = false; redirect_idx = i; break; }
            else if (args[i] == ">>" || args[i] == "1>>") { out_f = args[i+1]; out_app = true; redirect_idx = i; break; }
            else if (args[i] == "2>") { err_f = args[i+1]; err_app = false; redirect_idx = i; break; }
            else if (args[i] == "2>>") { err_f = args[i+1]; err_app = true; redirect_idx = i; break; }
        }

        std::vector<std::string> cmd_args = args;
        if (redirect_idx != -1) cmd_args.erase(cmd_args.begin() + redirect_idx, cmd_args.end());

        // CRITICAL FIX: Ensure files are created/truncated for EVERY command
        if (!out_f.empty()) {
            fs::path p(out_f);
            if (p.has_parent_path()) fs::create_directories(p.parent_path());
            std::ofstream(out_f, out_app ? std::ios::app : std::ios::out);
        }
        if (!err_f.empty()) {
            fs::path p(err_f);
            if (p.has_parent_path()) fs::create_directories(p.parent_path());
            std::ofstream(err_f, err_app ? std::ios::app : std::ios::out);
        }

        std::string command = cmd_args[0];

        if (command == "exit") return 0;
        else if (command == "echo") {
            std::ostream* out = &std::cout;
            std::ofstream f_out;
            if (!out_f.empty()) {
                f_out.open(out_f, out_app ? std::ios::app : std::ios::out);
                out = &f_out;
            }
            for (size_t i = 1; i < cmd_args.size(); ++i) 
                *out << cmd_args[i] << (i == cmd_args.size() - 1 ? "" : " ");
            *out << "\n";
        }
        else if (command == "type") {
            std::string target = cmd_args[1];
            if (std::find(builtins_list.begin(), builtins_list.end(), target) != builtins_list.end())
                std::cout << target << " is a shell builtin\n";
            else {
                std::string p = get_full_path(target);
                if (!p.empty()) std::cout << target << " is " << p << "\n";
                else std::cout << target << ": not found\n";
            }
        }
        else if (command == "pwd") std::cout << fs::current_path().string() << "\n";
        else if (command == "cd") {
            std::string path = cmd_args.size() > 1 ? cmd_args[1] : "";
            if (path == "~") path = std::getenv("HOME");
            if (fs::exists(path)) fs::current_path(path);
            else std::cout << "cd: " << path << ": No such file or directory\n";
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
                    for (auto& arg : cmd_args) c_args.push_back(&arg[0]);
                    c_args.push_back(nullptr);
                    execvp(c_args[0], c_args.data());
                    exit(1);
                } else wait(nullptr);
            } else std::cout << command << ": command not found\n";
        }
    }
    return 0;
}