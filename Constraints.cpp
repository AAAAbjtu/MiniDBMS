/**
 * CHECK 求值、外键目标校验等
 */

#include "Constraints.h"
#include "FileManager.h"
#include "Table.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

static std::string trim(std::string s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    while (s.size() > a && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s.substr(a);
}

static std::string toLower(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

static bool isIdentChar(unsigned char c) { return std::isalnum(c) || c == '_'; }

enum class Tk {
    End,
    Ident,
    Number,
    String,
    LParen,
    RParen,
    And,
    Or,
    Op, // text in tok
};

struct Tok {
    Tk k = Tk::End;
    std::string text;
};

struct Lex { // CHECK 子语言词法
    const char* p = nullptr;
    const char* n = nullptr;

    void skipWs() {
        while (p < n && std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }
    }

    Tok next() {
        skipWs();
        if (p >= n) {
            return {Tk::End, ""};
        }
        unsigned char c = static_cast<unsigned char>(*p);
        if (c == '(') {
            ++p;
            return {Tk::LParen, "("};
        }
        if (c == ')') {
            ++p;
            return {Tk::RParen, ")"};
        }
        if (c == '\'') {
            ++p;
            std::string s;
            while (p < n) {
                if (*p == '\'') {
                    if (p + 1 < n && p[1] == '\'') {
                        s += '\'';
                        p += 2;
                        continue;
                    }
                    ++p;
                    break;
                }
                s += *p++;
            }
            return {Tk::String, s};
        }
        if (c == '<') {
            if (p + 1 < n && p[1] == '=') {
                p += 2;
                return {Tk::Op, "<="};
            }
            if (p + 1 < n && p[1] == '>') {
                p += 2;
                return {Tk::Op, "<>"};
            }
            ++p;
            return {Tk::Op, "<"};
        }
        if (c == '>') {
            if (p + 1 < n && p[1] == '=') {
                p += 2;
                return {Tk::Op, ">="};
            }
            ++p;
            return {Tk::Op, ">"};
        }
        if (c == '!') {
            if (p + 1 < n && p[1] == '=') {
                p += 2;
                return {Tk::Op, "!="};
            }
            return {Tk::End, ""};
        }
        if (c == '=') {
            ++p;
            return {Tk::Op, "="};
        }
        if (std::isdigit(c) || (c == '-' && p + 1 < n && std::isdigit(static_cast<unsigned char>(p[1])))) {
            std::string s;
            if (*p == '-') {
                s += *p++;
            }
            while (p < n && std::isdigit(static_cast<unsigned char>(*p))) {
                s += *p++;
            }
            if (p < n && *p == '.') {
                s += *p++;
                while (p < n && std::isdigit(static_cast<unsigned char>(*p))) {
                    s += *p++;
                }
            }
            return {Tk::Number, s};
        }
        if (isIdentChar(c)) {
            const char* stp = p;
            while (p < n && isIdentChar(static_cast<unsigned char>(*p))) {
                ++p;
            }
            std::string id(stp, p);
            std::string lo = toLower(id);
            if (lo == "and") {
                return {Tk::And, id};
            }
            if (lo == "or") {
                return {Tk::Or, id};
            }
            return {Tk::Ident, id};
        }
        return {Tk::End, ""};
    }
};

struct ParserCk { // CHECK 递归下降解析
    Lex L;
    Tok cur{Tk::End, ""};

    explicit ParserCk(const std::string& s) {
        L.p = s.data();
        L.n = s.data() + s.size();
        bump();
    }

    void bump() { cur = L.next(); }

    bool eat(Tk k) {
        if (cur.k == k) {
            bump();
            return true;
        }
        return false;
    }

    bool atEnd() const { return cur.k == Tk::End; }

    bool parseTop(const std::vector<ColumnDef>& schema, const Record& row, bool& ok, std::string& err) {
        bool v = parseExpr(schema, row, ok, err);
        if (!ok) {
            return false;
        }
        if (!atEnd()) {
            ok = false;
            err = "CHECK：表达式有多余内容";
            return false;
        }
        return v;
    }

    bool parseOr(const std::vector<ColumnDef>& schema, const Record& row, bool& ok, std::string& err) {
        bool v = parseAnd(schema, row, ok, err);
        if (!ok) {
            return false;
        }
        while (cur.k == Tk::Or) {
            bump();
            bool rhs = parseAnd(schema, row, ok, err);
            if (!ok) {
                return false;
            }
            v = v || rhs;
        }
        return v;
    }

    bool parseAnd(const std::vector<ColumnDef>& schema, const Record& row, bool& ok, std::string& err) {
        bool v = parseAtom(schema, row, ok, err);
        if (!ok) {
            return false;
        }
        while (cur.k == Tk::And) {
            bump();
            bool rhs = parseAtom(schema, row, ok, err);
            if (!ok) {
                return false;
            }
            v = v && rhs;
        }
        return v;
    }

    bool parseAtom(const std::vector<ColumnDef>& schema, const Record& row, bool& ok, std::string& err) {
        if (eat(Tk::LParen)) {
            bool v = parseExpr(schema, row, ok, err);
            if (!ok) {
                return false;
            }
            if (!eat(Tk::RParen)) {
                ok = false;
                err = "CHECK：缺少右括号";
                return false;
            }
            return v;
        }
        return parseComparison(schema, row, ok, err);
    }

    bool parseComparison(const std::vector<ColumnDef>& schema, const Record& row, bool& ok, std::string& err);

    bool parseExpr(const std::vector<ColumnDef>& schema, const Record& row, bool& ok, std::string& err) {
        return parseOr(schema, row, ok, err);
    }
};

static int columnIndexByName(const std::vector<ColumnDef>& schema, const std::string& name) {
    for (size_t i = 0; i < schema.size(); ++i) {
        if (schema[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static const Cell* cellAt(const Record& row, int idx) {
    if (idx < 0 || idx >= static_cast<int>(row.cells.size())) {
        return nullptr;
    }
    return &row.cells[static_cast<size_t>(idx)];
}

static Cell coerceForSqlType(const Cell& c, SqlType t) {
    if (t == SqlType::Int) {
        if (const auto* p = std::get_if<std::int64_t>(&c)) {
            return *p;
        }
        if (const auto* p = std::get_if<double>(&c)) {
            return Cell{static_cast<std::int64_t>(*p)};
        }
        return c;
    }
    if (t == SqlType::Float) {
        if (const auto* p = std::get_if<std::int64_t>(&c)) {
            return Cell{static_cast<double>(*p)};
        }
        if (const auto* p = std::get_if<double>(&c)) {
            return Cell{*p};
        }
        return c;
    }
    return c;
}

static int compareTyped(const Cell& a, const Cell& b, SqlType t) {
    Cell ca = coerceForSqlType(a, t);
    Cell cb = coerceForSqlType(b, t);
    if (t == SqlType::Int) {
        const auto* pa = std::get_if<std::int64_t>(&ca);
        const auto* pb = std::get_if<std::int64_t>(&cb);
        if (!pa || !pb) {
            return 0;
        }
        if (*pa < *pb) {
            return -1;
        }
        if (*pa > *pb) {
            return 1;
        }
        return 0;
    }
    if (t == SqlType::Float) {
        double da = 0, db = 0;
        if (const auto* p = std::get_if<std::int64_t>(&ca)) {
            da = static_cast<double>(*p);
        } else if (const auto* p = std::get_if<double>(&ca)) {
            da = *p;
        } else {
            return 0;
        }
        if (const auto* p = std::get_if<std::int64_t>(&cb)) {
            db = static_cast<double>(*p);
        } else if (const auto* p = std::get_if<double>(&cb)) {
            db = *p;
        } else {
            return 0;
        }
        if (da < db) {
            return -1;
        }
        if (da > db) {
            return 1;
        }
        return 0;
    }
    const auto* sa = std::get_if<std::string>(&ca);
    const auto* sb = std::get_if<std::string>(&cb);
    if (!sa || !sb) {
        return 0;
    }
    if (*sa < *sb) {
        return -1;
    }
    if (*sa > *sb) {
        return 1;
    }
    return 0;
}

static bool applyCmp(int ord, const std::string& op) {
    if (op == "=" || op == "==") {
        return ord == 0;
    }
    if (op == "<>" || op == "!=") {
        return ord != 0;
    }
    if (op == "<") {
        return ord < 0;
    }
    if (op == ">") {
        return ord > 0;
    }
    if (op == "<=") {
        return ord <= 0;
    }
    if (op == ">=") {
        return ord >= 0;
    }
    return false;
}

bool ParserCk::parseComparison(const std::vector<ColumnDef>& schema, const Record& row, bool& ok, std::string& err) {
    // forms: col op literal | literal op col
    if (cur.k != Tk::Ident && cur.k != Tk::Number && cur.k != Tk::String) {
        ok = false;
        err = "CHECK：期望比较表达式";
        return false;
    }
    std::string leftTok;
    Tk leftKind = cur.k;
    leftTok = cur.text;
    bump();
    if (cur.k != Tk::Op) {
        ok = false;
        err = "CHECK：期望比较运算符";
        return false;
    }
    std::string op = cur.text;
    bump();
    if (cur.k != Tk::Ident && cur.k != Tk::Number && cur.k != Tk::String) {
        ok = false;
        err = "CHECK：期望右操作数";
        return false;
    }
    std::string rightTok;
    Tk rightKind = cur.k;
    rightTok = cur.text;
    bump();

    int colIdx = -1;
    std::string litRaw;
    SqlType colType = SqlType::Text;
    bool colOnLeft = false;

    if (leftKind == Tk::Ident && columnIndexByName(schema, leftTok) >= 0) {
        colIdx = columnIndexByName(schema, leftTok);
        colType = schema[static_cast<size_t>(colIdx)].type;
        litRaw = rightTok;
        colOnLeft = true;
    } else if (rightKind == Tk::Ident && columnIndexByName(schema, rightTok) >= 0) {
        colIdx = columnIndexByName(schema, rightTok);
        colType = schema[static_cast<size_t>(colIdx)].type;
        litRaw = leftTok;
        colOnLeft = false;
    } else {
        ok = false;
        err = "CHECK：比较式须包含本表列名";
        return false;
    }

    const Cell* cv = cellAt(row, colIdx);
    if (!cv) {
        ok = false;
        err = "CHECK：行数据不完整";
        return false;
    }

    std::string lit = litRaw;
    auto rhs = parseLiteralToCell(lit, colType);
    if (!rhs.has_value()) {
        ok = false;
        err = "CHECK：字面量与列类型不兼容";
        return false;
    }

    int ord = compareTyped(*cv, *rhs, colType);
    std::string useOp = op;
    if (!colOnLeft) {
        if (op == "<") {
            useOp = ">";
        } else if (op == ">") {
            useOp = "<";
        } else if (op == "<=") {
            useOp = ">=";
        } else if (op == ">=") {
            useOp = "<=";
        }
    }
    return applyCmp(ord, useOp);
}

bool evaluateAllCheckConstraints(const std::vector<ColumnDef>& schema, const Record& row, std::string& errMsg) {
    for (const auto& col : schema) {
        if (col.checkExpr.empty()) {
            continue;
        }
        ParserCk p(col.checkExpr);
        bool ok = true;
        bool v = p.parseTop(schema, row, ok, errMsg);
        if (!ok) {
            return false;
        }
        if (!v) {
            errMsg = "违反 CHECK 约束（列 " + col.name + "）";
            return false;
        }
    }
    return true;
}

bool parentHasMatchingKeyValue(const Table& parent, const std::string& refColumnName, const Cell& value) {
    int idx = parent.columnIndex(refColumnName);
    if (idx < 0) {
        return false;
    }
    SqlType t = parent.columnType(idx);
    for (const auto& r : parent.getRecords()) {
        if (idx >= static_cast<int>(r.cells.size())) {
            continue;
        }
        if (cellEqualsTyped(r.cells[static_cast<size_t>(idx)], value, t)) {
            return true;
        }
    }
    return false;
}

bool parentColumnIsFkTarget(const Table& parent, const std::string& refColumnName) {
    int idx = parent.columnIndex(refColumnName);
    if (idx < 0) {
        return false;
    }
    const auto& sch = parent.getSchema();
    const ColumnDef& cd = sch[static_cast<size_t>(idx)];
    return cd.primaryKey || cd.unique;
}

bool validateForeignKeyDefinition(const std::string& dbName, const std::string& creatingTable,
                                  const std::vector<ColumnDef>& creatingColumns, const std::string& fkRefTable,
                                  const std::string& fkRefCol, std::string& errMsg) {
    if (fkRefTable.empty() || fkRefCol.empty()) {
        errMsg = "外键引用表名或列名为空";
        return false;
    }
    if (fkRefTable == creatingTable) {
        int idx = -1;
        for (size_t i = 0; i < creatingColumns.size(); ++i) {
            if (creatingColumns[i].name == fkRefCol) {
                idx = static_cast<int>(i);
                break;
            }
        }
        if (idx < 0) {
            errMsg = "自引用外键：找不到引用列 " + fkRefCol;
            return false;
        }
        const ColumnDef& rc = creatingColumns[static_cast<size_t>(idx)];
        if (!rc.primaryKey && !rc.unique) {
            errMsg = "自引用外键：引用列须为 PRIMARY KEY 或 UNIQUE";
            return false;
        }
        return true;
    }
    if (!FileManager::tableExists(dbName, fkRefTable)) {
        errMsg = "外键：被引用表不存在: " + fkRefTable;
        return false;
    }
    Table parent = FileManager::load(dbName, fkRefTable);
    if (parent.columnIndex(fkRefCol) < 0) {
        errMsg = "外键：被引用列不存在: " + fkRefTable + "." + fkRefCol;
        return false;
    }
    if (!parentColumnIsFkTarget(parent, fkRefCol)) {
        errMsg = "外键：被引用列须为 PRIMARY KEY 或 UNIQUE: " + fkRefTable + "." + fkRefCol;
        return false;
    }
    return true;
}
