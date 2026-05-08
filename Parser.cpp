/**
 * Parser 模块实现
 * 负责 SQL 语句的词法分析和语法分析
 */

#include "Parser.h"
#include "Schema.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace {

/** 去除字符串首尾空白 */
std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

/** 字符串转大写 */
std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

/** 关键字比较（忽略大小写） */
bool eqKw(const std::string& a, const char* b) {
    return upper(a) == upper(std::string(b));
}

/**
 * 词法分析
 * 处理：单引号字符串、反引号/双引号标识符、数字、运算符等
 */
bool lexSql(const std::string& sql, std::vector<Token>& out, std::string& err) {
    size_t i = 0;
    const size_t n = sql.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(sql[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;  // 跳过空白字符
            continue;
        }
        if (c == '\'') {
            // 处理单引号字符串（支持 '' 转义）
            ++i;
            std::string s;
            while (i < n) {
                if (sql[i] == '\'') {
                    if (i + 1 < n && sql[i + 1] == '\'') {
                        s += '\'';
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                s += sql[i++];
            }
            out.push_back({Token::STRING, s});
            continue;
        }
        if (c == '`') {
            // 处理反引号标识符
            ++i;
            std::string s;
            while (i < n && sql[i] != '`') {
                s += sql[i++];
            }
            if (i >= n) {
                err = "未闭合的反引号标识符";
                return false;
            }
            ++i;
            out.push_back({Token::WORD, s});
            continue;
        }
        if (c == '"') {
            // 处理双引号标识符
            ++i;
            std::string s;
            while (i < n && sql[i] != '"') {
                s += sql[i++];
            }
            if (i >= n) {
                err = "未闭合的双引号标识符";
                return false;
            }
            ++i;
            out.push_back({Token::WORD, s});
            continue;
        }
        if (c == '=') {
            out.push_back({Token::EQ, ""});
            ++i;
            continue;
        }
        if (c == '<') {
            if (i + 1 < n && sql[i + 1] == '=') {
                out.push_back({Token::WORD, "<="});
                i += 2;
                continue;
            }
            if (i + 1 < n && sql[i + 1] == '>') {
                out.push_back({Token::WORD, "<>"});
                i += 2;
                continue;
            }
            out.push_back({Token::WORD, "<"});
            ++i;
            continue;
        }
        if (c == '>') {
            if (i + 1 < n && sql[i + 1] == '=') {
                out.push_back({Token::WORD, ">="});
                i += 2;
                continue;
            }
            out.push_back({Token::WORD, ">"});
            ++i;
            continue;
        }
        if (c == '!' && i + 1 < n && sql[i + 1] == '=') {
            out.push_back({Token::WORD, "!="});
            i += 2;
            continue;
        }
        if (c == ',') {
            out.push_back({Token::COMMA, ""});
            ++i;
            continue;
        }
        if (c == '(') {
            out.push_back({Token::LPAREN, ""});
            ++i;
            continue;
        }
        if (c == ')') {
            out.push_back({Token::RPAREN, ""});
            ++i;
            continue;
        }
        if (c == ';') {
            out.push_back({Token::SEMICOLON, ""});
            ++i;
            continue;
        }
        if (c == '*') {
            out.push_back({Token::WORD, "*"});
            ++i;
            continue;
        }
        if (c == '-' && i + 1 < n &&
            (std::isdigit(static_cast<unsigned char>(sql[i + 1])) ||
             (sql[i + 1] == '.' && i + 2 < n && std::isdigit(static_cast<unsigned char>(sql[i + 2]))))) {
            // 处理负数
            std::string s;
            s += '-';
            ++i;
            bool dot = false;
            while (i < n) {
                unsigned char ch = static_cast<unsigned char>(sql[i]);
                if (std::isdigit(ch)) {
                    s += sql[i++];
                } else if (ch == '.' && !dot) {
                    dot = true;
                    s += sql[i++];
                } else {
                    break;
                }
            }
            out.push_back({Token::NUMBER, s});
            continue;
        }
        if (std::isdigit(c) ||
            (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(sql[i + 1])))) {
            // 处理数字
            std::string s;
            if (c == '.') {
                s += sql[i++];
            } else {
                while (i < n && std::isdigit(static_cast<unsigned char>(sql[i]))) {
                    s += sql[i++];
                }
                if (i < n && sql[i] == '.') {
                    s += sql[i++];
                }
            }
            while (i < n && std::isdigit(static_cast<unsigned char>(sql[i]))) {
                s += sql[i++];
            }
            out.push_back({Token::NUMBER, s});
            continue;
        }
        if (std::isalpha(c) || c == '_') {
            // 处理标识符和关键字
            std::string s;
            while (i < n) {
                unsigned char c2 = static_cast<unsigned char>(sql[i]);
                if (std::isalnum(c2) || c2 == '_') {
                    s += sql[i++];
                } else {
                    break;
                }
            }
            out.push_back({Token::WORD, s});
            continue;
        }
        err = "无法识别的字符";
        return false;
    }
    out.push_back({Token::END, ""});
    return true;
}

/** 获取指定位置的 Token */
const Token& peek(const std::vector<Token>& t, size_t pos) { return t[pos]; }

/** 跳过分号 */
void skipSemi(const std::vector<Token>& t, size_t& pos) {
    if (pos < t.size() && peek(t, pos).kind == Token::SEMICOLON) {
        ++pos;
    }
}

/** 获取 Token 的文本值 */
std::string atomText(const Token& tok) {
    if (tok.kind == Token::STRING || tok.kind == Token::WORD || tok.kind == Token::NUMBER) {
        return tok.text;
    }
    return "";
}

/** 期望关键字并跳过 */
bool expectWord(const std::vector<Token>& t, size_t& pos, const char* kw) {
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    if (!eqKw(peek(t, pos).text, kw)) {
        return false;
    }
    ++pos;
    return true;
}

/** 将 CHECK 子句中的 Token 还原为 Constraints 模块可解析的文本 */
std::string tokenToCheckFragment(const Token& tok) {
    switch (tok.kind) {
    case Token::WORD:
        return tok.text;
    case Token::NUMBER:
        return tok.text;
    case Token::STRING: {
        std::string o = "'";
        for (char c : tok.text) {
            if (c == '\'') {
                o += "''";
            } else {
                o += c;
            }
        }
        o += '\'';
        return o;
    }
    case Token::EQ:
        return "=";
    case Token::LPAREN:
        return "(";
    case Token::RPAREN:
        return ")";
    case Token::COMMA:
        return ",";
    default:
        return "";
    }
}

std::string serializeCheckExprFromTokens(const std::vector<Token>& t, size_t start, size_t endExclusive) {
    std::string o;
    for (size_t j = start; j < endExclusive; ++j) {
        std::string frag = tokenToCheckFragment(t[j]);
        if (frag.empty()) {
            continue;
        }
        if (!o.empty()) {
            o += ' ';
        }
        o += frag;
    }
    return o;
}

/**
 * 解析列上的可选约束：NOT NULL、PRIMARY KEY、UNIQUE、REFERENCES、CHECK（可多次、顺序任意）
 */
bool parseColumnPostConstraints(const std::vector<Token>& t, size_t& pos, ColumnDef& col) {
    while (pos < t.size() && peek(t, pos).kind == Token::WORD) {
        std::string w = upper(peek(t, pos).text);
        if (w == "NOT") {
            if (pos + 1 >= t.size() || peek(t, pos + 1).kind != Token::WORD || !eqKw(peek(t, pos + 1).text, "NULL")) {
                return false;
            }
            col.notNull = true;
            pos += 2;
            continue;
        }
        if (w == "PRIMARY") {
            if (pos + 1 >= t.size() || peek(t, pos + 1).kind != Token::WORD || !eqKw(peek(t, pos + 1).text, "KEY")) {
                return false;
            }
            col.primaryKey = true;
            col.notNull = true;
            pos += 2;
            continue;
        }
        if (w == "UNIQUE") {
            col.unique = true;
            ++pos;
            continue;
        }
        if (w == "REFERENCES") {
            ++pos;
            if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
                return false;
            }
            col.fkRefTable = peek(t, pos).text;
            ++pos;
            if (pos >= t.size() || peek(t, pos).kind != Token::LPAREN) {
                return false;
            }
            ++pos;
            if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
                return false;
            }
            col.fkRefCol = peek(t, pos).text;
            ++pos;
            if (pos >= t.size() || peek(t, pos).kind != Token::RPAREN) {
                return false;
            }
            ++pos;
            continue;
        }
        if (w == "CHECK") {
            ++pos;
            if (pos >= t.size() || peek(t, pos).kind != Token::LPAREN) {
                return false;
            }
            ++pos;
            size_t innerStart = pos;
            int depth = 1;
            while (pos < t.size() && depth > 0) {
                if (peek(t, pos).kind == Token::LPAREN) {
                    ++depth;
                    ++pos;
                } else if (peek(t, pos).kind == Token::RPAREN) {
                    --depth;
                    if (depth == 0) {
                        col.checkExpr = serializeCheckExprFromTokens(t, innerStart, pos);
                        ++pos;
                        break;
                    }
                    ++pos;
                } else {
                    ++pos;
                }
            }
            if (depth != 0) {
                return false;
            }
            continue;
        }
        break;
    }
    return true;
}

/**
 * 解析 CREATE TABLE 语句
 * 格式：CREATE TABLE name ( col TYPE [NOT NULL] [PRIMARY KEY] [, ...] );
 */
bool parseCreateTable(const std::vector<Token>& t, size_t& pos, ast::CreateTableStmt& out) {
    if (!expectWord(t, pos, "CREATE") || !expectWord(t, pos, "TABLE")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;
    if (pos >= t.size() || peek(t, pos).kind != Token::LPAREN) {
        return false;
    }
    ++pos;
    while (pos < t.size() && peek(t, pos).kind == Token::WORD) {
        ColumnDef col;
        col.name = peek(t, pos).text;
        ++pos;
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        auto ty = tryParseSqlTypeKeyword(peek(t, pos).text);
        if (!ty.has_value()) {
            return false;
        }
        col.type = *ty;
        ++pos;
        if (!parseColumnPostConstraints(t, pos, col)) {
            return false;
        }
        for (const auto& c : out.columns) {
            if (c.name == col.name) {
                return false;
            }
        }
        out.columns.push_back(std::move(col));
        if (pos < t.size() && peek(t, pos).kind == Token::COMMA) {
            ++pos;
        } else {
            break;
        }
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::RPAREN) {
        return false;
    }
    ++pos;
    if (out.columns.empty()) {
        return false;
    }
    int pkCount = 0;
    for (const auto& c : out.columns) {
        if (c.primaryKey) {
            ++pkCount;
        }
    }
    if (pkCount > 1) {
        return false;
    }
    skipSemi(t, pos);
    return true;
}

/**
 * 解析 INSERT 语句
 * 格式：INSERT INTO table [(col1, col2)] VALUES (v1, v2) 或 VALUES v1, v2
 */
bool parseInsert(const std::vector<Token>& t, size_t& pos, ast::InsertStmt& out) {
    if (!expectWord(t, pos, "INSERT") || !expectWord(t, pos, "INTO")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;
    if (pos < t.size() && peek(t, pos).kind == Token::LPAREN) {
        // 解析指定列
        ++pos;
        while (pos < t.size() && peek(t, pos).kind == Token::WORD) {
            out.columns.push_back(peek(t, pos).text);
            ++pos;
            if (peek(t, pos).kind == Token::COMMA) {
                ++pos;
            } else {
                break;
            }
        }
        if (pos >= t.size() || peek(t, pos).kind != Token::RPAREN) {
            return false;
        }
        ++pos;
    }
    if (!expectWord(t, pos, "VALUES")) {
        return false;
    }
    // 解析值列表
    if (pos < t.size() && peek(t, pos).kind == Token::LPAREN) {
        ++pos;
        while (pos < t.size()) {
            Token::Kind k = peek(t, pos).kind;
            if (k == Token::STRING || k == Token::WORD || k == Token::NUMBER) {
                out.values.push_back(atomText(peek(t, pos)));
                ++pos;
            } else {
                break;
            }
            if (pos < t.size() && peek(t, pos).kind == Token::COMMA) {
                ++pos;
                continue;
            }
            break;
        }
        if (pos >= t.size() || peek(t, pos).kind != Token::RPAREN) {
            return false;
        }
        ++pos;
    } else {
        // 无括号格式（兼容旧版）
        while (pos < t.size() && peek(t, pos).kind != Token::SEMICOLON && peek(t, pos).kind != Token::END) {
            Token::Kind k = peek(t, pos).kind;
            if (k == Token::STRING || k == Token::WORD || k == Token::NUMBER) {
                out.values.push_back(atomText(peek(t, pos)));
                ++pos;
            } else {
                break;
            }
            if (pos < t.size() && peek(t, pos).kind == Token::COMMA) {
                ++pos;
            }
        }
    }
    skipSemi(t, pos);
    return true;
}

/**
 * 解析 WHERE 子句
 * 格式：WHERE col = value
 */
bool parseWhere(const std::vector<Token>& t, size_t& pos, std::string& col, std::string& val) {
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD || !eqKw(peek(t, pos).text, "WHERE")) {
        return true;  // 没有 WHERE 子句
    }
    ++pos;
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    col = peek(t, pos).text;
    ++pos;
    if (pos < t.size() && peek(t, pos).kind == Token::EQ) {
        ++pos;
    }
    if (pos >= t.size()) {
        return false;
    }
    Token::Kind vk = peek(t, pos).kind;
    if (vk != Token::STRING && vk != Token::WORD && vk != Token::NUMBER) {
        return false;
    }
    val = atomText(peek(t, pos));
    ++pos;
    return true;
}

/**
 * 解析 SELECT 语句
 * 格式：SELECT *|col1,col2 FROM table [WHERE ...]
 */
bool parseSelect(const std::vector<Token>& t, size_t& pos, ast::SelectStmt& out) {
    if (!expectWord(t, pos, "SELECT")) {
        return false;
    }
    if (pos >= t.size()) {
        return false;
    }
    if (peek(t, pos).kind == Token::WORD && peek(t, pos).text == "*") {
        out.selectAll = true;
        ++pos;
    } else {
        // 解析指定列
        while (pos < t.size() && peek(t, pos).kind == Token::WORD && !eqKw(peek(t, pos).text, "FROM")) {
            out.columns.push_back(peek(t, pos).text);
            ++pos;
            if (pos < t.size() && peek(t, pos).kind == Token::COMMA) {
                ++pos;
            } else {
                break;
            }
        }
    }
    if (!expectWord(t, pos, "FROM")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;
    if (!parseWhere(t, pos, out.whereColumn, out.whereValue)) {
        return false;
    }
    skipSemi(t, pos);
    return true;
}

/**
 * 解析 DELETE 语句
 * 格式：DELETE FROM table [WHERE ...]
 */
bool parseDelete(const std::vector<Token>& t, size_t& pos, ast::DeleteStmt& out) {
    if (!expectWord(t, pos, "DELETE") || !expectWord(t, pos, "FROM")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;
    if (!parseWhere(t, pos, out.whereColumn, out.whereValue)) {
        return false;
    }
    skipSemi(t, pos);
    return true;
}

/**
 * 解析 UPDATE 语句
 * 格式：UPDATE table SET col = value [WHERE ...]
 */
bool parseUpdate(const std::vector<Token>& t, size_t& pos, ast::UpdateStmt& out) {
    if (!expectWord(t, pos, "UPDATE")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;
    if (!expectWord(t, pos, "SET")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.setColumn = peek(t, pos).text;
    ++pos;
    if (pos < t.size() && peek(t, pos).kind == Token::EQ) {
        ++pos;
    }
    if (pos >= t.size()) {
        return false;
    }
    Token::Kind vk = peek(t, pos).kind;
    if (vk != Token::STRING && vk != Token::WORD && vk != Token::NUMBER) {
        return false;
    }
    out.setValue = atomText(peek(t, pos));
    ++pos;
    if (!parseWhere(t, pos, out.whereColumn, out.whereValue)) {
        return false;
    }
    skipSemi(t, pos);
    return true;
}

/**
 * 解析 ALTER TABLE 语句
 * 格式：ALTER TABLE t ADD|DROP|MODIFY COLUMN name TYPE
 */
bool parseAlterTable(const std::vector<Token>& t, size_t& pos, ast::AlterTableStmt& out) {
    if (!expectWord(t, pos, "ALTER") || !expectWord(t, pos, "TABLE")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;

    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    std::string opStr = upper(peek(t, pos).text);
    ++pos;

    if (opStr == "ADD") {
        // ADD COLUMN
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        out.op = ast::AlterOperation::AddColumn;
        out.addNotNull = false;
        out.addPrimaryKey = false;
        out.addUnique = false;
        out.addCheckExpr.clear();
        out.addFkRefTable.clear();
        out.addFkRefCol.clear();
        out.columnName = peek(t, pos).text;
        ++pos;
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        auto ty = tryParseSqlTypeKeyword(peek(t, pos).text);
        if (!ty.has_value()) {
            return false;
        }
        out.columnType = *ty;
        ++pos;
        ColumnDef tmp;
        tmp.name = out.columnName;
        tmp.type = out.columnType;
        if (!parseColumnPostConstraints(t, pos, tmp)) {
            return false;
        }
        out.addNotNull = tmp.notNull;
        out.addPrimaryKey = tmp.primaryKey;
        out.addUnique = tmp.unique;
        out.addCheckExpr = tmp.checkExpr;
        out.addFkRefTable = tmp.fkRefTable;
        out.addFkRefCol = tmp.fkRefCol;
    } else if (opStr == "DROP") {
        // DROP COLUMN
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD || !eqKw(peek(t, pos).text, "COLUMN")) {
            return false;
        }
        ++pos;
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        out.op = ast::AlterOperation::DropColumn;
        out.columnName = peek(t, pos).text;
        ++pos;
    } else if (opStr == "MODIFY") {
        // MODIFY COLUMN
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD || !eqKw(peek(t, pos).text, "COLUMN")) {
            return false;
        }
        ++pos;
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        out.op = ast::AlterOperation::ModifyColumn;
        out.columnName = peek(t, pos).text;
        ++pos;
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        auto ty = tryParseSqlTypeKeyword(peek(t, pos).text);
        if (!ty.has_value()) {
            return false;
        }
        out.columnType = *ty;
        ++pos;
    } else {
        return false;
    }

    skipSemi(t, pos);
    return true;
}

} // namespace

/**
 * 解析 SQL 语句
 * @param sql SQL 字符串
 * @return 解析结果，包含 AST 或错误信息
 */
ParseResult Parser::parse(const std::string& sql) {
    ParseResult r;

    std::string raw = trim(sql);
    if (raw.empty()) {
        return r;
    }

    std::vector<Token> tokens;
    std::string lexErr;
    if (!lexSql(raw, tokens, lexErr)) {
        r.errorMsg = lexErr;
        return r;
    }

    size_t pos = 0;
    if (peek(tokens, pos).kind == Token::END) {
        return r;
    }

    if (peek(tokens, pos).kind != Token::WORD) {
        r.errorMsg = "语句应以关键字开头";
        return r;
    }

    std::string kw = upper(peek(tokens, pos).text);

    // 解析 CONNECT 语句
    if (kw == "CONNECT") {
        ++pos;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "CONNECT 需要数据库名";
            return r;
        }
        ast::ConnectStmt c;
        c.database = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(c)};
        return r;
    }

    // 解析 CREATE DATABASE 语句
    if (kw == "CREATE" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "DATABASE")) {
        pos += 2;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "CREATE DATABASE 需要数据库名";
            return r;
        }
        ast::CreateDatabaseStmt c;
        c.database = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(c)};
        return r;
    }

    // 解析 CREATE USER 语句
    if (kw == "CREATE" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "USER")) {
        pos += 2;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "CREATE USER 需要用户名";
            return r;
        }
        ast::CreateUserStmt u;
        u.username = peek(tokens, pos).text;
        u.hasRole = false;
        u.role = ast::RoleType::USER;
        ++pos;
        if (pos >= tokens.size() || !eqKw(peek(tokens, pos).text, "IDENTIFIED")) {
            r.errorMsg = "CREATE USER 语法错误（需: CREATE USER username IDENTIFIED BY 'password' [ROLE ADMIN]）";
            return r;
        }
        ++pos;
        if (pos >= tokens.size() || !eqKw(peek(tokens, pos).text, "BY")) {
            r.errorMsg = "CREATE USER 语法错误（需: CREATE USER username IDENTIFIED BY 'password' [ROLE ADMIN]）";
            return r;
        }
        ++pos;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::STRING) {
            r.errorMsg = "CREATE USER 需要密码字符串";
            return r;
        }
        u.password = peek(tokens, pos).text;
        ++pos;

        // 解析可选的 ROLE 子句
        if (pos < tokens.size() && eqKw(peek(tokens, pos).text, "ROLE")) {
            ++pos;
            if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
                r.errorMsg = "CREATE USER ROLE 需要指定角色（ADMIN/DBA/USER）";
                return r;
            }
            std::string roleText = upper(peek(tokens, pos).text);
            u.hasRole = true;
            if (roleText == "ADMIN") {
                u.role = ast::RoleType::ADMIN;
            } else if (roleText == "DBA") {
                u.role = ast::RoleType::DBA;
            } else if (roleText == "USER") {
                u.role = ast::RoleType::USER;
            } else {
                r.errorMsg = "CREATE USER ROLE 必须是 ADMIN/DBA/USER";
                return r;
            }
            ++pos;
        }

        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(u)};
        return r;
    }

    // 解析 DROP DATABASE 语句
    if (kw == "DROP" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "DATABASE")) {
        pos += 2;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "DROP DATABASE 需要数据库名";
            return r;
        }
        ast::DropDatabaseStmt d;
        d.database = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(d)};
        return r;
    }

    // 解析 DROP USER 语句
    if (kw == "DROP" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "USER")) {
        pos += 2;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "DROP USER 需要用户名";
            return r;
        }
        ast::DropUserStmt u;
        u.username = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(u)};
        return r;
    }

    // 解析 SHOW TABLES 语句
    if (kw == "SHOW" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "TABLES")) {
        pos += 2;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{ast::ShowTablesStmt{}};
        return r;
    }

    // 解析 SHOW DATABASES 语句
    if (kw == "SHOW" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "DATABASES")) {
        pos += 2;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{ast::ShowDatabasesStmt{}};
        return r;
    }

    // 解析 SHOW USERS 语句
    if (kw == "SHOW" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "USERS")) {
        pos += 2;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{ast::ShowUsersStmt{}};
        return r;
    }

    // 解析 LOGOUT 语句
    if (kw == "LOGOUT") {
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{ast::LogoutStmt{}};
        return r;
    }

    // 解析 LOGIN 语句
    if (kw == "LOGIN") {
        ++pos;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "LOGIN 需要用户名";
            return r;
        }
        ast::LoginStmt l;
        l.username = peek(tokens, pos).text;
        ++pos;
        if (pos >= tokens.size() || !eqKw(peek(tokens, pos).text, "IDENTIFIED")) {
            r.errorMsg = "LOGIN 语法错误（需: LOGIN username IDENTIFIED BY 'password'）";
            return r;
        }
        ++pos;
        if (pos >= tokens.size() || !eqKw(peek(tokens, pos).text, "BY")) {
            r.errorMsg = "LOGIN 语法错误（需: LOGIN username IDENTIFIED BY 'password'）";
            return r;
        }
        ++pos;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::STRING) {
            r.errorMsg = "LOGIN 需要密码字符串";
            return r;
        }
        l.password = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(l)};
        return r;
    }

    // 解析 CREATE ROLE 语句
    if (kw == "CREATE" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "ROLE")) {
        pos += 2;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "CREATE ROLE 需要角色名";
            return r;
        }
        ast::CreateRoleStmt stmt;
        std::string roleStr = upper(peek(tokens, pos).text);
        if (roleStr == "ADMIN") stmt.role = ast::RoleType::ADMIN;
        else if (roleStr == "DBA") stmt.role = ast::RoleType::DBA;
        else if (roleStr == "USER") stmt.role = ast::RoleType::USER;
        else {
            r.errorMsg = "CREATE ROLE 角色名必须是 ADMIN, DBA 或 USER";
            return r;
        }
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(stmt)};
        return r;
    }

    // 解析 DROP ROLE 语句
    if (kw == "DROP" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "ROLE")) {
        pos += 2;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "DROP ROLE 需要角色名";
            return r;
        }
        ast::DropRoleStmt stmt;
        std::string roleStr = upper(peek(tokens, pos).text);
        if (roleStr == "ADMIN") stmt.role = ast::RoleType::ADMIN;
        else if (roleStr == "DBA") stmt.role = ast::RoleType::DBA;
        else if (roleStr == "USER") stmt.role = ast::RoleType::USER;
        else {
            r.errorMsg = "DROP ROLE 角色名必须是 ADMIN, DBA 或 USER";
            return r;
        }
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(stmt)};
        return r;
    }

    // 解析 GRANT 语句
    if (kw == "GRANT") {
        ++pos;
        ast::GrantStmt stmt;

        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "GRANT 需要权限或角色名";
            return r;
        }

        std::string first = upper(peek(tokens, pos).text);

        if (first == "ADMIN" || first == "DBA" || first == "USER") {
            stmt.hasRole = true;
            if (first == "ADMIN") stmt.role = ast::RoleType::ADMIN;
            else if (first == "DBA") stmt.role = ast::RoleType::DBA;
            else stmt.role = ast::RoleType::USER;
            ++pos;
        } else {
            while (true) {
                std::string privStr = upper(peek(tokens, pos).text);
                if (privStr == "CREATE_USER") stmt.privileges.push_back(ast::Privilege::CREATE_USER);
                else if (privStr == "DROP_USER") stmt.privileges.push_back(ast::Privilege::DROP_USER);
                else if (privStr == "SHOW_USERS") stmt.privileges.push_back(ast::Privilege::SHOW_USERS);
                else if (privStr == "CREATE_ROLE") stmt.privileges.push_back(ast::Privilege::CREATE_ROLE);
                else if (privStr == "DROP_ROLE") stmt.privileges.push_back(ast::Privilege::DROP_ROLE);
                else if (privStr == "GRANT") stmt.privileges.push_back(ast::Privilege::GRANT);
                else if (privStr == "REVOKE") stmt.privileges.push_back(ast::Privilege::REVOKE);
                else if (privStr == "SHOW_GRANTS") stmt.privileges.push_back(ast::Privilege::SHOW_GRANTS);
                else if (privStr == "SHOW_AUDIT") stmt.privileges.push_back(ast::Privilege::SHOW_AUDIT);
                else if (privStr == "CREATE_DB") stmt.privileges.push_back(ast::Privilege::CREATE_DB);
                else if (privStr == "DROP_DB") stmt.privileges.push_back(ast::Privilege::DROP_DB);
                else if (privStr == "SHOW_DATABASES") stmt.privileges.push_back(ast::Privilege::SHOW_DATABASES);
                else if (privStr == "CONNECT") stmt.privileges.push_back(ast::Privilege::CONNECT);
                else if (privStr == "CREATE_TABLE") stmt.privileges.push_back(ast::Privilege::CREATE_TABLE);
                else if (privStr == "DROP_TABLE") stmt.privileges.push_back(ast::Privilege::DROP_TABLE);
                else if (privStr == "ALTER_TABLE") stmt.privileges.push_back(ast::Privilege::ALTER_TABLE);
                else if (privStr == "SHOW_TABLES") stmt.privileges.push_back(ast::Privilege::SHOW_TABLES);
                else if (privStr == "DESCRIBE") stmt.privileges.push_back(ast::Privilege::DESCRIBE);
                else if (privStr == "INSERT") stmt.privileges.push_back(ast::Privilege::INSERT);
                else if (privStr == "SELECT") stmt.privileges.push_back(ast::Privilege::SELECT);
                else if (privStr == "UPDATE") stmt.privileges.push_back(ast::Privilege::UPDATE);
                else if (privStr == "DELETE") stmt.privileges.push_back(ast::Privilege::DELETE);
                else {
                    r.errorMsg = "GRANT 不支持的权限: " + privStr;
                    return r;
                }
                ++pos;

                if (pos < tokens.size() && peek(tokens, pos).text == ",") {
                    ++pos;
                    if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
                        r.errorMsg = "GRANT 语法错误";
                        return r;
                    }
                    continue;
                }
                break;
            }
        }

        if (pos >= tokens.size() || !eqKw(peek(tokens, pos).text, "TO")) {
            r.errorMsg = "GRANT 语法错误（需: GRANT <权限|角色> TO user）";
            return r;
        }
        ++pos;

        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "GRANT 需要用户名";
            return r;
        }
        stmt.username = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(stmt)};
        return r;
    }

    // 解析 REVOKE 语句
    if (kw == "REVOKE") {
        ++pos;
        ast::RevokeStmt stmt;

        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "REVOKE 需要权限或角色名";
            return r;
        }

        std::string first = upper(peek(tokens, pos).text);

        if (first == "ADMIN" || first == "DBA" || first == "USER") {
            stmt.hasRole = true;
            if (first == "ADMIN") stmt.role = ast::RoleType::ADMIN;
            else if (first == "DBA") stmt.role = ast::RoleType::DBA;
            else stmt.role = ast::RoleType::USER;
            ++pos;
        } else {
            while (true) {
                std::string privStr = upper(peek(tokens, pos).text);
                if (privStr == "CREATE_USER") stmt.privileges.push_back(ast::Privilege::CREATE_USER);
                else if (privStr == "DROP_USER") stmt.privileges.push_back(ast::Privilege::DROP_USER);
                else if (privStr == "SHOW_USERS") stmt.privileges.push_back(ast::Privilege::SHOW_USERS);
                else if (privStr == "CREATE_ROLE") stmt.privileges.push_back(ast::Privilege::CREATE_ROLE);
                else if (privStr == "DROP_ROLE") stmt.privileges.push_back(ast::Privilege::DROP_ROLE);
                else if (privStr == "GRANT") stmt.privileges.push_back(ast::Privilege::GRANT);
                else if (privStr == "REVOKE") stmt.privileges.push_back(ast::Privilege::REVOKE);
                else if (privStr == "SHOW_GRANTS") stmt.privileges.push_back(ast::Privilege::SHOW_GRANTS);
                else if (privStr == "SHOW_AUDIT") stmt.privileges.push_back(ast::Privilege::SHOW_AUDIT);
                else if (privStr == "CREATE_DB") stmt.privileges.push_back(ast::Privilege::CREATE_DB);
                else if (privStr == "DROP_DB") stmt.privileges.push_back(ast::Privilege::DROP_DB);
                else if (privStr == "SHOW_DATABASES") stmt.privileges.push_back(ast::Privilege::SHOW_DATABASES);
                else if (privStr == "CONNECT") stmt.privileges.push_back(ast::Privilege::CONNECT);
                else if (privStr == "CREATE_TABLE") stmt.privileges.push_back(ast::Privilege::CREATE_TABLE);
                else if (privStr == "DROP_TABLE") stmt.privileges.push_back(ast::Privilege::DROP_TABLE);
                else if (privStr == "ALTER_TABLE") stmt.privileges.push_back(ast::Privilege::ALTER_TABLE);
                else if (privStr == "SHOW_TABLES") stmt.privileges.push_back(ast::Privilege::SHOW_TABLES);
                else if (privStr == "DESCRIBE") stmt.privileges.push_back(ast::Privilege::DESCRIBE);
                else if (privStr == "INSERT") stmt.privileges.push_back(ast::Privilege::INSERT);
                else if (privStr == "SELECT") stmt.privileges.push_back(ast::Privilege::SELECT);
                else if (privStr == "UPDATE") stmt.privileges.push_back(ast::Privilege::UPDATE);
                else if (privStr == "DELETE") stmt.privileges.push_back(ast::Privilege::DELETE);
                else {
                    r.errorMsg = "REVOKE 不支持的权限: " + privStr;
                    return r;
                }
                ++pos;

                if (pos < tokens.size() && peek(tokens, pos).text == ",") {
                    ++pos;
                    if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
                        r.errorMsg = "REVOKE 语法错误";
                        return r;
                    }
                    continue;
                }
                break;
            }
        }

        if (pos >= tokens.size() || !eqKw(peek(tokens, pos).text, "FROM")) {
            r.errorMsg = "REVOKE 语法错误（需: REVOKE <权限|角色> FROM user）";
            return r;
        }
        ++pos;

        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "REVOKE 需要用户名";
            return r;
        }
        stmt.username = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(stmt)};
        return r;
    }

    // 解析 SHOW GRANTS FOR 语句
    if (kw == "SHOW" && pos + 1 < tokens.size() && eqKw(peek(tokens, pos + 1).text, "GRANTS")) {
        pos += 2;
        if (pos >= tokens.size() || !eqKw(peek(tokens, pos).text, "FOR")) {
            r.errorMsg = "SHOW GRANTS 语法错误（需: SHOW GRANTS FOR user）";
            return r;
        }
        ++pos;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "SHOW GRANTS 需要用户名";
            return r;
        }
        ast::ShowGrantsStmt stmt;
        stmt.username = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(stmt)};
        return r;
    }

    // 解析 SHOW AUDIT LOG 语句
    if (kw == "SHOW" && pos + 1 < tokens.size() && eqKw(peek(tokens, pos + 1).text, "AUDIT") &&
        pos + 2 < tokens.size() && eqKw(peek(tokens, pos + 2).text, "LOG")) {
        pos += 3;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{ast::ShowAuditLogStmt{}};
        return r;
    }

    // 解析 DESC/DESCRIBE 语句
    if (kw == "DESC" || kw == "DESCRIBE") {
        ++pos;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "DESC 需要表名";
            return r;
        }
        ast::DescribeStmt d;
        d.table = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(d)};
        return r;
    }

    // 解析 DROP TABLE 语句
    if (kw == "DROP") {
        if (pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
            eqKw(peek(tokens, pos + 1).text, "DATABASE")) {
            return r;
        }
        if (!expectWord(tokens, pos, "DROP") || !expectWord(tokens, pos, "TABLE")) {
            r.errorMsg = "DROP TABLE 语法错误";
            return r;
        }
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "DROP TABLE 需要表名";
            return r;
        }
        ast::DropTableStmt d;
        d.table = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(d)};
        return r;
    }

    // 解析 ALTER TABLE 语句
    if (kw == "ALTER") {
        ast::AlterTableStmt a;
        if (!parseAlterTable(tokens, pos, a)) {
            r.errorMsg = "ALTER TABLE 语法错误（需: ALTER TABLE t ADD|DROP|MODIFY COLUMN name TYPE;）";
            return r;
        }
        r.stmt = ast::Stmt{std::move(a)};
        return r;
    }

    // 解析 CREATE TABLE 语句
    if (kw == "CREATE") {
        ast::CreateTableStmt c;
        if (!parseCreateTable(tokens, pos, c)) {
            r.errorMsg = "CREATE TABLE 语法错误（需: CREATE TABLE t (列名 INT|FLOAT|TEXT, ...);）";
            return r;
        }
        r.stmt = ast::Stmt{std::move(c)};
        return r;
    }

    // 解析 INSERT 语句
    if (kw == "INSERT") {
        ast::InsertStmt ins;
        if (!parseInsert(tokens, pos, ins)) {
            r.errorMsg = "INSERT 语法错误";
            return r;
        }
        r.stmt = ast::Stmt{std::move(ins)};
        return r;
    }

    // 解析 SELECT 语句
    if (kw == "SELECT") {
        ast::SelectStmt s;
        if (!parseSelect(tokens, pos, s)) {
            r.errorMsg = "SELECT 语法错误";
            return r;
        }
        r.stmt = ast::Stmt{std::move(s)};
        return r;
    }

    // 解析 DELETE 语句
    if (kw == "DELETE") {
        ast::DeleteStmt d;
        if (!parseDelete(tokens, pos, d)) {
            r.errorMsg = "DELETE 语法错误";
            return r;
        }
        r.stmt = ast::Stmt{std::move(d)};
        return r;
    }

    // 解析 UPDATE 语句
    if (kw == "UPDATE") {
        ast::UpdateStmt u;
        if (!parseUpdate(tokens, pos, u)) {
            r.errorMsg = "UPDATE 语法错误";
            return r;
        }
        r.stmt = ast::Stmt{std::move(u)};
        return r;
    }

    r.errorMsg = "不支持的关键字或语句";
    return r;
}
