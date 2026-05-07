#include "MiniDB.h"
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

int main() {
    setupConsoleUtf8();
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
