#include <iostream>
#include <string>
#include <vector>
#include <set>

int main() {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // 1. Define your builtins in a set for easy searching
  std::set<std::string> builtins = {"exit", "echo", "type"};

  while (true) {
    std::cout << "$ ";
    std::string input;
    if (!std::getline(std::cin, input)) break;

    // 2. Split the input into command and argument
    // Example: "type echo" -> command is "type", argument is "echo"
    size_t space_pos = input.find(' ');
    std::string command = input.substr(0, space_pos);
    std::string argument = (space_pos != std::string::npos) ? input.substr(space_pos + 1) : "";

    if (command == "exit") {
      break; 
    } 
    else if (command == "echo") {
      std::cout << argument << std::endl;
    } 
    else if (command == "type") {
      // 3. Logic for the 'type' command
      if (builtins.count(argument)) {
        std::cout << argument << " is a shell builtin" << std::endl;
      } else {
        std::cout << argument << ": not found" << std::endl;
      }
    } 
    else {
      std::cout << command << ": command not found" << std::endl;
    }
  }
}