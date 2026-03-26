#include "FileManager.h"
#include <cerrno>
#include <direct.h>
#include <fstream>
#include <io.h>
#include <sstream>
#include <vector>

namespace {
// 数据库目录：data_<dbName>
std::string dbDir(const std::string& dbName) {
    return "data_" + dbName;
}

// 表文件：data_<dbName>/<table>.tbl
std::string tableFile(const std::string& dbName, const std::string& tableName) {
    return dbDir(dbName) + "/" + tableName + ".tbl";
}
}

bool FileManager::ensureDatabase(const std::string& dbName) {
    if (dbName.empty()) {
        return false;
    }
    std::string dir = dbDir(dbName);
    // 目录不存在则创建；存在则直接复用。
    int rc = _mkdir(dir.c_str());
    return rc == 0 || errno == EEXIST;
}

bool FileManager::tableExists(const std::string& dbName, const std::string& tableName) {
    std::ifstream f(tableFile(dbName, tableName));
    return f.good();
}

std::vector<std::string> FileManager::listTables(const std::string& dbName) {
    std::vector<std::string> result;
    std::string pattern = dbDir(dbName) + "/*.tbl";
    _finddata_t fileInfo;
    intptr_t handle = _findfirst(pattern.c_str(), &fileInfo);
    if (handle == -1) {
        return result;
    }

    do {
        std::string name = fileInfo.name;
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".tbl") {
            result.push_back(name.substr(0, name.size() - 4));
        }
    } while (_findnext(handle, &fileInfo) == 0);

    _findclose(handle);
    return result;
}

bool FileManager::dropTable(const std::string& dbName, const std::string& tableName) {
    std::string path = tableFile(dbName, tableName);
    return std::remove(path.c_str()) == 0;
}

void FileManager::save(const std::string& dbName, const Table& table) {
    ensureDatabase(dbName);
    std::ofstream file(tableFile(dbName, table.getName()), std::ios::trunc);
    // 第一行写入 schema，后续每行一条记录。
    file << "#schema";
    for (const auto& c : table.getColumns()) {
        file << "," << c;
    }
    file << "\n";
    for (const auto& r : table.getRecords()) {
        for (size_t i = 0; i < r.values.size(); ++i) {
            if (i > 0) {
                file << ",";
            }
            file << r.values[i];
        }
        file << "\n";
    }
}

Table FileManager::load(const std::string& dbName, const std::string& tableName) {
    std::ifstream file(tableFile(dbName, tableName));
    if (!file.good()) {
        return Table(tableName);
    }

    std::string line;
    Table t(tableName);
    bool firstLine = true;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string v;
        std::vector<std::string> parts;
        while (std::getline(ss, v, ',')) {
            parts.push_back(v);
        }
        if (firstLine) {
            firstLine = false;
            // 解析 schema 头：#schema,col1,col2,...
            if (!parts.empty() && parts[0] == "#schema") {
                std::vector<std::string> cols;
                for (size_t i = 1; i < parts.size(); ++i) {
                    cols.push_back(parts[i]);
                }
                t.setColumns(cols);
                continue;
            }
        }
        Record r;
        for (const auto& p : parts) {
            r.values.push_back(p);
        }
        t.insert(r);
    }
    return t;
}