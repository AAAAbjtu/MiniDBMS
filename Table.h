#ifndef TABLE_H
#define TABLE_H

#include "Record.h"
#include <cstddef>
#include <string>
#include <vector>

/**
 * 表类
 * 管理表结构和数据
 */
class Table {
private:
    std::string name;          // 表名
    std::vector<ColumnDef> schema_;  // 表结构（列定义）
    std::vector<Record> records;     // 记录数据

public:
    /**
     * 默认构造函数
     */
    Table();
    
    /**
     * 带表名的构造函数
     * @param name 表名
     */
    explicit Table(const std::string& name);
    
    /**
     * 带表名和表结构的构造函数
     * @param name 表名
     * @param schema 列定义
     */
    Table(const std::string& name, const std::vector<ColumnDef>& schema);

    /**
     * 获取表名
     * @return 表名
     */
    const std::string& getName() const;
    
    /**
     * 获取表结构
     * @return 列定义列表
     */
    const std::vector<ColumnDef>& getSchema() const;
    
    /**
     * 获取所有列名
     * 兼容旧打印逻辑
     * @return 列名列表
     */
    std::vector<std::string> getColumnNames() const;
    
    /**
     * 设置表结构
     * @param s 新的列定义列表
     */
    void setSchema(const std::vector<ColumnDef>& s);
    
    /**
     * 获取列索引
     * @param column 列名
     * @return 列索引，未找到返回 -1
     */
    int columnIndex(const std::string& column) const;
    
    /**
     * 获取列类型
     * @param idx 列索引
     * @return 列类型
     */
    SqlType columnType(int idx) const;

    /**
     * 插入记录
     * @param r 记录对象
     */
    void insert(const Record& r);
    
    /**
     * 查询记录
     * @param whereColumn WHERE 条件列
     * @param whereValue WHERE 条件值
     * @return 符合条件的记录列表
     */
    std::vector<Record> select(const std::string& whereColumn = "", const std::string& whereValue = "") const;
    
    /**
     * 删除记录
     * @param whereColumn WHERE 条件列
     * @param whereValue WHERE 条件值
     * @return 删除的记录数
     */
    int deleteRows(const std::string& whereColumn = "", const std::string& whereValue = "");
    
    /**
     * 更新记录
     * @param updateColumn 要更新的列
     * @param updateValue 新值
     * @param whereColumn WHERE 条件列
     * @param whereValue WHERE 条件值
     * @return 更新的记录数
     */
    int updateRows(const std::string& updateColumn, const std::string& updateValue, const std::string& whereColumn = "",
                   const std::string& whereValue = "");

    /**
     * 获取记录列表（非const版本）
     * @return 记录列表
     */
    std::vector<Record>& getRecords();
    
    /**
     * 获取记录列表（const版本）
     * @return 记录列表
     */
    const std::vector<Record>& getRecords() const;
    
    /**
     * 主键列索引，无则 -1
     */
    int primaryKeyColumnIndex() const;

    /**
     * 当前所有行的主键值是否两两不同（无主键时视为 true）
     */
    bool primaryKeyUniqueAmongRows() const;

    /**
     * PRIMARY KEY 与 UNIQUE 列在现有行中是否两两满足唯一性
     */
    bool allUniqueConstraintsAmongRows() const;

    /**
     * 候选行是否与已有行在某 PRIMARY KEY / UNIQUE 列上冲突
     */
    bool rowConflictsUniqueKeys(const Record& candidate, size_t excludeRowIndex, std::string& errMsg) const;

    /**
     * 候选行是否与已有行主键冲突
     * @param excludeRowIndex 排除的行下标；(size_t)-1 表示不排除任何行
     */
    bool conflictsPrimaryKey(const Record& candidate, size_t excludeRowIndex) const;

    /**
     * 匹配 WHERE 单列等值的行下标（whereColumn 为空则全部行）
     */
    std::vector<size_t> matchingRowIndices(const std::string& whereColumn, const std::string& whereValue) const;

    /**
     * 添加列（含 NOT NULL / PRIMARY KEY 元数据）
     */
    void addColumn(const ColumnDef& col);
    
    /**
     * 删除列
     * @param columnName 列名
     */
    void dropColumn(const std::string& columnName);
    
    /**
     * 修改列类型
     * @param columnName 列名
     * @param newType 新类型
     * @return 是否修改成功
     */
    bool modifyColumn(const std::string& columnName, SqlType newType);
};

#endif
