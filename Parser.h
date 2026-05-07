#ifndef PARSER_H
#define PARSER_H

#include "Ast.h"
#include <optional>
#include <string>
#include <vector>

/**
 * 词法单元
 * 参考 MySQL 风格：
 * - WORD：关键字/标识符
 * - STRING：单引号字符串，支持 '' 转义
 * - 反引号/双引号在词法阶段已展开为 WORD 文本
 */
struct Token {
    /**
     * 词法单元类型
     */
    enum Kind {
        WORD,     // 关键字、标识符
        STRING,   // 字符串字面量
        NUMBER,   // 数字字面量
        EQ,       // 等号
        COMMA,    // 逗号
        LPAREN,   // 左括号
        RPAREN,   // 右括号
        SEMICOLON, // 分号
        END       // 结束
    } kind;
    std::string text;  // 词法单元文本
};

/**
 * 解析结果
 * 成功时 stmt 有值；失败时 errorMsg 非空
 */
struct ParseResult {
    std::optional<ast::Stmt> stmt;  // 解析后的语句
    std::string errorMsg;           // 错误信息
};

/**
 * 解析器类
 * 负责将 SQL 字符串解析为抽象语法树
 */
class Parser {
public:
    /**
     * 解析 SQL 语句
     * @param sql SQL 字符串
     * @return 解析结果
     */
    ParseResult parse(const std::string& sql);
};

#endif
