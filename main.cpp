#include "MiniDB.h"
#include "ApiServer.h"
#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

void setupConsoleUtf8() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
}

std::string trim(const std::string& s) {
    size_t left = 0;
    while (left < s.size() && std::isspace(static_cast<unsigned char>(s[left]))) {
        ++left;
    }
    size_t right = s.size();
    while (right > left && std::isspace(static_cast<unsigned char>(s[right - 1]))) {
        --right;
    }
    return s.substr(left, right - left);
}

std::string toLower(const std::string& s) {
    std::string t = s;
    for (auto& c : t) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return t;
}

bool isExitCommand(const std::string& s) {
    std::string t = toLower(trim(s));
    return t == "exit" || t == "quit" || t == "exit;" || t == "quit;";
}

bool endsWithSemicolon(const std::string& s) {
    size_t i = s.size();
    while (i > 0 && std::isspace(static_cast<unsigned char>(s[i - 1]))) {
        --i;
    }
    return i > 0 && s[i - 1] == ';';
}

} // namespace

int main(int argc, char* argv[]) {
    setupConsoleUtf8();

    bool webMode = false;
    int  port    = 3000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--web") {
            webMode = true;
        } else if (arg == "--port" && i + 1 < argc) {
            try {
                port = std::stoi(argv[++i]);
            } catch (...) {
                std::cerr << "Invalid port number.\n";
                return 1;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "MiniDB - Mini Database Management System\n"
                      << "Usage: MiniDB [options]\n"
                      << "  --web          Start web server mode\n"
                      << "  --port <num>   Port for web server (default: 3000)\n"
                      << "  --help, -h     Show this help\n";
            return 0;
        }
    }

    if (webMode) {
        std::cout << "Starting MiniDB web server on port " << port << "...\n";
        ApiServer server(port);
        server.start();
        return 0;
    }

    MiniDB db;

    std::string line;
    std::string statement;

    std::cout << "MiniDB started. Type SQL and end with ';'.\n";
    std::cout << "Use EXIT or QUIT to leave.\n";

    while (true) {
        std::cout << (statement.empty() ? "miniDB> " : ".....> ");

        if (!std::getline(std::cin, line)) {
            break;
        }

        std::string trimmed = trim(line);
        if (statement.empty() && trimmed.empty()) {
            continue;
        }
        if (statement.empty() && isExitCommand(trimmed)) {
            break;
        }

        if (!statement.empty()) {
            statement += ' ';
        }
        statement += line;

        if (endsWithSemicolon(statement)) {
            db.execute(statement);
            statement.clear();
        }
    }

    return 0;
}
