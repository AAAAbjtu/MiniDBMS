/**
 * Table 模块实现
 * 负责表结构和数据操作
 */

#include "Table.h"
#include <algorithm>

/**
 * 默认构造函数
 */
Table::Table() {}

/**
 * 带表名的构造函数
 */
Table::Table(const std::string& n) : name(n) {}

/**
 * 带表名和表结构的构造函数
 */
Table::Table(const std::string& n, const std::vector<ColumnDef>& schema) : name(n), schema_(schema) {}

/**
 * 获取表名
 */
const std::string& Table::getName() const { return name; }

/**
 * 获取表结构
 */
const std::vector<ColumnDef>& Table::getSchema() const { return schema_; }

/**
 * 获取所有列名
 */
std::vector<std::string> Table::getColumnNames() const {
    std::vector<std::string> out;
    out.reserve(schema_.size());
    for (const auto& c : schema_) {
        out.push_back(c.name);
    }
    return out;
}

/**
 * 设置表结构
 */
void Table::setSchema(const std::vector<ColumnDef>& s) { schema_ = s; }

/**
 * 获取列索引
 * @param column 列名
 * @return 列索引，未找到返回 -1
 */
int Table::columnIndex(const std::string& column) const {
    for (size_t i = 0; i < schema_.size(); ++i) {
        if (schema_[i].name == column) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/**
 * 获取列类型
 * @param idx 列索引
 * @return 列类型
 */
SqlType Table::columnType(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(schema_.size())) {
        return SqlType::Text;
    }
    return schema_[idx].type;
}

/**
 * 插入记录
 * @param r 记录对象
 */
void Table::insert(const Record& r) { records.push_back(r); }

/**
 * 检查记录是否匹配 WHERE 条件
 * @param r 记录对象
 * @param idx 列索引
 * @param colType 列类型
 * @param whereRaw WHERE 条件值
 * @return 是否匹配
 */
static bool rowMatchesWhere(const Record& r, int idx, SqlType colType, const std::string& whereRaw) {
    if (idx < 0 || idx >= static_cast<int>(r.cells.size())) {
        return false;
    }
    auto rhs = parseLiteralToCell(whereRaw, colType);
    if (!rhs.has_value()) {
        return false;
    }
    return cellEqualsTyped(r.cells[idx], *rhs, colType);
}

/**
 * 查询记录
 * @param whereColumn WHERE 条件列
 * @param whereValue WHERE 条件值
 * @return 符合条件的记录列表
 */
std::vector<Record> Table::select(const std::string& whereColumn, const std::string& whereValue) const {
    if (whereColumn.empty()) {
        return records;
    }
    std::vector<Record> result;
    int idx = columnIndex(whereColumn);
    if (idx < 0) {
        return result;
    }
    SqlType t = columnType(idx);
    for (const auto& r : records) {
        if (rowMatchesWhere(r, idx, t, whereValue)) {
            result.push_back(r);
        }
    }
    return result;
}

/**
 * 删除记录
 * @param whereColumn WHERE 条件列
 * @param whereValue WHERE 条件值
 * @return 删除的记录数
 */
int Table::deleteRows(const std::string& whereColumn, const std::string& whereValue) {
    if (whereColumn.empty()) {
        int count = static_cast<int>(records.size());
        records.clear();
        return count;
    }
    int idx = columnIndex(whereColumn);
    if (idx < 0) {
        return 0;
    }
    SqlType t = columnType(idx);
    size_t before = records.size();
    records.erase(std::remove_if(records.begin(), records.end(), [&](const Record& r) {
        return rowMatchesWhere(r, idx, t, whereValue);
    }), records.end());
    return static_cast<int>(before - records.size());
}

/**
 * 更新记录
 * @param updateColumn 要更新的列
 * @param updateValue 新值
 * @param whereColumn WHERE 条件列
 * @param whereValue WHERE 条件值
 * @return 更新的记录数
 */
int Table::updateRows(const std::string& updateColumn, const std::string& updateValue, const std::string& whereColumn,
                      const std::string& whereValue) {
    int updateIdx = columnIndex(updateColumn);
    if (updateIdx < 0) {
        return 0;
    }
    SqlType updateType = columnType(updateIdx);
    auto newCell = parseLiteralToCell(updateValue, updateType);
    if (!newCell.has_value()) {
        return 0;
    }
    int whereIdx = whereColumn.empty() ? -1 : columnIndex(whereColumn);
    SqlType whereType = whereIdx >= 0 ? columnType(whereIdx) : SqlType::Text;
    int count = 0;
    for (auto& r : records) {
        bool match = true;
        if (whereIdx >= 0) {
            match = rowMatchesWhere(r, whereIdx, whereType, whereValue);
        }
        if (match && updateIdx < static_cast<int>(r.cells.size())) {
            r.cells[updateIdx] = *newCell;
            ++count;
        }
    }
    return count;
}

/**
 * 获取记录列表（非const版本）
 */
std::vector<Record>& Table::getRecords() { return records; }

/**
 * 获取记录列表（const版本）
 */
const std::vector<Record>& Table::getRecords() const { return records; }

/**
 * 添加列
 * @param columnName 列名
 * @param type 列类型
 */
void Table::addColumn(const std::string& columnName, SqlType type) {
    if (columnIndex(columnName) >= 0) {
        return;
    }
    schema_.push_back({columnName, type});
    for (auto& r : records) {
        switch (type) {
        case SqlType::Int:
            r.cells.push_back(Cell{static_cast<std::int64_t>(0)});
            break;
        case SqlType::Float:
            r.cells.push_back(Cell{0.0});
            break;
        case SqlType::Text:
            r.cells.push_back(Cell{std::string{}});
            break;
        }
    }
}

/**
 * 删除列
 * @param columnName 列名
 */
void Table::dropColumn(const std::string& columnName) {
    int idx = columnIndex(columnName);
    if (idx < 0) {
        return;
    }
    schema_.erase(schema_.begin() + idx);
    for (auto& r : records) {
        if (idx < static_cast<int>(r.cells.size())) {
            r.cells.erase(r.cells.begin() + idx);
        }
    }
}

/**
 * 修改列类型
 * @param columnName 列名
 * @param newType 新类型
 * @return 是否修改成功
 */
bool Table::modifyColumn(const std::string& columnName, SqlType newType) {
    int idx = columnIndex(columnName);
    if (idx < 0) {
        return false;
    }
    SqlType oldType = schema_[static_cast<size_t>(idx)].type;
    if (oldType == newType) {
        return true;
    }
    for (auto& r : records) {
        if (idx < static_cast<int>(r.cells.size())) {
            auto& cell = r.cells[static_cast<size_t>(idx)];
            if (newType == SqlType::Int) {
                double v = 0.0;
                if (std::holds_alternative<std::int64_t>(cell)) {
                    v = static_cast<double>(std::get<std::int64_t>(cell));
                } else if (std::holds_alternative<double>(cell)) {
                    v = std::get<double>(cell);
                } else if (std::holds_alternative<std::string>(cell)) {
                    try {
                        v = std::stod(std::get<std::string>(cell));
                    } catch (...) {
                        v = 0.0;
                    }
                }
                cell = Cell{static_cast<std::int64_t>(v)};
            } else if (newType == SqlType::Float) {
                double v = 0.0;
                if (std::holds_alternative<std::int64_t>(cell)) {
                    v = static_cast<double>(std::get<std::int64_t>(cell));
                } else if (std::holds_alternative<double>(cell)) {
                    v = std::get<double>(cell);
                } else if (std::holds_alternative<std::string>(cell)) {
                    try {
                        v = std::stod(std::get<std::string>(cell));
                    } catch (...) {
                        v = 0.0;
                    }
                }
                cell = Cell{v};
            } else {
                cell = Cell{cellToString(cell)};
            }
        }
    }
    schema_[static_cast<size_t>(idx)].type = newType;
    return true;
}
