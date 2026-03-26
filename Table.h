#ifndef TABLE_H
#define TABLE_H

#include "Record.h"
#include <string>
#include <vector>

class Table {
private:
    std::string name;
    std::vector<std::string> columns;
    std::vector<Record> records;

public:
    Table();
    explicit Table(const std::string& name);
    Table(const std::string& name, const std::vector<std::string>& columns);

    const std::string& getName() const;
    const std::vector<std::string>& getColumns() const;
    void setColumns(const std::vector<std::string>& newColumns);
    int columnIndex(const std::string& column) const;
    void insert(const Record& r);
    std::vector<Record> select(const std::string& whereColumn = "", const std::string& whereValue = "") const;
    int deleteRows(const std::string& whereColumn = "", const std::string& whereValue = "");
    int updateRows(const std::string& updateColumn, const std::string& updateValue, const std::string& whereColumn = "", const std::string& whereValue = "");
    std::vector<Record>& getRecords();
    const std::vector<Record>& getRecords() const;
};

#endif