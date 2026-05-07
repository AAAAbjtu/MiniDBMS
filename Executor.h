#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "FileManager.h"
#include "Parser.h"
#include <map>
#include <string>

/**
 * 执行器类
 * 负责执行 SQL 语句
 */
class Executor {
private:
    std::map<std::string, Table> tables;  // 已加载的表
    std::string currentDb = "default";   // 当前数据库名称
    std::string currentUser;   // 当前登录用户（空表示未登录

    /**
     * 确保数据库连接
     * @return 是否连接成功
     */
    bool ensureConnected();
    
    /**
     * 加载表
     * 如果表已加载则返回现有表，否则从文件加载
     * @param tableName 表名
     * @return 表对象引用
     */
    Table& loadTable(const std::string& tableName);
    
    /**
     * 打印查询结果
     * @param table 表对象
     * @param rows 记录列表
     * @param selectedColumns 选择的列名
     */
    void printRows(const Table& table, const std::vector<Record>& rows, const std::vector<std::string>& selectedColumns);

public:
    /**
     * 构造函数
     */
    Executor();
    
    /**
     * 执行 SQL 语句
     * @param pr 解析结果
     */
    void execute(const ParseResult& pr);
};

#endif
