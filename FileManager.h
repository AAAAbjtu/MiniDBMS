#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "Table.h"
#include <string>
#include <vector>

class FileManager {
public:
    static bool ensureDatabase(const std::string& dbName);
    static bool tableExists(const std::string& dbName, const std::string& tableName);
    static std::vector<std::string> listTables(const std::string& dbName);
    static bool dropTable(const std::string& dbName, const std::string& tableName);
    static void save(const std::string& dbName, const Table& table);
    static Table load(const std::string& dbName, const std::string& tableName);
};

#endif