#include "Table.h"
#include <algorithm>

Table::Table() {}
Table::Table(const std::string& n) : name(n) {}
Table::Table(const std::string& n, const std::vector<std::string>& cols) : name(n), columns(cols) {}
const std::string& Table::getName() const { return name; }
const std::vector<std::string>& Table::getColumns() const { return columns; }
void Table::setColumns(const std::vector<std::string>& newColumns) { columns = newColumns; }

int Table::columnIndex(const std::string& column) const {
    // 线性查找列下标；简化版实现先不做索引。
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i] == column) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Table::insert(const Record& r) {
    records.push_back(r);
}

std::vector<Record> Table::select(const std::string& whereColumn, const std::string& whereValue) const {
    // 无 WHERE 时返回全表扫描结果。
    if (whereColumn.empty()) {
        return records;
    }
    std::vector<Record> result;
    int idx = columnIndex(whereColumn);
    if (idx < 0) {
        return result;
    }
    for (const auto& r : records) {
        if (idx < static_cast<int>(r.values.size()) && r.values[idx] == whereValue) {
            result.push_back(r);
        }
    }
    return result;
}

int Table::deleteRows(const std::string& whereColumn, const std::string& whereValue) {
    // 无 WHERE 表示删除整表数据。
    if (whereColumn.empty()) {
        int count = static_cast<int>(records.size());
        records.clear();
        return count;
    }
    int idx = columnIndex(whereColumn);
    if (idx < 0) {
        return 0;
    }
    size_t before = records.size();
    records.erase(std::remove_if(records.begin(), records.end(), [&](const Record& r) {
        return idx < static_cast<int>(r.values.size()) && r.values[idx] == whereValue;
    }), records.end());
    return static_cast<int>(before - records.size());
}

int Table::updateRows(const std::string& updateColumn, const std::string& updateValue, const std::string& whereColumn, const std::string& whereValue) {
    int updateIdx = columnIndex(updateColumn);
    if (updateIdx < 0) {
        return 0;
    }
    // whereIdx = -1 表示更新全部行。
    int whereIdx = whereColumn.empty() ? -1 : columnIndex(whereColumn);
    int count = 0;
    for (auto& r : records) {
        bool match = true;
        if (whereIdx >= 0) {
            match = whereIdx < static_cast<int>(r.values.size()) && r.values[whereIdx] == whereValue;
        }
        if (match && updateIdx < static_cast<int>(r.values.size())) {
            r.values[updateIdx] = updateValue;
            ++count;
        }
    }
    return count;
}

std::vector<Record>& Table::getRecords() { return records; }
const std::vector<Record>& Table::getRecords() const { return records; }