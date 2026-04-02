#include "Executor.h"
#include <iostream>

Executor::Executor() {
    // 启动时默认连接到 default 库目录。
    FileManager::ensureDatabase(currentDb);
}

bool Executor::ensureConnected() {
    return FileManager::ensureDatabase(currentDb);
}

Table& Executor::loadTable(const std::string& tableName) {
    auto it = tables.find(tableName);
    if (it != tables.end()) {
        return it->second;
    }
    // 首次访问时从磁盘加载，后续走内存缓存。
    tables[tableName] = FileManager::load(currentDb, tableName);
    return tables[tableName];
}

void Executor::printRows(const Table& table, const std::vector<Record>& rows, const std::vector<std::string>& selectedColumns) {
    std::vector<int> idxs;
    std::vector<std::string> headers;

    // 未指定列时输出全部列，否则按投影列输出。
    if (selectedColumns.empty()) {
        headers = table.getColumns();
        for (size_t i = 0; i < headers.size(); ++i) {
            idxs.push_back(static_cast<int>(i));
        }
    } else {
        for (const auto& c : selectedColumns) {
            int idx = table.columnIndex(c);
            if (idx >= 0) {
                idxs.push_back(idx);
                headers.push_back(c);
            }
        }
    }

    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) {
            std::cout << " | ";
        }
        std::cout << headers[i];
    }
    std::cout << "\n";

    for (const auto& r : rows) {
        for (size_t i = 0; i < idxs.size(); ++i) {
            if (i > 0) {
                std::cout << " | ";
            }
            int idx = idxs[i];
            if (idx < static_cast<int>(r.values.size())) {
                std::cout << r.values[idx];
            }
        }
        std::cout << "\n";
    }
    std::cout << "(" << rows.size() << " rows)\n";
}

void Executor::execute(const Query& q) {
    if (q.type == UNKNOWN) {
        std::cout << "Unsupported SQL.\n";
        return;
    }
    if (!ensureConnected()) {
        std::cout << "Cannot connect db.\n";
        return;
    }

    if (q.type == CONNECT_DB) {
        currentDb = q.dbName.empty() ? "default" : q.dbName;
        // 切库后清空表缓存，避免跨库串数据。
        tables.clear();
        if (FileManager::ensureDatabase(currentDb)) {
            std::cout << "Connected to db: " << currentDb << "\n";
        } else {
            std::cout << "Connect failed.\n";
        }
        return;
    }

    if (q.type == SHOW_TABLES_Q) {
        auto names = FileManager::listTables(currentDb);
        std::cout << "Tables_in_" << currentDb << "\n";
        for (const auto& name : names) {
            std::cout << name << "\n";
        }
        std::cout << "(" << names.size() << " tables)\n";
        return;
    }

    if (q.type == DESC_TABLE_Q) {
        if (!FileManager::tableExists(currentDb, q.table)) {
            std::cout << "Table not found: " << q.table << "\n";
            return;
        }
        Table& t = loadTable(q.table);
        std::cout << "Field\n";
        for (const auto& col : t.getColumns()) {
            std::cout << col << "\n";
        }
        std::cout << "(" << t.getColumns().size() << " columns)\n";
        return;
    }

    if (q.type == DROP_TABLE_Q) {
        if (!FileManager::tableExists(currentDb, q.table)) {
            std::cout << "Table not found: " << q.table << "\n";
            return;
        }
        if (FileManager::dropTable(currentDb, q.table)) {
            tables.erase(q.table);
            std::cout << "Table dropped: " << q.table << "\n";
        } else {
            std::cout << "Drop table failed: " << q.table << "\n";
        }
        return;
    }

    if (q.type == CREATE_TABLE) {
        if (q.columns.empty()) {
            std::cout << "CREATE TABLE requires columns.\n";
            return;
        }
        Table t(q.table, q.columns);
        tables[q.table] = t;
        // 建表后立即持久化 schema。
        FileManager::save(currentDb, tables[q.table]);
        std::cout << "Table created: " << q.table << "\n";
        return;
    }

    if (!FileManager::tableExists(currentDb, q.table)) {
        std::cout << "Table not found: " << q.table << "\n";
        return;
    }

    Table& t = loadTable(q.table);

    if (q.type == INSERT) {
        Record r;
        r.values = q.values;
        if (!q.columns.empty()) {
            // 支持按列插入：先按 schema 补齐，再按列名回填值。
            r.values = std::vector<std::string>(t.getColumns().size(), "");
            for (size_t i = 0; i < q.columns.size() && i < q.values.size(); ++i) {
                int idx = t.columnIndex(q.columns[i]);
                if (idx >= 0) {
                    r.values[idx] = q.values[i];
                }
            }
        }
        if (r.values.size() != t.getColumns().size()) {
            std::cout << "Column count mismatch.\n";
            return;
        }
        t.insert(r);
        // 每次写入后落盘，保证重启可恢复。
        FileManager::save(currentDb, t);
        std::cout << "1 row inserted.\n";
        return;
    }

    if (q.type == SELECT) {
        auto rows = t.select(q.whereColumn, q.whereValue);
        printRows(t, rows, q.selectAll ? std::vector<std::string>{} : q.columns);
        return;
    }

    if (q.type == DELETE_Q) {
        int n = t.deleteRows(q.whereColumn, q.whereValue);
        // 删除后落盘。
        FileManager::save(currentDb, t);
        std::cout << n << " rows deleted.\n";
        return;
    }

    if (q.type == UPDATE_Q) {
        int n = t.updateRows(q.updateColumn, q.updateValue, q.whereColumn, q.whereValue);
        // 更新后落盘。
        FileManager::save(currentDb, t);
        std::cout << n << " rows updated.\n";
        return;
    }
}