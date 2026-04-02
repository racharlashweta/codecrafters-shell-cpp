Custom POSIX-Compliant Shell (C++20) 🐚
A high-performance Linux shell built from the ground up, utilizing low-level system calls to handle complex process lifecycles, background job management, and I/O stream redirection.

🚀 Advanced Features
Process Management (Fork/Exec): Unlike basic shells that use std::system, this implementation uses the fork() and execvp() pattern to manage child processes directly, providing full control over the process lifecycle.

Asynchronous Job Control: Supports background execution via the & operator. Implemented a non-blocking "reaping" mechanism using waitpid with WNOHANG to track and clean up background tasks without interrupting the user experience.

I/O Redirection: Direct manipulation of file descriptors (dup2) to support standard output redirection (>) and append mode (>>).

Robust REPL Architecture: A persistent Read-Eval-Print Loop with integrated GNU Readline for command history, line editing, and interactive navigation.

Custom Tokenization & Quoting: Hand-rolled parser for handling single (') and double (") quotes, including backslash escaping for complex string inputs.

Built-in Command Suite: Native implementations of cd, echo, pwd, type, jobs, and exit.

🛠 Tech Stack
Language: C++20 (utilizing std::filesystem and modern container management).

System Calls: fork, execvp, waitpid, dup2, open, chdir.

Libraries: libreadline for professional CLI interaction.

Environment: Development and testing on Ubuntu 24.04.
