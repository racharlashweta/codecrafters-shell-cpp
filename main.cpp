#include <iostream>
#include <string>

int main() {
    // Flush after every output automatically (Optional but helpful)
    std::cout << std::unitbuf; 

    while (true) {
        // 1. Print the prompt WITHOUT a newline
        std::cout << "$ ";

        // 2. IMPORTANT: Manually flush the buffer so the tester sees it
        std::flush(std::cout);

        std::string input;
        if (!std::getline(std::cin, input)) {
            break; // Handle Ctrl+D (EOF)
        }

        // 3. (Next Stage) Handle the input here...
    }

    return 0;
}