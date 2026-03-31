cat <<EOF > README.md
# Custom POSIX-Compliant Shell (C++17) 🐚

A functional Linux shell built from the ground up, designed to handle command execution, process management, and system-level environment resolution.

### 🚀 Key Features
* **REPL Architecture:** Implemented a robust Read-Eval-Print Loop.
* **Command Execution:** Supports built-in commands (cd, echo, pwd, type, exit) and external binary execution.
* **Advanced Input:** Integrated the GNU Readline library for command history and navigation.
* **PATH Resolution:** Custom logic to locate executables within the Linux filesystem.

### 🛠 Tech Stack
* **Language:** C++17
* **Tools:** CMake, GNU Readline, Linux System Calls
* **Environment:** Ubuntu 24.04 (via GitHub Codespaces)
EOF

git add README.md
git commit -m "docs: update README with professional project summary"
git push origin main
