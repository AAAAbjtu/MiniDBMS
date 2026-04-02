#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "FileManager.h"
#include "Parser.h"
#include <map>
#include <string>

class Executor {
private:
    std::map<std::string, Table> tables;
    std::string currentDb = "default";
    bool ensureConnected();
    Table& loadTable(const std::string& tableName);
    void printRows(const Table& table, const std::vector<Record>& rows, const std::vector<std::string>& selectedColumns);

public:
    Executor();
    void execute(const Query& q);
};

#endif