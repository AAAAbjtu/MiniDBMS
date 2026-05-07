/**
 * MiniDB 主类
 * 封装了解析器和执行器，提供统一的 SQL 执行接口
 */

#include "MiniDB.h"
#include "Executor.h"
#include "Parser.h"

Parser parser;  // 解析器实例
Executor executor;  // 执行器实例

/** 构造函数 */
MiniDB::MiniDB() {}

/**
 * 执行 SQL 语句
 * @param sql SQL 字符串
 */
void MiniDB::execute(const std::string& sql) {
    ParseResult pr = parser.parse(sql);  // 解析 SQL
    executor.execute(pr);  // 执行解析结果
}