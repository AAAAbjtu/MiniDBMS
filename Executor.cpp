/**
 * Executor 模块实现
 * 负责执行 SQL 语句
 */

#include "Executor.h"
#include "Ast.h"
#include "Constraints.h"
#include "QueryEval.h"
#include "Schema.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {

/**
 * 构建插入记录
 * 处理 INSERT 语句的值与列的匹配
 */
static bool buildInsertRecord(Table& t, const ast::InsertStmt& ins, Record& out, std::string& err) {
    const auto& sch = t.getSchema();
    if (sch.empty()) {
        err = "表无列定义";
        return false;
    }
    out.cells.resize(sch.size());
    for (size_t i = 0; i < sch.size(); ++i) {
        switch (sch[i].type) {
        case SqlType::Int:
            out.cells[i] = Cell{static_cast<std::int64_t>(0)};
            break;
        case SqlType::Float:
            out.cells[i] = Cell{0.0};
            break;
        case SqlType::Text:
            out.cells[i] = Cell{std::string{}};
            break;
        }
    }
    if (!ins.columns.empty()) {
        if (ins.columns.size() != ins.values.size()) {
            err = "指定列与值数量不一致";
            return false;
        }
        for (size_t i = 0; i < ins.columns.size(); ++i) {
            int idx = t.columnIndex(ins.columns[i]);
            if (idx < 0) {
                err = "未知列: " + ins.columns[i];
                return false;
            }
            auto c = parseLiteralToCell(ins.values[i], sch[static_cast<size_t>(idx)].type);
            if (!c.has_value()) {
                err = "值与列类型不匹配: " + ins.columns[i];
                return false;
            }
            out.cells[static_cast<size_t>(idx)] = *c;
        }
        return true;
    }
    if (ins.values.size() != sch.size()) {
        err = "列数与值数量不一致";
        return false;
    }
    for (size_t i = 0; i < sch.size(); ++i) {
        auto c = parseLiteralToCell(ins.values[i], sch[i].type);
        if (!c.has_value()) {
            err = "值与列类型不匹配";
            return false;
        }
        out.cells[i] = *c;
    }
    return true;
}

static bool insertColumnListed(const ast::InsertStmt& ins, const std::string& colName) {
    for (const auto& c : ins.columns) {
        if (c == colName) {
            return true;
        }
    }
    return false;
}

static bool validateChildForeignKeys(Executor& ex, const std::string& db, const Table& child, const Record& row,
                                     std::string& err) {
    const auto& sch = child.getSchema();
    for (size_t i = 0; i < sch.size(); ++i) {
        if (sch[i].fkRefTable.empty()) {
            continue;
        }
        if (i >= row.cells.size()) {
            continue;
        }
        Table& parent = ex.loadTable(sch[i].fkRefTable);
        if (!parentHasMatchingKeyValue(parent, sch[i].fkRefCol, row.cells[i])) {
            err = "违反外键：列 " + sch[i].name + " 的值在被引用表中不存在";
            return false;
        }
    }
    return true;
}

static bool canDeleteParentRows(Executor& ex, const std::string& db, Table& parent, const ast::DeleteStmt& s,
                                std::string& err) {
    std::vector<IncomingForeignKey> incoming = FileManager::listIncomingForeignKeys(db, parent.getName());
    if (incoming.empty()) {
        return true;
    }
    std::vector<size_t> rows = parent.matchingRowIndices(s.whereColumn, s.whereValue);
    for (size_t ri : rows) {
        const Record& pr = parent.getRecords()[ri];
        for (const auto& fk : incoming) {
            int pidx = parent.columnIndex(fk.parentReferencedColumn);
            if (pidx < 0 || pidx >= static_cast<int>(pr.cells.size())) {
                continue;
            }
            const Cell& v = pr.cells[static_cast<size_t>(pidx)];
            Table& child = ex.loadTable(fk.childTable);
            int cidx = child.columnIndex(fk.childFkColumn);
            if (cidx < 0) {
                continue;
            }
            SqlType ct = child.columnType(cidx);
            for (const auto& cr : child.getRecords()) {
                if (cidx < static_cast<int>(cr.cells.size()) &&
                    cellEqualsTyped(cr.cells[static_cast<size_t>(cidx)], v, ct)) {
                    err = "存在子表外键引用，拒绝删除";
                    return false;
                }
            }
        }
    }
    return true;
}

/**
 * NOT NULL / CHECK / 外键 / 唯一性（插入）
 */
static bool validateInsertIntegrity(Executor& ex, const std::string& db, const Table& t, const ast::InsertStmt& ins,
                                    const Record& row, std::string& err) {
    const auto& sch = t.getSchema();
    if (!ins.columns.empty()) {
        for (size_t i = 0; i < sch.size(); ++i) {
            if (sch[i].notNull && !insertColumnListed(ins, sch[i].name)) {
                err = "违反 NOT NULL：未为列 " + sch[i].name + " 提供值";
                return false;
            }
        }
    }
    for (size_t i = 0; i < sch.size(); ++i) {
        if (!sch[i].notNull && !sch[i].primaryKey) {
            continue;
        }
        if (i >= row.cells.size()) {
            err = "行数据与表结构不一致";
            return false;
        }
        if (cellViolatesNotNull(row.cells[i], sch[i].type)) {
            err = sch[i].primaryKey ? "主键列不能为空字符串" : ("违反 NOT NULL：列 " + sch[i].name);
            return false;
        }
    }
    if (!evaluateAllCheckConstraints(sch, row, err)) {
        return false;
    }
    if (!validateChildForeignKeys(ex, db, t, row, err)) {
        return false;
    }
    constexpr size_t kNoExclude = static_cast<size_t>(-1);
    if (t.rowConflictsUniqueKeys(row, kNoExclude, err)) {
        return false;
    }
    return true;
}

static bool validateUpdateIntegrity(Executor& ex, const std::string& db, Table& t, const ast::UpdateStmt& s,
                                    std::string& err) {
    int setIdx = t.columnIndex(s.setColumn);
    if (setIdx < 0) {
        err = "列不存在: " + s.setColumn;
        return false;
    }
    SqlType st = t.columnType(setIdx);
    auto newCell = parseLiteralToCell(s.setValue, st);
    if (!newCell.has_value()) {
        err = "SET 值与列类型不匹配";
        return false;
    }
    const auto& sch = t.getSchema();
    const ColumnDef& cd = sch[static_cast<size_t>(setIdx)];
    if ((cd.notNull || cd.primaryKey) && cellViolatesNotNull(*newCell, st)) {
        err = cd.primaryKey ? "主键列不能为空字符串" : "违反 NOT NULL";
        return false;
    }
    std::vector<size_t> matched = t.matchingRowIndices(s.whereColumn, s.whereValue);

    if (cd.primaryKey || cd.unique) {
        if (matched.size() > 1) {
            err = "不能将多行更新为相同的唯一键值";
            return false;
        }
        if (matched.size() == 1) {
            const auto& recs = t.getRecords();
            Record temp = recs[matched[0]];
            if (setIdx >= static_cast<int>(temp.cells.size())) {
                err = "行数据与表结构不一致";
                return false;
            }
            temp.cells[static_cast<size_t>(setIdx)] = *newCell;
            if (t.rowConflictsUniqueKeys(temp, matched[0], err)) {
                return false;
            }
        }
    }

    std::vector<IncomingForeignKey> incoming = FileManager::listIncomingForeignKeys(db, t.getName());
    for (const auto& fk : incoming) {
        if (fk.parentReferencedColumn != s.setColumn) {
            continue;
        }
        for (size_t ri : matched) {
            const Record& oldr = t.getRecords()[ri];
            if (setIdx >= static_cast<int>(oldr.cells.size())) {
                continue;
            }
            const Cell& oldv = oldr.cells[static_cast<size_t>(setIdx)];
            if (cellEqualsTyped(oldv, *newCell, st)) {
                continue;
            }
            Table& child = ex.loadTable(fk.childTable);
            int cidx = child.columnIndex(fk.childFkColumn);
            if (cidx < 0) {
                continue;
            }
            SqlType ct = child.columnType(cidx);
            for (const auto& cr : child.getRecords()) {
                if (cidx < static_cast<int>(cr.cells.size()) &&
                    cellEqualsTyped(cr.cells[static_cast<size_t>(cidx)], oldv, ct)) {
                    err = "存在子表外键引用该列旧值，拒绝更新";
                    return false;
                }
            }
        }
    }

    if (!cd.fkRefTable.empty()) {
        Table& parent = ex.loadTable(cd.fkRefTable);
        if (!parentHasMatchingKeyValue(parent, cd.fkRefCol, *newCell)) {
            err = "违反外键：SET 值在被引用表中不存在";
            return false;
        }
    }

    for (size_t ri : matched) {
        Record temp = t.getRecords()[ri];
        if (setIdx >= static_cast<int>(temp.cells.size())) {
            err = "行数据与表结构不一致";
            return false;
        }
        temp.cells[static_cast<size_t>(setIdx)] = *newCell;
        if (!evaluateAllCheckConstraints(sch, temp, err)) {
            return false;
        }
    }
    return true;
}

static bool logicalNameMatch(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

static bool validateFromJoinModes(const std::vector<ast::FromTableSpec>& from, std::string& err) {
    if (from.empty()) {
        err = "FROM 不能为空";
        return false;
    }
    if (from.size() > 1) {
        bool joinMode = static_cast<bool>(from[1].joinOn);
        for (size_t i = 1; i < from.size(); ++i) {
            if (static_cast<bool>(from[i].joinOn) != joinMode) {
                err = "FROM 子句混合了逗号连接与 JOIN";
                return false;
            }
        }
        if (joinMode) {
            for (size_t i = 1; i < from.size(); ++i) {
                if (!from[i].joinOn) {
                    err = "JOIN 缺少 ON";
                    return false;
                }
            }
        }
    }
    return true;
}

static void printResultGrid(const std::vector<std::string>& headers,
                            const std::vector<std::vector<std::optional<Cell>>>& rows) {
    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) {
            std::cout << " | ";
        }
        std::cout << headers[i];
    }
    std::cout << "\n";
    for (const auto& r : rows) {
        for (size_t i = 0; i < r.size(); ++i) {
            if (i > 0) {
                std::cout << " | ";
            }
            std::cout << cellToString(r[i]);
        }
        std::cout << "\n";
    }
    std::cout << "(" << rows.size() << " rows)\n";
}

static QueryRowContext buildPrefixCtx(Executor& ex, const std::vector<ast::FromTableSpec>& from,
                                      const std::vector<size_t>& idxs, size_t lastInclusive) {
    QueryRowContext ctx;
    for (size_t i = 0; i <= lastInclusive && i < from.size(); ++i) {
        Table& tab = ex.loadTable(from[i].table);
        ctx.tables.push_back(&tab);
        ctx.rowIndices.push_back(idxs[i]);
        ctx.logicalNames.push_back(from[i].alias.empty() ? from[i].table : from[i].alias);
    }
    return ctx;
}

static QueryRowContext buildFullCtx(Executor& ex, const std::vector<ast::FromTableSpec>& from,
                                    const std::vector<size_t>& idxs) {
    return buildPrefixCtx(ex, from, idxs, from.empty() ? 0 : from.size() - 1);
}

static QueryRowContext mergeContexts(const QueryRowContext* outer, QueryRowContext inner) {
    if (!outer) {
        return inner;
    }
    QueryRowContext m = *outer;
    m.tables.insert(m.tables.end(), inner.tables.begin(), inner.tables.end());
    m.rowIndices.insert(m.rowIndices.end(), inner.rowIndices.begin(), inner.rowIndices.end());
    m.logicalNames.insert(m.logicalNames.end(), inner.logicalNames.begin(), inner.logicalNames.end());
    return m;
}

static void dfsJoinContexts(Executor& ex, const std::vector<ast::FromTableSpec>& from, const ast::QueryExpr* where,
                            const QueryRowContext* outer, size_t depth, std::vector<size_t>& idxs,
                            std::vector<QueryRowContext>& out, std::string& err, bool& ok, SubqueryRunner* sub) {
    if (!ok) {
        return;
    }
    if (depth == from.size()) {
        QueryRowContext inner = buildFullCtx(ex, from, idxs);
        QueryRowContext merged = mergeContexts(outer, std::move(inner));
        if (where) {
            bool wb = false;
            if (!evalQueryExprBool(*where, merged, wb, err, sub)) {
                ok = false;
                return;
            }
            if (!wb) {
                return;
            }
        }
        out.push_back(std::move(merged));
        return;
    }
    Table& tab = ex.loadTable(from[depth].table);
    const auto& recs = tab.getRecords();
    if (depth > 0 && from[depth].joinOn) {
        std::vector<size_t> matched;
        for (size_t ri = 0; ri < recs.size(); ++ri) {
            idxs[depth] = ri;
            QueryRowContext pctx = buildPrefixCtx(ex, from, idxs, depth);
            bool jb = false;
            if (!evalQueryExprBool(*from[depth].joinOn, pctx, jb, err, sub)) {
                ok = false;
                return;
            }
            if (jb) {
                matched.push_back(ri);
            }
        }
        if (matched.empty() && from[depth].joinKind == ast::JoinKind::Left) {
            idxs[depth] = kNullRowIndex;
            dfsJoinContexts(ex, from, where, outer, depth + 1, idxs, out, err, ok, sub);
        } else {
            for (size_t ri : matched) {
                idxs[depth] = ri;
                dfsJoinContexts(ex, from, where, outer, depth + 1, idxs, out, err, ok, sub);
            }
        }
        return;
    }
    for (size_t ri = 0; ri < recs.size(); ++ri) {
        idxs[depth] = ri;
        dfsJoinContexts(ex, from, where, outer, depth + 1, idxs, out, err, ok, sub);
    }
}

static bool selectHasStar(const ast::SelectStmt& s) {
    for (const auto& it : s.selectList) {
        if (it.kind == ast::SelectItemKind::Star) {
            return true;
        }
    }
    return false;
}

static bool selectListHasAggregate(const ast::SelectStmt& s) {
    for (const auto& it : s.selectList) {
        if (it.kind == ast::SelectItemKind::Expr && it.expr && exprContainsAggregate(*it.expr)) {
            return true;
        }
    }
    return false;
}

static bool projectDataRow(Executor& ex, const ast::SelectStmt& s, const QueryRowContext& ctx,
                           const std::vector<QueryRowContext>* groupBucket, std::vector<std::optional<Cell>>& row,
                           std::string& err, SubqueryRunner* sub) {
    row.clear();
    for (const auto& it : s.selectList) {
        if (it.kind == ast::SelectItemKind::Star) {
            if (it.starTable.has_value()) {
                size_t ti = static_cast<size_t>(-1);
                for (size_t i = 0; i < s.from.size(); ++i) {
                    std::string log = s.from[i].alias.empty() ? s.from[i].table : s.from[i].alias;
                    if (logicalNameMatch(log, *it.starTable)) {
                        ti = i;
                        break;
                    }
                }
                if (ti == static_cast<size_t>(-1)) {
                    err = "未知表或别名: " + *it.starTable;
                    return false;
                }
                Table& t = ex.loadTable(s.from[ti].table);
                const auto& recs = t.getRecords();
                size_t ri = ctx.rowIndices[ti];
                if (ri == kNullRowIndex) {
                    for (size_t k = 0; k < t.getSchema().size(); ++k) {
                        row.push_back(std::nullopt);
                    }
                } else {
                    if (ri >= recs.size()) {
                        err = "行越界";
                        return false;
                    }
                    for (const auto& c : recs[ri].cells) {
                        row.push_back(c);
                    }
                }
            } else {
                for (size_t ti = 0; ti < s.from.size(); ++ti) {
                    Table& t = ex.loadTable(s.from[ti].table);
                    const auto& recs = t.getRecords();
                    size_t ri = ctx.rowIndices[ti];
                    if (ri == kNullRowIndex) {
                        for (size_t k = 0; k < t.getSchema().size(); ++k) {
                            row.push_back(std::nullopt);
                        }
                    } else {
                        if (ri >= recs.size()) {
                            err = "行越界";
                            return false;
                        }
                        for (const auto& c : recs[ri].cells) {
                            row.push_back(c);
                        }
                    }
                }
            }
        } else {
            if (!it.expr) {
                err = "缺少表达式";
                return false;
            }
            std::optional<Cell> c;
            SqlType st{};
            if (groupBucket) {
                if (exprContainsAggregate(*it.expr)) {
                    if (!evalAggregateExpr(*it.expr, *groupBucket, c, st, err)) {
                        return false;
                    }
                } else {
                    if (groupBucket->empty()) {
                        err = "无匹配行时 SELECT 列表不能包含非聚合表达式";
                        return false;
                    }
                    if (!evalQueryExprScalar(*it.expr, (*groupBucket)[0], c, st, err, sub)) {
                        return false;
                    }
                }
            } else {
                if (!evalQueryExprScalar(*it.expr, ctx, c, st, err, sub)) {
                    return false;
                }
            }
            row.push_back(std::move(c));
        }
    }
    return true;
}

static bool havingPredicateCompare(const std::string& op, const std::optional<Cell>& a, const std::optional<Cell>& b,
                                   bool& out) {
    if (!a.has_value() || !b.has_value()) {
        out = false;
        return true;
    }
    if (op == "=" || op == "==") {
        auto c = cellCompareLoose(*a, *b);
        if (!c.has_value()) {
            return false;
        }
        out = (*c == 0);
        return true;
    }
    if (op == "<>" || op == "!=") {
        auto c = cellCompareLoose(*a, *b);
        if (!c.has_value()) {
            return false;
        }
        out = (*c != 0);
        return true;
    }
    auto c = cellCompareLoose(*a, *b);
    if (!c.has_value()) {
        return false;
    }
    int v = *c;
    if (op == "<") {
        out = (v < 0);
        return true;
    }
    if (op == "<=") {
        out = (v <= 0);
        return true;
    }
    if (op == ">") {
        out = (v > 0);
        return true;
    }
    if (op == ">=") {
        out = (v >= 0);
        return true;
    }
    return false;
}

static std::string groupKeyForCtx(const ast::SelectStmt& s, const QueryRowContext& ctx, std::string& err) {
    std::string key;
    for (const auto& ge : s.groupBy) {
        std::optional<Cell> c;
        SqlType st{};
        if (!evalQueryExprScalar(*ge, ctx, c, st, err, nullptr)) {
            return "";
        }
        key += "|" + cellToString(c);
    }
    return key;
}

static bool evalHavingGroup(const ast::QueryExpr& e, const std::vector<QueryRowContext>& bucket, bool& out,
                            std::string& err, SubqueryRunner* sub) {
    return std::visit(
        [&](const auto& alt) -> bool {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, ast::QUnary>) {
                if (alt.op != ast::QUnary::Op::Not) {
                    err = "HAVING 不支持该运算符";
                    return false;
                }
                bool inner = false;
                if (!evalHavingGroup(*alt.child, bucket, inner, err, sub)) {
                    return false;
                }
                out = !inner;
                return true;
            } else if constexpr (std::is_same_v<T, ast::QBinary>) {
                if (alt.op == "AND") {
                    bool a = false;
                    if (!evalHavingGroup(*alt.left, bucket, a, err, sub)) {
                        return false;
                    }
                    if (!a) {
                        out = false;
                        return true;
                    }
                    return evalHavingGroup(*alt.right, bucket, out, err, sub);
                }
                if (alt.op == "OR") {
                    bool a = false;
                    if (!evalHavingGroup(*alt.left, bucket, a, err, sub)) {
                        return false;
                    }
                    if (a) {
                        out = true;
                        return true;
                    }
                    return evalHavingGroup(*alt.right, bucket, out, err, sub);
                }
                std::optional<Cell> l;
                std::optional<Cell> r;
                SqlType lt{};
                SqlType rt{};
                QueryRowContext emptyCtx;
                const QueryRowContext& scalarCtx = bucket.empty() ? emptyCtx : bucket[0];
                if (exprContainsAggregate(*alt.left)) {
                    if (!evalAggregateExpr(*alt.left, bucket, l, lt, err)) {
                        return false;
                    }
                } else {
                    if (!evalQueryExprScalar(*alt.left, scalarCtx, l, lt, err, sub)) {
                        return false;
                    }
                }
                if (exprContainsAggregate(*alt.right)) {
                    if (!evalAggregateExpr(*alt.right, bucket, r, rt, err)) {
                        return false;
                    }
                } else {
                    if (!evalQueryExprScalar(*alt.right, scalarCtx, r, rt, err, sub)) {
                        return false;
                    }
                }
                bool res = false;
                if (!havingPredicateCompare(alt.op, l, r, res)) {
                    err = "HAVING 比较失败";
                    return false;
                }
                out = res;
                return true;
            }
            err = "HAVING 仅支持 AND/OR/NOT 与比较表达式";
            return false;
        },
        e.node);
}

static int compareOptionalCellsLoose(const std::optional<Cell>& a, const std::optional<Cell>& b) {
    if (!a.has_value() && !b.has_value()) {
        return 0;
    }
    if (!a.has_value()) {
        return -1;
    }
    if (!b.has_value()) {
        return 1;
    }
    auto c = cellCompareLoose(*a, *b);
    if (!c.has_value()) {
        return 0;
    }
    return *c;
}

struct ExecSubqueryRunner : SubqueryRunner {
    Executor& ex;
    std::string db;

    ExecSubqueryRunner(Executor& e, std::string d) : ex(e), db(std::move(d)) {}

    bool evalExists(const std::string& selectSql, const QueryRowContext* outer, bool notExists, bool& out,
                    std::string& err) override {
        Parser p;
        ParseResult pr = p.parse(selectSql);
        if (!pr.errorMsg.empty() || !pr.stmt.has_value()) {
            err = "子查询解析失败: " + pr.errorMsg;
            return false;
        }
        auto* compound = std::get_if<ast::CompoundSelectStmt>(&*pr.stmt);
        if (!compound || compound->arms.size() != 1) {
            err = "EXISTS 子查询须为单条 SELECT（不支持 UNION 等）";
            return false;
        }
        const ast::SelectStmt& sub = compound->arms[0];
        std::string verr;
        if (!validateFromJoinModes(sub.from, verr)) {
            err = verr;
            return false;
        }
        for (const auto& f : sub.from) {
            if (!FileManager::tableExists(db, f.table)) {
                err = "子查询表不存在: " + f.table;
                return false;
            }
        }
        std::vector<QueryRowContext> ctxs;
        std::vector<size_t> idxs(sub.from.size(), 0);
        bool ok = true;
        dfsJoinContexts(ex, sub.from, sub.where.get(), outer, 0, idxs, ctxs, err, ok, this);
        if (!ok) {
            return false;
        }
        bool any = !ctxs.empty();
        out = notExists ? !any : any;
        return true;
    }

    bool evalIn(const std::string& selectSql, const QueryRowContext* outer, const Cell& left, bool notIn, bool& out,
                std::string& err) override {
        Parser p;
        ParseResult pr = p.parse(selectSql);
        if (!pr.errorMsg.empty() || !pr.stmt.has_value()) {
            err = "子查询解析失败: " + pr.errorMsg;
            return false;
        }
        auto* compound = std::get_if<ast::CompoundSelectStmt>(&*pr.stmt);
        if (!compound || compound->arms.size() != 1) {
            err = "IN 子查询须为单条 SELECT（不支持 UNION 等）";
            return false;
        }
        const ast::SelectStmt& sub = compound->arms[0];
        if (selectHasStar(sub) || sub.selectList.size() != 1 || sub.selectList[0].kind != ast::SelectItemKind::Expr) {
            err = "IN 子查询必须恰好返回一列（不使用 *）";
            return false;
        }
        std::string verr;
        if (!validateFromJoinModes(sub.from, verr)) {
            err = verr;
            return false;
        }
        for (const auto& f : sub.from) {
            if (!FileManager::tableExists(db, f.table)) {
                err = "子查询表不存在: " + f.table;
                return false;
            }
        }
        std::vector<QueryRowContext> ctxs;
        std::vector<size_t> idxs(sub.from.size(), 0);
        bool ok = true;
        dfsJoinContexts(ex, sub.from, sub.where.get(), outer, 0, idxs, ctxs, err, ok, this);
        if (!ok) {
            return false;
        }
        bool found = false;
        for (const auto& cx : ctxs) {
            std::optional<Cell> v;
            SqlType st{};
            if (!evalQueryExprScalar(*sub.selectList[0].expr, cx, v, st, err, this)) {
                return false;
            }
            if (!v.has_value()) {
                continue;
            }
            auto cmp = cellCompareLoose(left, *v);
            if (!cmp.has_value()) {
                err = "IN 子查询比较类型不兼容";
                return false;
            }
            if (*cmp == 0) {
                found = true;
                break;
            }
        }
        out = notIn ? !found : found;
        return true;
    }
};

static bool buildSelectHeaders(Executor& ex, const ast::SelectStmt& s, std::vector<std::string>& headers,
                               std::string& err) {
    headers.clear();
    int anon = 0;
    for (const auto& it : s.selectList) {
        if (it.kind == ast::SelectItemKind::Star) {
            if (it.starTable.has_value()) {
                size_t ti = static_cast<size_t>(-1);
                for (size_t i = 0; i < s.from.size(); ++i) {
                    std::string log = s.from[i].alias.empty() ? s.from[i].table : s.from[i].alias;
                    if (logicalNameMatch(log, *it.starTable)) {
                        ti = i;
                        break;
                    }
                }
                if (ti == static_cast<size_t>(-1)) {
                    err = "未知表或别名: " + *it.starTable;
                    return false;
                }
                Table& t = ex.loadTable(s.from[ti].table);
                std::string pfx = s.from[ti].alias.empty() ? s.from[ti].table : s.from[ti].alias;
                for (const auto& cn : t.getColumnNames()) {
                    headers.push_back(pfx + "." + cn);
                }
            } else {
                for (size_t ti = 0; ti < s.from.size(); ++ti) {
                    Table& t = ex.loadTable(s.from[ti].table);
                    std::string pfx = s.from[ti].alias.empty() ? s.from[ti].table : s.from[ti].alias;
                    for (const auto& cn : t.getColumnNames()) {
                        headers.push_back(pfx + "." + cn);
                    }
                }
            }
        } else {
            if (!it.expr) {
                err = "缺少表达式";
                return false;
            }
            if (!it.outputAlias.empty()) {
                headers.push_back(it.outputAlias);
            } else if (std::holds_alternative<ast::QColumnRef>(it.expr->node)) {
                const auto& cr = std::get<ast::QColumnRef>(it.expr->node);
                if (cr.table.has_value()) {
                    headers.push_back(*cr.table + "." + cr.name);
                } else {
                    headers.push_back(cr.name);
                }
            } else {
                headers.push_back("expr" + std::to_string(++anon));
            }
        }
    }
    return true;
}

static std::string rowFingerprintCells(const std::vector<std::optional<Cell>>& row) {
    std::string s;
    for (const auto& c : row) {
        s.push_back('\x1E');
        s += cellToString(c);
    }
    return s;
}

static std::vector<std::vector<std::optional<Cell>>> dedupeRowsStable(
    const std::vector<std::vector<std::optional<Cell>>>& in) {
    std::unordered_set<std::string> seen;
    std::vector<std::vector<std::optional<Cell>>> out;
    for (const auto& r : in) {
        std::string k = rowFingerprintCells(r);
        if (seen.insert(k).second) {
            out.push_back(r);
        }
    }
    return out;
}

static std::unordered_map<std::string, std::pair<std::vector<std::optional<Cell>>, int>> countRowsByKey(
    const std::vector<std::vector<std::optional<Cell>>>& rows) {
    std::unordered_map<std::string, std::pair<std::vector<std::optional<Cell>>, int>> m;
    for (const auto& r : rows) {
        std::string k = rowFingerprintCells(r);
        auto it = m.find(k);
        if (it == m.end()) {
            m[k] = {r, 1};
        } else {
            it->second.second++;
        }
    }
    return m;
}

static std::vector<std::vector<std::optional<Cell>>> applySetOperator(
    const std::vector<std::vector<std::optional<Cell>>>& a,
    const std::vector<std::vector<std::optional<Cell>>>& b,
    ast::SetOperator op,
    bool all) {
    auto concatDedupe = [&](const std::vector<std::vector<std::optional<Cell>>>& x,
                            const std::vector<std::vector<std::optional<Cell>>>& y) {
        std::vector<std::vector<std::optional<Cell>>> cat = x;
        cat.insert(cat.end(), y.begin(), y.end());
        return dedupeRowsStable(cat);
    };
    switch (op) {
    case ast::SetOperator::Union:
        if (all) {
            std::vector<std::vector<std::optional<Cell>>> out = a;
            out.insert(out.end(), b.begin(), b.end());
            return out;
        }
        return concatDedupe(a, b);
    case ast::SetOperator::Intersect: {
        if (all) {
            auto ma = countRowsByKey(a);
            auto mb = countRowsByKey(b);
            std::vector<std::vector<std::optional<Cell>>> out;
            for (const auto& [k, pa] : ma) {
                auto itb = mb.find(k);
                if (itb == mb.end()) {
                    continue;
                }
                int n = std::min(pa.second, itb->second.second);
                for (int i = 0; i < n; ++i) {
                    out.push_back(pa.first);
                }
            }
            return out;
        }
        auto da = dedupeRowsStable(a);
        auto db = dedupeRowsStable(b);
        std::unordered_set<std::string> sb;
        for (const auto& r : db) {
            sb.insert(rowFingerprintCells(r));
        }
        std::vector<std::vector<std::optional<Cell>>> out;
        for (const auto& r : da) {
            if (sb.count(rowFingerprintCells(r))) {
                out.push_back(r);
            }
        }
        return out;
    }
    case ast::SetOperator::Except: {
        if (all) {
            auto ma = countRowsByKey(a);
            auto mb = countRowsByKey(b);
            std::vector<std::vector<std::optional<Cell>>> out;
            for (auto& [k, pa] : ma) {
                int sub = 0;
                auto itb = mb.find(k);
                if (itb != mb.end()) {
                    sub = itb->second.second;
                }
                int rem = pa.second - sub;
                for (int i = 0; i < rem; ++i) {
                    out.push_back(pa.first);
                }
            }
            return out;
        }
        auto da = dedupeRowsStable(a);
        auto db = dedupeRowsStable(b);
        std::unordered_set<std::string> sb;
        for (const auto& r : db) {
            sb.insert(rowFingerprintCells(r));
        }
        std::vector<std::vector<std::optional<Cell>>> out;
        for (const auto& r : da) {
            if (!sb.count(rowFingerprintCells(r))) {
                out.push_back(r);
            }
        }
        return out;
    }
    }
    return a;
}

static bool resolveOrderColumnIndex(const ast::QueryExpr& e, const std::vector<std::string>& headers, size_t& outCol,
                                    std::string& err) {
    return std::visit(
        [&](const auto& alt) -> bool {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, ast::QLiteral>) {
                const Cell& cv = alt.value;
                std::int64_t ord = 0;
                if (std::holds_alternative<std::int64_t>(cv)) {
                    ord = std::get<std::int64_t>(cv);
                } else if (std::holds_alternative<double>(cv)) {
                    double d = std::get<double>(cv);
                    if (d != std::floor(d)) {
                        err = "ORDER BY 列序号须为整数";
                        return false;
                    }
                    ord = static_cast<std::int64_t>(d);
                } else {
                    err = "复合查询 ORDER BY 须为列序号(1..n)或列名";
                    return false;
                }
                if (ord < 1 || static_cast<size_t>(ord) > headers.size()) {
                    err = "ORDER BY 列序号越界";
                    return false;
                }
                outCol = static_cast<size_t>(ord - 1);
                return true;
            }
            if constexpr (std::is_same_v<T, ast::QColumnRef>) {
                for (size_t i = 0; i < headers.size(); ++i) {
                    if (alt.table.has_value()) {
                        std::string qual = *alt.table + "." + alt.name;
                        if (logicalNameMatch(headers[i], qual)) {
                            outCol = i;
                            return true;
                        }
                    } else {
                        auto dot = headers[i].rfind('.');
                        std::string shortName = (dot == std::string::npos) ? headers[i] : headers[i].substr(dot + 1);
                        if (logicalNameMatch(shortName, alt.name)) {
                            outCol = i;
                            return true;
                        }
                    }
                }
                err = "ORDER BY 未找到列";
                return false;
            }
            err = "复合查询 ORDER BY 仅支持列序号或简单列名";
            return false;
        },
        e.node);
}

static bool sortCompoundResultGrid(std::vector<std::vector<std::optional<Cell>>>& grid,
                                   const std::vector<std::string>& headers,
                                   const std::vector<ast::OrderByTerm>& orderBy,
                                   std::string& err) {
    if (orderBy.empty()) {
        return true;
    }
    std::vector<std::pair<size_t, bool>> ord;
    for (const auto& ob : orderBy) {
        if (!ob.expr) {
            continue;
        }
        size_t col = 0;
        if (!resolveOrderColumnIndex(*ob.expr, headers, col, err)) {
            return false;
        }
        ord.push_back({col, ob.descending});
    }
    if (ord.empty()) {
        return true;
    }
    std::stable_sort(grid.begin(), grid.end(),
                     [&](const std::vector<std::optional<Cell>>& a, const std::vector<std::optional<Cell>>& b) -> bool {
                         for (const auto& [col, desc] : ord) {
                             if (col >= a.size() || col >= b.size()) {
                                 return false;
                             }
                             int cmp = compareOptionalCellsLoose(a[col], b[col]);
                             if (cmp != 0) {
                                 return desc ? (cmp > 0) : (cmp < 0);
                             }
                         }
                         return false;
                     });
    return true;
}

static void applyCompoundLimit(std::vector<std::vector<std::optional<Cell>>>& grid,
                               std::optional<std::int64_t> limit,
                               std::optional<std::int64_t> offset) {
    std::int64_t off64 = offset.value_or(0);
    if (off64 < 0) {
        off64 = 0;
    }
    size_t off = static_cast<size_t>(off64);
    size_t n = grid.size();
    if (off > n) {
        off = n;
    }
    size_t end = n;
    if (limit.has_value()) {
        std::int64_t lim = *limit;
        if (lim < 0) {
            lim = 0;
        }
        end = std::min(n, off + static_cast<size_t>(lim));
    }
    std::vector<std::vector<std::optional<Cell>>> sliced;
    sliced.reserve(end - off);
    for (size_t i = off; i < end; ++i) {
        sliced.push_back(std::move(grid[i]));
    }
    grid = std::move(sliced);
}

struct ResultSortRow {
    QueryRowContext ctx;
    std::optional<std::vector<QueryRowContext>> bucket;
    std::vector<std::optional<Cell>> cells;
};

static bool evaluateSelectStmtGrid(Executor& ex, const std::string& db, const ast::SelectStmt& s,
                                   std::vector<std::string>& headers,
                                   std::vector<std::vector<std::optional<Cell>>>& outGrid,
                                   std::string& err) {
    err.clear();
    if (!validateFromJoinModes(s.from, err)) {
        return false;
    }
    if (!s.groupBy.empty() && selectHasStar(s)) {
        err = "GROUP BY 时不能使用 *";
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const auto& f : s.from) {
        std::string n = f.alias.empty() ? f.table : f.alias;
        std::string u = n;
        for (auto& c : u) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (seen.count(u)) {
            err = "表别名重复: " + n;
            return false;
        }
        seen.insert(std::move(u));
    }
    for (const auto& f : s.from) {
        if (!FileManager::tableExists(db, f.table)) {
            err = "Table not found: " + f.table;
            return false;
        }
    }
    if (!buildSelectHeaders(ex, s, headers, err)) {
        return false;
    }
    ExecSubqueryRunner runner(ex, db);
    std::vector<QueryRowContext> raw;
    std::vector<size_t> idxs(s.from.size(), 0);
    bool ok = true;
    dfsJoinContexts(ex, s.from, s.where.get(), nullptr, 0, idxs, raw, err, ok, &runner);
    if (!ok) {
        return false;
    }

    std::vector<ResultSortRow> result;
    const bool needGroup = !s.groupBy.empty() || selectListHasAggregate(s);
    if (needGroup) {
        std::unordered_map<std::string, std::vector<QueryRowContext>> buckets;
        if (s.groupBy.empty()) {
            buckets[""] = std::move(raw);
        } else {
            for (const auto& cx : raw) {
                std::string k = groupKeyForCtx(s, cx, err);
                if (k.empty() && !err.empty()) {
                    return false;
                }
                buckets[k].push_back(cx);
            }
        }
        for (auto& kv : buckets) {
            auto& bucket = kv.second;
            if (s.having) {
                bool hb = false;
                if (!evalHavingGroup(*s.having, bucket, hb, err, &runner)) {
                    return false;
                }
                if (!hb) {
                    continue;
                }
            }
            ResultSortRow gr;
            gr.ctx = bucket.empty() ? QueryRowContext{} : bucket[0];
            gr.bucket = std::move(bucket);
            if (!projectDataRow(ex, s, gr.ctx, &*gr.bucket, gr.cells, err, &runner)) {
                return false;
            }
            result.push_back(std::move(gr));
        }
    } else {
        for (const auto& cx : raw) {
            ResultSortRow gr;
            gr.ctx = cx;
            if (!projectDataRow(ex, s, cx, nullptr, gr.cells, err, &runner)) {
                return false;
            }
            result.push_back(std::move(gr));
        }
    }

    if (s.distinct) {
        std::unordered_set<std::string> dseen;
        std::vector<ResultSortRow> deduped;
        for (auto& r : result) {
            std::string k = rowFingerprintCells(r.cells);
            if (dseen.insert(k).second) {
                deduped.push_back(std::move(r));
            }
        }
        result = std::move(deduped);
    }

    if (!s.orderBy.empty()) {
        bool sortErr = false;
        std::stable_sort(result.begin(), result.end(),
                         [&](const ResultSortRow& a, const ResultSortRow& b) -> bool {
                             if (sortErr) {
                                 return false;
                             }
                             for (const auto& ob : s.orderBy) {
                                 if (!ob.expr) {
                                     continue;
                                 }
                                 std::optional<Cell> va;
                                 std::optional<Cell> vb;
                                 SqlType sta{};
                                 SqlType stb{};
                                 if (a.bucket.has_value() && exprContainsAggregate(*ob.expr)) {
                                     if (!evalAggregateExpr(*ob.expr, *a.bucket, va, sta, err)) {
                                         sortErr = true;
                                         return false;
                                     }
                                 } else {
                                     if (!evalQueryExprScalar(*ob.expr, a.ctx, va, sta, err, &runner)) {
                                         sortErr = true;
                                         return false;
                                     }
                                 }
                                 if (b.bucket.has_value() && exprContainsAggregate(*ob.expr)) {
                                     if (!evalAggregateExpr(*ob.expr, *b.bucket, vb, stb, err)) {
                                         sortErr = true;
                                         return false;
                                     }
                                 } else {
                                     if (!evalQueryExprScalar(*ob.expr, b.ctx, vb, stb, err, &runner)) {
                                         sortErr = true;
                                         return false;
                                     }
                                 }
                                 int cmp = compareOptionalCellsLoose(va, vb);
                                 if (cmp != 0) {
                                     return ob.descending ? (cmp > 0) : (cmp < 0);
                                 }
                             }
                             return false;
                         });
        if (sortErr) {
            return false;
        }
    }

    std::int64_t off64 = s.offset.value_or(0);
    if (off64 < 0) {
        off64 = 0;
    }
    size_t off = static_cast<size_t>(off64);
    size_t n = result.size();
    if (off > n) {
        off = n;
    }
    size_t end = n;
    if (s.limit.has_value()) {
        std::int64_t lim = *s.limit;
        if (lim < 0) {
            lim = 0;
        }
        end = std::min(n, off + static_cast<size_t>(lim));
    }
    outGrid.clear();
    outGrid.reserve(end - off);
    for (size_t i = off; i < end; ++i) {
        outGrid.push_back(std::move(result[i].cells));
    }
    return true;
}

static bool executeAdvancedSelect(Executor& ex, const std::string& db, const ast::SelectStmt& s) {
    std::vector<std::string> headers;
    std::vector<std::vector<std::optional<Cell>>> grid;
    std::string err;
    if (!evaluateSelectStmtGrid(ex, db, s, headers, grid, err)) {
        std::cout << err << "\n";
        return false;
    }
    printResultGrid(headers, grid);
    return true;
}

static bool executeCompoundSelect(Executor& ex, const std::string& db, const ast::CompoundSelectStmt& cs) {
    std::string err;
    if (cs.arms.empty()) {
        std::cout << "复合查询为空\n";
        return false;
    }
    if (cs.ops.size() + 1 != cs.arms.size()) {
        std::cout << "复合查询运算符数量错误\n";
        return false;
    }
    if (cs.arms.size() == 1) {
        return executeAdvancedSelect(ex, db, cs.arms[0]);
    }
    std::vector<std::string> headers;
    std::vector<std::vector<std::optional<Cell>>> acc;
    for (size_t i = 0; i < cs.arms.size(); ++i) {
        std::vector<std::string> h2;
        std::vector<std::vector<std::optional<Cell>>> g;
        if (!evaluateSelectStmtGrid(ex, db, cs.arms[i], h2, g, err)) {
            std::cout << err << "\n";
            return false;
        }
        if (i == 0) {
            headers = std::move(h2);
            acc = std::move(g);
        } else {
            if (h2.size() != headers.size()) {
                std::cout << "复合查询各 SELECT 的列数须一致\n";
                return false;
            }
            acc = applySetOperator(acc, g, cs.ops[i - 1].first, cs.ops[i - 1].second);
        }
    }
    if (!sortCompoundResultGrid(acc, headers, cs.compoundOrderBy, err)) {
        std::cout << err << "\n";
        return false;
    }
    applyCompoundLimit(acc, cs.compoundLimit, cs.compoundOffset);
    printResultGrid(headers, acc);
    return true;
}

} // namespace

/** 构造函数 */
Executor::Executor() {
    FileManager::ensureDatabase(currentDb);
}

/** 确保数据库连接 */
bool Executor::ensureConnected() {
    return FileManager::ensureDatabase(currentDb);
}

/**
 * 加载表
 * 如果表已加载则返回现有表，否则从文件加载
 */
Table& Executor::loadTable(const std::string& tableName) {
    auto it = tables.find(tableName);
    if (it != tables.end()) {
        return it->second;
    }
    tables[tableName] = FileManager::load(currentDb, tableName);
    return tables[tableName];
}

/**
 * 打印查询结果
 */
void Executor::printRows(const Table& table, const std::vector<Record>& rows, const std::vector<std::string>& selectedColumns) {
    std::vector<int> idxs;  // 列索引
    std::vector<std::string> headers;  // 列名
    std::vector<std::string> allNames = table.getColumnNames();

    if (selectedColumns.empty()) {
        // 显示所有列
        headers = allNames;
        for (size_t i = 0; i < headers.size(); ++i) {
            idxs.push_back(static_cast<int>(i));
        }
    } else {
        // 显示指定列
        for (const auto& c : selectedColumns) {
            int idx = table.columnIndex(c);
            if (idx >= 0) {
                idxs.push_back(idx);
                headers.push_back(c);
            }
        }
    }

    // 打印表头
    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) {
            std::cout << " | ";
        }
        std::cout << headers[i];
    }
    std::cout << "\n";

    // 打印数据行
    for (const auto& r : rows) {
        for (size_t i = 0; i < idxs.size(); ++i) {
            if (i > 0) {
                std::cout << " | ";
            }
            int idx = idxs[i];
            if (idx >= 0 && idx < static_cast<int>(r.cells.size())) {
                std::cout << cellToString(r.cells[static_cast<size_t>(idx)]);
            }
        }
        std::cout << "\n";
    }
    std::cout << "(" << rows.size() << " rows)\n";
}

/**
 * 执行 SQL 语句
 */
void Executor::execute(const ParseResult& pr) {
    if (!pr.errorMsg.empty()) {
        std::cout << "SQL 错误: " << pr.errorMsg << "\n";
        return;
    }
    if (!pr.stmt.has_value()) {
        return;
    }
    if (!ensureConnected()) {
        std::cout << "Cannot connect db.\n";
        return;
    }

    std::visit(
        [this](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            
            // 执行 CONNECT 语句
            if constexpr (std::is_same_v<T, ast::ConnectStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::CONNECT))) {
                    std::cout << "Permission denied: you need CONNECT privilege.\n";
                    FileManager::addAuditLog(currentUser, "CONNECT", arg.database, "DENIED");
                    return;
                }
                const auto& s = arg;
                currentDb = s.database.empty() ? "default" : s.database;
                tables.clear();
                if (FileManager::ensureDatabase(currentDb)) {
                    std::cout << "Connected to db: " << currentDb << "\n";
                    FileManager::addAuditLog(currentUser, "CONNECT", currentDb, "SUCCESS");
                } else {
                    std::cout << "Connect failed.\n";
                    FileManager::addAuditLog(currentUser, "CONNECT", s.database, "FAILED");
                }
            }
            
            // 执行 CREATE DATABASE 语句
            else if constexpr (std::is_same_v<T, ast::CreateDatabaseStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::CREATE_DB))) {
                    std::cout << "Permission denied: you need CREATE DATABASE privilege.\n";
                    FileManager::addAuditLog(currentUser, "CREATE DATABASE", arg.database, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (FileManager::ensureDatabase(s.database)) {
                    std::cout << "Database created: " << s.database << "\n";
                    FileManager::addAuditLog(currentUser, "CREATE DATABASE", s.database, "SUCCESS");
                } else {
                    std::cout << "Create database failed.\n";
                    FileManager::addAuditLog(currentUser, "CREATE DATABASE", s.database, "FAILED");
                }
            }

            // 执行 DROP DATABASE 语句
            else if constexpr (std::is_same_v<T, ast::DropDatabaseStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DROP_DB))) {
                    std::cout << "Permission denied: you need DROP DATABASE privilege.\n";
                    FileManager::addAuditLog(currentUser, "DROP DATABASE", arg.database, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (FileManager::dropDatabase(s.database)) {
                    std::cout << "Database dropped: " << s.database << "\n";
                    FileManager::addAuditLog(currentUser, "DROP DATABASE", s.database, "SUCCESS");
                    if (currentDb == s.database) {
                        currentDb = "default";
                        tables.clear();
                        FileManager::ensureDatabase(currentDb);
                    }
                } else {
                    std::cout << "Drop database failed.\n";
                    FileManager::addAuditLog(currentUser, "DROP DATABASE", s.database, "FAILED");
                }
            }
            
            // 执行 SHOW TABLES 语句
            else if constexpr (std::is_same_v<T, ast::ShowTablesStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SHOW_TABLES))) {
                    std::cout << "Permission denied: you need SHOW TABLES privilege.\n";
                    FileManager::addAuditLog(currentUser, "SHOW TABLES", "-", "DENIED");
                    return;
                }
                (void)arg;
                auto names = FileManager::listTables(currentDb);
                std::cout << "Tables_in_" << currentDb << "\n";
                for (const auto& name : names) {
                    std::cout << name << "\n";
                }
                std::cout << "(" << names.size() << " tables)\n";
            }

            // 执行 SHOW DATABASES 语句
            else if constexpr (std::is_same_v<T, ast::ShowDatabasesStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SHOW_DATABASES))) {
                    std::cout << "Permission denied: you need SHOW DATABASES privilege.\n";
                    FileManager::addAuditLog(currentUser, "SHOW DATABASES", "-", "DENIED");
                    return;
                }
                (void)arg;
                auto names = FileManager::listDatabases();
                std::cout << "Databases\n";
                for (const auto& name : names) {
                    std::cout << name << "\n";
                }
                std::cout << "(" << names.size() << " databases)\n";
            }

            // 执行 DESC 语句
            else if constexpr (std::is_same_v<T, ast::DescribeStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DESCRIBE))) {
                    std::cout << "Permission denied: you need DESCRIBE privilege.\n";
                    FileManager::addAuditLog(currentUser, "DESCRIBE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                std::cout << "Field\tType\tConstraints\n";
                for (const auto& c : t.getSchema()) {
                    std::cout << c.name << "\t" << sqlTypeToString(c.type) << "\t";
                    std::vector<std::string> bits;
                    if (c.primaryKey) {
                        bits.push_back("PRIMARY KEY");
                    }
                    if (c.unique && !c.primaryKey) {
                        bits.push_back("UNIQUE");
                    }
                    if (c.notNull && !c.primaryKey) {
                        bits.push_back("NOT NULL");
                    }
                    if (!c.fkRefTable.empty()) {
                        bits.push_back("REFERENCES " + c.fkRefTable + "(" + c.fkRefCol + ")");
                    }
                    if (!c.checkExpr.empty()) {
                        bits.push_back("CHECK (" + c.checkExpr + ")");
                    }
                    for (size_t bi = 0; bi < bits.size(); ++bi) {
                        if (bi > 0) {
                            std::cout << " ";
                        }
                        std::cout << bits[bi];
                    }
                    std::cout << "\n";
                }
                std::cout << "(" << t.getSchema().size() << " columns)\n";
                FileManager::addAuditLog(currentUser, "DESCRIBE", s.table, "SUCCESS");
            }

            // 执行 DROP TABLE 语句
            else if constexpr (std::is_same_v<T, ast::DropTableStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DROP_TABLE))) {
                    std::cout << "Permission denied: you need DROP TABLE privilege.\n";
                    FileManager::addAuditLog(currentUser, "DROP TABLE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                if (FileManager::isTableReferencedByForeignKeys(currentDb, s.table)) {
                    std::cout << "存在外键引用该表，拒绝 DROP TABLE。\n";
                    FileManager::addAuditLog(currentUser, "DROP TABLE", s.table, "DENIED");
                    return;
                }
                if (FileManager::dropTable(currentDb, s.table)) {
                    tables.erase(s.table);
                    std::cout << "Table dropped: " << s.table << "\n";
                    FileManager::addAuditLog(currentUser, "DROP TABLE", s.table, "SUCCESS");
                } else {
                    std::cout << "Drop table failed: " << s.table << "\n";
                    FileManager::addAuditLog(currentUser, "DROP TABLE", s.table, "FAILED");
                }
            }
            
            // 执行 ALTER TABLE 语句
            else if constexpr (std::is_same_v<T, ast::AlterTableStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::ALTER_TABLE))) {
                    std::cout << "Permission denied: you need ALTER TABLE privilege.\n";
                    FileManager::addAuditLog(currentUser, "ALTER TABLE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                bool success = false;
                switch (s.op) {
                case ast::AlterOperation::AddColumn:
                    if (t.primaryKeyColumnIndex() >= 0 && s.addPrimaryKey) {
                        std::cout << "表已有主键列，不能再添加主键列。\n";
                        break;
                    }
                    if (!t.getRecords().empty() && s.addNotNull && s.columnType == SqlType::Text) {
                        std::cout << "不能向非空表添加 NOT NULL 文本列（无默认值）。\n";
                        break;
                    }
                    {
                        ColumnDef cd;
                        cd.name = s.columnName;
                        cd.type = s.columnType;
                        cd.notNull = s.addNotNull;
                        cd.primaryKey = s.addPrimaryKey;
                        cd.unique = s.addUnique;
                        cd.checkExpr = s.addCheckExpr;
                        cd.fkRefTable = s.addFkRefTable;
                        cd.fkRefCol = s.addFkRefCol;
                        if (cd.primaryKey) {
                            cd.notNull = true;
                        }
                        std::vector<ColumnDef> schPlus = t.getSchema();
                        schPlus.push_back(cd);
                        if (!cd.fkRefTable.empty()) {
                            std::string fe;
                            if (!validateForeignKeyDefinition(currentDb, s.table, schPlus, cd.fkRefTable, cd.fkRefCol,
                                                                fe)) {
                                std::cout << fe << "\n";
                                break;
                            }
                        }
                        t.addColumn(cd);
                        if (!t.allUniqueConstraintsAmongRows()) {
                            t.dropColumn(s.columnName);
                            std::cout << "添加列失败：唯一性不满足（与现有数据冲突）。\n";
                            break;
                        }
                        std::string chkErr;
                        bool chkOk = true;
                        for (const auto& r : t.getRecords()) {
                            if (!evaluateAllCheckConstraints(t.getSchema(), r, chkErr)) {
                                t.dropColumn(s.columnName);
                                std::cout << chkErr << "\n";
                                chkOk = false;
                                break;
                            }
                        }
                        if (chkOk) {
                            success = true;
                            std::cout << "Column added: " << s.columnName << "\n";
                        }
                    }
                    break;
                case ast::AlterOperation::DropColumn:
                    if (FileManager::isParentColumnReferencedByFk(currentDb, s.table, s.columnName)) {
                        std::cout << "该列被外键引用，拒绝 DROP COLUMN。\n";
                        break;
                    }
                    t.dropColumn(s.columnName);
                    success = true;
                    std::cout << "Column dropped: " << s.columnName << "\n";
                    break;
                case ast::AlterOperation::ModifyColumn:
                    success = t.modifyColumn(s.columnName, s.columnType);
                    if (success) {
                        std::cout << "Column modified: " << s.columnName << "\n";
                    } else {
                        std::cout << "Column not found: " << s.columnName << "\n";
                    }
                    break;
                }
                if (success) {
                    FileManager::save(currentDb, t);
                    FileManager::addAuditLog(currentUser, "ALTER TABLE", s.table, "SUCCESS");
                }
            }

            // 执行 CREATE TABLE 语句
            else if constexpr (std::is_same_v<T, ast::CreateTableStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::CREATE_TABLE))) {
                    std::cout << "Permission denied: you need CREATE TABLE privilege.\n";
                    FileManager::addAuditLog(currentUser, "CREATE TABLE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (s.columns.empty()) {
                    std::cout << "CREATE TABLE requires columns.\n";
                    return;
                }
                for (const auto& c : s.columns) {
                    if (!c.fkRefTable.empty()) {
                        std::string fe;
                        if (!validateForeignKeyDefinition(currentDb, s.table, s.columns, c.fkRefTable, c.fkRefCol,
                                                            fe)) {
                            std::cout << fe << "\n";
                            return;
                        }
                    }
                }
                Table tab(s.table, s.columns);
                tables[s.table] = std::move(tab);
                FileManager::save(currentDb, tables[s.table]);
                std::cout << "Table created: " << s.table << "\n";
                FileManager::addAuditLog(currentUser, "CREATE TABLE", s.table, "SUCCESS");
            }

            // 执行 INSERT 语句
            else if constexpr (std::is_same_v<T, ast::InsertStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::INSERT))) {
                    std::cout << "Permission denied: you need INSERT privilege.\n";
                    FileManager::addAuditLog(currentUser, "INSERT", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                Record r;
                std::string err;
                if (!buildInsertRecord(t, s, r, err)) {
                    std::cout << err << "\n";
                    return;
                }
                if (!validateInsertIntegrity(*this, currentDb, t, s, r, err)) {
                    std::cout << err << "\n";
                    return;
                }
                t.insert(r);
                FileManager::save(currentDb, t);
                std::cout << "1 row inserted.\n";
                FileManager::addAuditLog(currentUser, "INSERT", s.table, "SUCCESS");
            }

            // 执行 SELECT / UNION / INTERSECT / EXCEPT
            else if constexpr (std::is_same_v<T, ast::CompoundSelectStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                const auto& cs = arg;
                std::string auditTarget;
                std::unordered_set<std::string> tablesSeen;
                for (const auto& arm : cs.arms) {
                    for (const auto& f : arm.from) {
                        tablesSeen.insert(f.table);
                    }
                }
                {
                    size_t i = 0;
                    for (const auto& tn : tablesSeen) {
                        if (i++ > 0) {
                            auditTarget += ",";
                        }
                        auditTarget += tn;
                    }
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SELECT))) {
                    std::cout << "Permission denied: you need SELECT privilege.\n";
                    FileManager::addAuditLog(currentUser, "SELECT", auditTarget.empty() ? "?" : auditTarget, "DENIED");
                    return;
                }
                if (!executeCompoundSelect(*this, currentDb, cs)) {
                    FileManager::addAuditLog(currentUser, "SELECT", auditTarget.empty() ? "?" : auditTarget, "FAILED");
                    return;
                }
                FileManager::addAuditLog(currentUser, "SELECT", auditTarget.empty() ? "?" : auditTarget, "SUCCESS");
            }
            
            // 执行 DELETE 语句
            else if constexpr (std::is_same_v<T, ast::DeleteStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DELETE))) {
                    std::cout << "Permission denied: you need DELETE privilege.\n";
                    FileManager::addAuditLog(currentUser, "DELETE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                std::string derr;
                if (!canDeleteParentRows(*this, currentDb, t, s, derr)) {
                    std::cout << derr << "\n";
                    return;
                }
                int n = t.deleteRows(s.whereColumn, s.whereValue);
                FileManager::save(currentDb, t);
                std::cout << n << " rows deleted.\n";
                FileManager::addAuditLog(currentUser, "DELETE", s.table, "SUCCESS");
            }

            // 执行 UPDATE 语句
            else if constexpr (std::is_same_v<T, ast::UpdateStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::UPDATE))) {
                    std::cout << "Permission denied: you need UPDATE privilege.\n";
                    FileManager::addAuditLog(currentUser, "UPDATE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                std::string uerr;
                if (!validateUpdateIntegrity(*this, currentDb, t, s, uerr)) {
                    std::cout << uerr << "\n";
                    return;
                }
                int n = t.updateRows(s.setColumn, s.setValue, s.whereColumn, s.whereValue);
                FileManager::save(currentDb, t);
                std::cout << n << " rows updated.\n";
                FileManager::addAuditLog(currentUser, "UPDATE", s.table, "SUCCESS");
            }

            // 执行 CREATE USER 语句
            else if constexpr (std::is_same_v<T, ast::CreateUserStmt>) {
                const auto& s = arg;
                if (s.username.empty()) {
                    std::cout << "Username cannot be empty.\n";
                    return;
                }

                // 检查是否有用户存在，如果没有则允许创建第一个管理员
                auto existingUsers = FileManager::listUsers();
                bool isFirstUser = existingUsers.empty();

                if (!isFirstUser) {
                    // 不是第一个用户，需要权限检查
                    if (currentUser.empty()) {
                        std::cout << "Permission denied: please login first.\n";
                        return;
                    }
                    if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::CREATE_USER))) {
                        std::cout << "Permission denied: you need CREATE USER privilege.\n";
                        FileManager::addAuditLog(currentUser, "CREATE USER", arg.username, "DENIED");
                        return;
                    }
                }

                // 确定角色
                int role = 2; // 默认 USER
                if (s.hasRole) {
                    switch (s.role) {
                        case ast::RoleType::ADMIN: role = 0; break;
                        case ast::RoleType::DBA: role = 1; break;
                        case ast::RoleType::USER: role = 2; break;
                    }
                } else if (isFirstUser) {
                    // 第一个用户默认是 ADMIN
                    role = 0;
                    std::cout << "INFO: First user created as ADMIN by default.\n";
                }

                if (FileManager::createUser(s.username, s.password, role)) {
                    std::string roleName = (role == 0) ? "ADMIN" : (role == 1) ? "DBA" : "USER";
                    std::cout << "User created: " << s.username << " (" << roleName << ")\n";
                    if (!isFirstUser) {
                        FileManager::addAuditLog(currentUser, "CREATE USER", s.username, "SUCCESS");
                    }
                } else {
                    std::cout << "User already exists or create failed: " << s.username << "\n";
                    if (!isFirstUser) {
                        FileManager::addAuditLog(currentUser, "CREATE USER", s.username, "FAILED");
                    }
                }
            }

            // 执行 DROP USER 语句
            else if constexpr (std::is_same_v<T, ast::DropUserStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DROP_USER))) {
                    std::cout << "Permission denied: you need DROP USER privilege.\n";
                    FileManager::addAuditLog(currentUser, "DROP USER", arg.username, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (FileManager::dropUser(s.username)) {
                    std::cout << "User dropped: " << s.username << "\n";
                    if (currentUser == s.username) {
                        currentUser.clear();
                        std::cout << "You have been logged out.\n";
                    }
                    FileManager::addAuditLog(currentUser, "DROP USER", s.username, "SUCCESS");
                } else {
                    std::cout << "User not found: " << s.username << "\n";
                    FileManager::addAuditLog(currentUser, "DROP USER", s.username, "FAILED");
                }
            }

            // 执行 SHOW USERS 语句
            else if constexpr (std::is_same_v<T, ast::ShowUsersStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SHOW_USERS))) {
                    std::cout << "Permission denied: you need SHOW USERS privilege.\n";
                    FileManager::addAuditLog(currentUser, "SHOW USERS", "-", "DENIED");
                    return;
                }
                (void)arg;
                auto users = FileManager::listUsers();
                std::cout << "Username    | Role\n";
                std::cout << "------------|------\n";
                for (const auto& u : users) {
                    std::string roleName;
                    switch (static_cast<ast::RoleType>(u.role)) {
                    case ast::RoleType::ADMIN: roleName = "ADMIN"; break;
                    case ast::RoleType::DBA: roleName = "DBA"; break;
                    case ast::RoleType::USER: roleName = "USER"; break;
                    default: roleName = "NONE"; break;
                    }
                    std::cout << u.username << "     | " << roleName << "\n";
                }
                std::cout << "(" << users.size() << " users)\n";
            }

            // 执行 LOGIN 语句
            else if constexpr (std::is_same_v<T, ast::LoginStmt>) {
                const auto& s = arg;
                if (FileManager::validateUser(s.username, s.password)) {
                    currentUser = s.username;
                    std::cout << "Login successful: " << s.username << "\n";
                    FileManager::addAuditLog(s.username, "LOGIN", "-", "SUCCESS");
                } else {
                    std::cout << "Login failed: invalid username or password.\n";
                    FileManager::addAuditLog(s.username, "LOGIN", "-", "FAILED");
                }
            }

            // 执行 LOGOUT 语句
            else if constexpr (std::is_same_v<T, ast::LogoutStmt>) {
                (void)arg;
                if (currentUser.empty()) {
                    std::cout << "No user is currently logged in.\n";
                } else {
                    std::cout << "Logged out: " << currentUser << "\n";
                    FileManager::addAuditLog(currentUser, "LOGOUT", "-", "SUCCESS");
                    currentUser.clear();
                }
            }

            // 执行 CREATE ROLE 语句
            else if constexpr (std::is_same_v<T, ast::CreateRoleStmt>) {
                (void)arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::CREATE_ROLE))) {
                    std::cout << "Permission denied: you need CREATE ROLE privilege.\n";
                    FileManager::addAuditLog(currentUser, "CREATE ROLE", "-", "DENIED");
                    return;
                }
                std::string roleName;
                switch (arg.role) {
                case ast::RoleType::ADMIN: roleName = "ADMIN"; break;
                case ast::RoleType::DBA: roleName = "DBA"; break;
                case ast::RoleType::USER: roleName = "USER"; break;
                }
                std::cout << "Role created: " << roleName << " (role assignment is automatic based on user creation)\n";
                FileManager::addAuditLog(currentUser, "CREATE ROLE", roleName, "SUCCESS");
            }

            // 执行 DROP ROLE 语句
            else if constexpr (std::is_same_v<T, ast::DropRoleStmt>) {
                (void)arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DROP_ROLE))) {
                    std::cout << "Permission denied: you need DROP ROLE privilege.\n";
                    FileManager::addAuditLog(currentUser, "DROP ROLE", "-", "DENIED");
                    return;
                }
                std::string roleName;
                switch (arg.role) {
                case ast::RoleType::ADMIN: roleName = "ADMIN"; break;
                case ast::RoleType::DBA: roleName = "DBA"; break;
                case ast::RoleType::USER: roleName = "USER"; break;
                }
                std::cout << "Role dropped: " << roleName << "\n";
                FileManager::addAuditLog(currentUser, "DROP ROLE", roleName, "SUCCESS");
            }

            // 执行 GRANT 语句
            else if constexpr (std::is_same_v<T, ast::GrantStmt>) {
                const auto& s = arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::GRANT))) {
                    std::cout << "Permission denied: you need GRANT privilege.\n";
                    FileManager::addAuditLog(currentUser, "GRANT", "-", "DENIED");
                    return;
                }
                if (s.hasRole) {
                    UserInfo targetUser = FileManager::getUser(s.username);
                    if (targetUser.username.empty()) {
                        std::cout << "User not found: " << s.username << "\n";
                        FileManager::addAuditLog(currentUser, "GRANT ROLE", s.username, "FAILED");
                        return;
                    }
                    targetUser.role = static_cast<int>(s.role);
                    FileManager::updateUser(targetUser);
                    std::string roleName;
                    switch (s.role) {
                    case ast::RoleType::ADMIN: roleName = "ADMIN"; break;
                    case ast::RoleType::DBA: roleName = "DBA"; break;
                    case ast::RoleType::USER: roleName = "USER"; break;
                    }
                    std::cout << "Role " << roleName << " granted to: " << s.username << "\n";
                    FileManager::addAuditLog(currentUser, "GRANT ROLE", s.username, "SUCCESS");
                } else {
                    for (auto priv : s.privileges) {
                        FileManager::grantPrivilege(s.username, static_cast<int>(priv));
                    }
                    std::cout << "Privileges granted to: " << s.username << "\n";
                    FileManager::addAuditLog(currentUser, "GRANT PRIVILEGES", s.username, "SUCCESS");
                }
            }

            // 执行 REVOKE 语句
            else if constexpr (std::is_same_v<T, ast::RevokeStmt>) {
                const auto& s = arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::REVOKE))) {
                    std::cout << "Permission denied: you need REVOKE privilege.\n";
                    FileManager::addAuditLog(currentUser, "REVOKE", "-", "DENIED");
                    return;
                }
                if (s.hasRole) {
                    UserInfo targetUser = FileManager::getUser(s.username);
                    if (targetUser.username.empty()) {
                        std::cout << "User not found: " << s.username << "\n";
                        FileManager::addAuditLog(currentUser, "REVOKE ROLE", s.username, "FAILED");
                        return;
                    }
                    targetUser.role = 0;
                    FileManager::updateUser(targetUser);
                    std::cout << "Role revoked from: " << s.username << "\n";
                    FileManager::addAuditLog(currentUser, "REVOKE ROLE", s.username, "SUCCESS");
                } else {
                    for (auto priv : s.privileges) {
                        FileManager::revokePrivilege(s.username, static_cast<int>(priv));
                    }
                    std::cout << "Privileges revoked from: " << s.username << "\n";
                    FileManager::addAuditLog(currentUser, "REVOKE PRIVILEGES", s.username, "SUCCESS");
                }
            }

            // 执行 SHOW GRANTS FOR 语句
            else if constexpr (std::is_same_v<T, ast::ShowGrantsStmt>) {
                const auto& s = arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SHOW_GRANTS))) {
                    std::cout << "Permission denied: you need SHOW GRANTS privilege.\n";
                    FileManager::addAuditLog(currentUser, "SHOW GRANTS", "-", "DENIED");
                    return;
                }
                UserInfo user = FileManager::getUser(s.username);
                if (user.username.empty()) {
                    std::cout << "User not found: " << s.username << "\n";
                    return;
                }
                std::cout << "Grants for " << s.username << ":\n";
                std::string roleName;
                switch (static_cast<ast::RoleType>(user.role)) {
                case ast::RoleType::ADMIN: roleName = "ADMIN"; break;
                case ast::RoleType::DBA: roleName = "DBA"; break;
                case ast::RoleType::USER: roleName = "USER"; break;
                default: roleName = "NONE"; break;
                }
                std::cout << "  Role: " << roleName << "\n";
                if (!user.privileges.empty()) {
                    std::cout << "  Privileges: ";
                    for (size_t i = 0; i < user.privileges.size(); ++i) {
                        if (i > 0) std::cout << ", ";
                        std::cout << user.privileges[i];
                    }
                    std::cout << "\n";
                }
            }

            // 执行 SHOW AUDIT LOG 语句
            else if constexpr (std::is_same_v<T, ast::ShowAuditLogStmt>) {
                (void)arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SHOW_AUDIT))) {
                    std::cout << "Permission denied: you need SHOW AUDIT privilege.\n";
                    return;
                }
                auto logs = FileManager::getAuditLogs();
                std::cout << "Timestamp            | User     | Action              | Target | Result\n";
                std::cout << "---------------------|----------|---------------------|--------|--------\n";
                for (const auto& log : logs) {
                    std::cout << log.timestamp;
                    std::cout.width(20 - log.timestamp.length());
                    std::cout << " | ";
                    std::cout.width(8);
                    std::cout << log.username << " | ";
                    std::cout.width(19);
                    std::cout << log.action << " | ";
                    std::cout.width(6);
                    std::cout << log.target << " | ";
                    std::cout << log.result << "\n";
                }
                std::cout << "(" << logs.size() << " log entries)\n";
            }
        },
        *pr.stmt);
}
