#ifndef CONSTRAINTS_H
#define CONSTRAINTS_H

#include "Record.h"
#include "Schema.h"
#include <string>
#include <vector>

class Table;

/**
 * 对一行求值所有列上存储的 CHECK 表达式（任一失败则失败）
 */
bool evaluateAllCheckConstraints(const std::vector<ColumnDef>& schema, const Record& row, std::string& errMsg);

/**
 * 父表中 ref 列是否存在与 value 相等的行（用于外键）
 */
bool parentHasMatchingKeyValue(const Table& parent, const std::string& refColumnName, const Cell& value);

/**
 * 父表列是否可作为外键引用目标（PRIMARY KEY 或 UNIQUE）
 */
bool parentColumnIsFkTarget(const Table& parent, const std::string& refColumnName);

/**
 * 校验 CREATE/ALTER 定义的外键目标是否合法
 * @param creatingTable 正在创建的表名（自引用时与 fk 目标表相同）
 * @param creatingColumns 正在创建的列列表（自引用时用于查找目标列定义）
 */
bool validateForeignKeyDefinition(const std::string& dbName, const std::string& creatingTable,
                                    const std::vector<ColumnDef>& creatingColumns, const std::string& fkRefTable,
                                    const std::string& fkRefCol, std::string& errMsg);

#endif
