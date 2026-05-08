/**
 * SELECT / WHERE / ON / 聚合 表达式求值
 */

#include "QueryEval.h"
#include "Schema.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

bool logicalNameMatch(const std::string& a, const std::string& b) { return upper(a) == upper(b); }

bool resolveQualified(const QueryRowContext& ctx, const std::string& qual, size_t& tabIdx) {
    for (size_t i = 0; i < ctx.logicalNames.size(); ++i) {
        if (logicalNameMatch(ctx.logicalNames[i], qual)) {
            tabIdx = i;
            return true;
        }
    }
    return false;
}

bool resolveUnqualifiedColumn(const QueryRowContext& ctx, const std::string& colName, size_t& tabIdx, int& colIdx,
                              std::string& err) {
    int hits = 0;
    size_t lastTab = 0;
    int lastCol = -1;
    for (size_t i = 0; i < ctx.tables.size(); ++i) {
        int c = ctx.tables[i]->columnIndex(colName);
        if (c >= 0) {
            ++hits;
            lastTab = i;
            lastCol = c;
        }
    }
    if (hits == 0) {
        err = "未知列: " + colName;
        return false;
    }
    if (hits > 1) {
        err = "列名歧义: " + colName + "（请使用 表名.列名）";
        return false;
    }
    tabIdx = lastTab;
    colIdx = lastCol;
    return true;
}

bool loadCell(const QueryRowContext& ctx, const ast::QColumnRef& ref, std::optional<Cell>& out, SqlType& ty,
              std::string& err) {
    size_t tabIdx = 0;
    int colIdx = -1;
    if (ref.table.has_value()) {
        if (!resolveQualified(ctx, *ref.table, tabIdx)) {
            err = "未知表或别名: " + *ref.table;
            return false;
        }
        colIdx = ctx.tables[tabIdx]->columnIndex(ref.name);
        if (colIdx < 0) {
            err = "列不存在: " + *ref.table + "." + ref.name;
            return false;
        }
    } else {
        if (!resolveUnqualifiedColumn(ctx, ref.name, tabIdx, colIdx, err)) {
            return false;
        }
    }
    if (ctx.rowIndices[tabIdx] == kNullRowIndex) {
        out = std::nullopt;
        ty = SqlType::Text;
        return true;
    }
    const auto& recs = ctx.tables[tabIdx]->getRecords();
    size_t ri = ctx.rowIndices[tabIdx];
    if (ri >= recs.size() || static_cast<size_t>(colIdx) >= recs[ri].cells.size()) {
        err = "行数据异常";
        return false;
    }
    out = recs[ri].cells[static_cast<size_t>(colIdx)];
    ty = ctx.tables[tabIdx]->columnType(colIdx);
    return true;
}

bool cellTruthyOpt(const std::optional<Cell>& c) {
    if (!c.has_value()) {
        return false;
    }
    return cellTruthy(*c);
}

bool applyCompare(const std::string& op, const Cell& a, const Cell& b, bool& out) {
    if (op == "=" || op == "==") {
        auto c = cellCompareLoose(a, b);
        if (!c.has_value()) {
            return false;
        }
        out = (*c == 0);
        return true;
    }
    if (op == "<>" || op == "!=") {
        auto c = cellCompareLoose(a, b);
        if (!c.has_value()) {
            return false;
        }
        out = (*c != 0);
        return true;
    }
    auto c = cellCompareLoose(a, b);
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

bool applyCompareOpt(const std::string& op, const std::optional<Cell>& a, const std::optional<Cell>& b, bool& out) {
    if (!a.has_value() || !b.has_value()) {
        out = false;
        return true;
    }
    return applyCompare(op, *a, *b, out);
}

bool evalArithmetic(const std::string& op, const Cell& a, SqlType ta, const Cell& b, SqlType tb, Cell& out,
                    SqlType& outType, std::string& err) {
    auto toD = [](const Cell& c, double& d) -> bool {
        if (std::holds_alternative<std::int64_t>(c)) {
            d = static_cast<double>(*std::get_if<std::int64_t>(&c));
            return true;
        }
        if (std::holds_alternative<double>(c)) {
            d = *std::get_if<double>(&c);
            return true;
        }
        return false;
    };
    double da = 0.0;
    double db = 0.0;
    if (!toD(a, da) || !toD(b, db)) {
        err = "算术运算需要数值类型";
        return false;
    }
    bool bothInt = ta == SqlType::Int && tb == SqlType::Int && std::holds_alternative<std::int64_t>(a) &&
                   std::holds_alternative<std::int64_t>(b);
    if (op == "+") {
        if (bothInt) {
            out = Cell{(*std::get_if<std::int64_t>(&a)) + (*std::get_if<std::int64_t>(&b))};
            outType = SqlType::Int;
        } else {
            out = Cell{da + db};
            outType = SqlType::Float;
        }
        return true;
    }
    if (op == "-") {
        if (bothInt) {
            out = Cell{(*std::get_if<std::int64_t>(&a)) - (*std::get_if<std::int64_t>(&b))};
            outType = SqlType::Int;
        } else {
            out = Cell{da - db};
            outType = SqlType::Float;
        }
        return true;
    }
    if (op == "*") {
        if (bothInt) {
            out = Cell{(*std::get_if<std::int64_t>(&a)) * (*std::get_if<std::int64_t>(&b))};
            outType = SqlType::Int;
        } else {
            out = Cell{da * db};
            outType = SqlType::Float;
        }
        return true;
    }
    if (op == "/") {
        if (db == 0.0) {
            err = "除零";
            return false;
        }
        out = Cell{da / db};
        outType = SqlType::Float;
        return true;
    }
    err = "未知运算符";
    return false;
}

bool isAggName(const std::string& name) {
    std::string u = upper(name);
    return u == "COUNT" || u == "SUM" || u == "AVG" || u == "MIN" || u == "MAX";
}

bool evalScalarCall(const ast::QCall& call, const QueryRowContext& ctx, std::optional<Cell>& out, SqlType& outType,
                    std::string& err, SubqueryRunner* sub) {
    std::string fn = upper(call.name);
    if (isAggName(call.name)) {
        err = "聚合函数仅允许出现在含聚合的 SELECT 或 HAVING 中";
        return false;
    }
    if (fn == "UPPER" || fn == "LOWER") {
        if (call.args.size() != 1) {
            err = fn + " 需要 1 个参数";
            return false;
        }
        std::optional<Cell> a;
        SqlType ta{};
        if (!evalQueryExprScalar(*call.args[0], ctx, a, ta, err, sub)) {
            return false;
        }
        if (!a.has_value()) {
            out = std::nullopt;
            outType = SqlType::Text;
            return true;
        }
        const auto* s = std::get_if<std::string>(&*a);
        if (!s) {
            err = fn + " 需要文本参数";
            return false;
        }
        std::string r = *s;
        if (fn == "UPPER") {
            std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
        } else {
            std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
        }
        out = Cell{std::move(r)};
        outType = SqlType::Text;
        return true;
    }
    if (fn == "LENGTH") {
        if (call.args.size() != 1) {
            err = "LENGTH 需要 1 个参数";
            return false;
        }
        std::optional<Cell> a;
        SqlType ta{};
        if (!evalQueryExprScalar(*call.args[0], ctx, a, ta, err, sub)) {
            return false;
        }
        if (!a.has_value()) {
            out = std::nullopt;
            outType = SqlType::Int;
            return true;
        }
        const auto* s = std::get_if<std::string>(&*a);
        if (!s) {
            err = "LENGTH 需要文本参数";
            return false;
        }
        out = Cell{static_cast<std::int64_t>(s->size())};
        outType = SqlType::Int;
        return true;
    }
    if (fn == "ABS") {
        if (call.args.size() != 1) {
            err = "ABS 需要 1 个参数";
            return false;
        }
        std::optional<Cell> a;
        SqlType ta{};
        if (!evalQueryExprScalar(*call.args[0], ctx, a, ta, err, sub)) {
            return false;
        }
        if (!a.has_value()) {
            out = std::nullopt;
            outType = SqlType::Int;
            return true;
        }
        if (std::holds_alternative<std::int64_t>(*a)) {
            std::int64_t v = *std::get_if<std::int64_t>(&*a);
            out = Cell{std::llabs(v)};
            outType = SqlType::Int;
            return true;
        }
        if (std::holds_alternative<double>(*a)) {
            out = Cell{std::fabs(*std::get_if<double>(&*a))};
            outType = SqlType::Float;
            return true;
        }
        err = "ABS 需要数值参数";
        return false;
    }
    if (fn == "ROUND") {
        if (call.args.size() != 1 && call.args.size() != 2) {
            err = "ROUND 需要 1 或 2 个参数";
            return false;
        }
        std::optional<Cell> a;
        SqlType ta{};
        if (!evalQueryExprScalar(*call.args[0], ctx, a, ta, err, sub)) {
            return false;
        }
        if (!a.has_value()) {
            out = std::nullopt;
            outType = SqlType::Float;
            return true;
        }
        double x = 0.0;
        if (std::holds_alternative<std::int64_t>(*a)) {
            x = static_cast<double>(*std::get_if<std::int64_t>(&*a));
        } else if (std::holds_alternative<double>(*a)) {
            x = *std::get_if<double>(&*a);
        } else {
            err = "ROUND 需要数值参数";
            return false;
        }
        int places = 0;
        if (call.args.size() == 2) {
            std::optional<Cell> b;
            SqlType tb{};
            if (!evalQueryExprScalar(*call.args[1], ctx, b, tb, err, sub)) {
                return false;
            }
            if (!b.has_value() || !std::holds_alternative<std::int64_t>(*b)) {
                err = "ROUND 小数位须为整数";
                return false;
            }
            places = static_cast<int>(*std::get_if<std::int64_t>(&*b));
            if (places < 0) {
                places = 0;
            }
        }
        double mul = std::pow(10.0, places);
        out = Cell{std::round(x * mul) / mul};
        outType = SqlType::Float;
        return true;
    }
    if (fn == "CONCAT") {
        if (call.args.empty()) {
            err = "CONCAT 至少 1 个参数";
            return false;
        }
        std::string acc;
        for (const auto& arg : call.args) {
            std::optional<Cell> a;
            SqlType ta{};
            if (!evalQueryExprScalar(*arg, ctx, a, ta, err, sub)) {
                return false;
            }
            acc += cellToString(a);
        }
        out = Cell{std::move(acc)};
        outType = SqlType::Text;
        return true;
    }
    if (fn == "COALESCE") {
        if (call.args.empty()) {
            err = "COALESCE 至少 1 个参数";
            return false;
        }
        for (const auto& arg : call.args) {
            std::optional<Cell> a;
            SqlType ta{};
            if (!evalQueryExprScalar(*arg, ctx, a, ta, err, sub)) {
                return false;
            }
            if (!a.has_value()) {
                continue;
            }
            if (ta == SqlType::Text) {
                const auto* s = std::get_if<std::string>(&*a);
                if (s && !s->empty()) {
                    out = *a;
                    outType = ta;
                    return true;
                }
            } else if (cellTruthy(*a)) {
                out = *a;
                outType = ta;
                return true;
            }
        }
        return evalQueryExprScalar(*call.args.back(), ctx, out, outType, err, sub);
    }
    err = "未知函数: " + call.name;
    return false;
}

double cellToDouble(const Cell& c) {
    if (std::holds_alternative<std::int64_t>(c)) {
        return static_cast<double>(*std::get_if<std::int64_t>(&c));
    }
    if (std::holds_alternative<double>(c)) {
        return *std::get_if<double>(&c);
    }
    return 0.0;
}

} // namespace

bool exprContainsAggregate(const ast::QueryExpr& expr) {
    return std::visit(
        [&](const auto& alt) -> bool {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, ast::QCall>) {
                return isAggName(alt.name);
            } else if constexpr (std::is_same_v<T, ast::QUnary>) {
                return exprContainsAggregate(*alt.child);
            } else if constexpr (std::is_same_v<T, ast::QBinary>) {
                return exprContainsAggregate(*alt.left) || exprContainsAggregate(*alt.right);
            } else if constexpr (std::is_same_v<T, ast::QInSubquery>) {
                return exprContainsAggregate(*alt.left);
            }
            return false;
        },
        expr.node);
}

bool evalAggregateExpr(const ast::QueryExpr& expr, const std::vector<QueryRowContext>& groupRows,
                       std::optional<Cell>& out, SqlType& outType, std::string& err) {
    return std::visit(
        [&](const auto& alt) -> bool {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, ast::QCall>) {
                std::string fn = upper(alt.name);
                if (fn == "COUNT") {
                    if (alt.countStar || alt.args.empty()) {
                        out = Cell{static_cast<std::int64_t>(groupRows.size())};
                        outType = SqlType::Int;
                        return true;
                    }
                    if (alt.args.size() != 1) {
                        err = "COUNT 参数个数无效";
                        return false;
                    }
                    std::int64_t c = 0;
                    for (const auto& g : groupRows) {
                        std::optional<Cell> v;
                        SqlType vt{};
                        if (!evalQueryExprScalar(*alt.args[0], g, v, vt, err, nullptr)) {
                            return false;
                        }
                        if (v.has_value()) {
                            ++c;
                        }
                    }
                    out = Cell{c};
                    outType = SqlType::Int;
                    return true;
                }
                if (fn == "SUM" || fn == "AVG") {
                    if (alt.args.size() != 1) {
                        err = fn + " 需要 1 个参数";
                        return false;
                    }
                    double s = 0.0;
                    std::int64_t cnt = 0;
                    for (const auto& g : groupRows) {
                        std::optional<Cell> v;
                        SqlType vt{};
                        if (!evalQueryExprScalar(*alt.args[0], g, v, vt, err, nullptr)) {
                            return false;
                        }
                        if (!v.has_value()) {
                            continue;
                        }
                        if (!std::holds_alternative<std::int64_t>(*v) && !std::holds_alternative<double>(*v)) {
                            err = fn + " 需要数值参数";
                            return false;
                        }
                        s += cellToDouble(*v);
                        ++cnt;
                    }
                    if (fn == "SUM") {
                        out = Cell{s};
                        outType = SqlType::Float;
                        return true;
                    }
                    if (cnt == 0) {
                        out = std::nullopt;
                        outType = SqlType::Float;
                        return true;
                    }
                    out = Cell{s / static_cast<double>(cnt)};
                    outType = SqlType::Float;
                    return true;
                }
                if (fn == "MIN" || fn == "MAX") {
                    if (alt.args.size() != 1) {
                        err = fn + " 需要 1 个参数";
                        return false;
                    }
                    std::optional<Cell> best;
                    SqlType bt = SqlType::Text;
                    bool first = true;
                    for (const auto& g : groupRows) {
                        std::optional<Cell> v;
                        SqlType vt{};
                        if (!evalQueryExprScalar(*alt.args[0], g, v, vt, err, nullptr)) {
                            return false;
                        }
                        if (!v.has_value()) {
                            continue;
                        }
                        if (first) {
                            best = *v;
                            bt = vt;
                            first = false;
                        } else {
                            auto cmp = cellCompareLoose(*best, *v);
                            if (!cmp.has_value()) {
                                err = "MIN/MAX 类型不一致";
                                return false;
                            }
                            if (fn == "MIN" && *cmp > 0) {
                                best = *v;
                                bt = vt;
                            }
                            if (fn == "MAX" && *cmp < 0) {
                                best = *v;
                                bt = vt;
                            }
                        }
                    }
                    out = best;
                    outType = bt;
                    return true;
                }
                err = "未知聚合: " + alt.name;
                return false;
            } else if constexpr (std::is_same_v<T, ast::QColumnRef>) {
                if (groupRows.empty()) {
                    err = "空分组";
                    return false;
                }
                return loadCell(groupRows[0], alt, out, outType, err);
            } else if constexpr (std::is_same_v<T, ast::QLiteral>) {
                out = alt.value;
                if (std::holds_alternative<std::int64_t>(*out)) {
                    outType = SqlType::Int;
                } else if (std::holds_alternative<double>(*out)) {
                    outType = SqlType::Float;
                } else {
                    outType = SqlType::Text;
                }
                return true;
            } else if constexpr (std::is_same_v<T, ast::QUnary>) {
                err = "聚合模式下不支持该表达式";
                return false;
            } else if constexpr (std::is_same_v<T, ast::QBinary>) {
                (void)alt;
                err = "聚合投影请使用 列、字面量 或 聚合函数(含 SUM(a+b) 等)";
                return false;
            }
            err = "不支持的聚合表达式";
            return false;
        },
        expr.node);
}

bool evalQueryExprScalar(const ast::QueryExpr& expr, const QueryRowContext& ctx, std::optional<Cell>& out,
                         SqlType& outType, std::string& err, SubqueryRunner* sub) {
    return std::visit(
        [&](auto& alt) -> bool {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, ast::QLiteral>) {
                out = alt.value;
                if (std::holds_alternative<std::int64_t>(*out)) {
                    outType = SqlType::Int;
                } else if (std::holds_alternative<double>(*out)) {
                    outType = SqlType::Float;
                } else {
                    outType = SqlType::Text;
                }
                return true;
            } else if constexpr (std::is_same_v<T, ast::QColumnRef>) {
                return loadCell(ctx, alt, out, outType, err);
            } else if constexpr (std::is_same_v<T, ast::QUnary>) {
                bool b = false;
                if (!evalQueryExprBool(*alt.child, ctx, b, err, sub)) {
                    return false;
                }
                if (alt.op == ast::QUnary::Op::Not) {
                    out = Cell{static_cast<std::int64_t>(b ? 0 : 1)};
                    outType = SqlType::Int;
                    return true;
                }
                err = "未知一元运算符";
                return false;
            } else if constexpr (std::is_same_v<T, ast::QBinary>) {
                if (alt.op == "AND") {
                    bool l = false;
                    bool r = false;
                    if (!evalQueryExprBool(*alt.left, ctx, l, err, sub)) {
                        return false;
                    }
                    if (!l) {
                        out = Cell{static_cast<std::int64_t>(0)};
                        outType = SqlType::Int;
                        return true;
                    }
                    if (!evalQueryExprBool(*alt.right, ctx, r, err, sub)) {
                        return false;
                    }
                    out = Cell{static_cast<std::int64_t>(r ? 1 : 0)};
                    outType = SqlType::Int;
                    return true;
                }
                if (alt.op == "OR") {
                    bool l = false;
                    if (!evalQueryExprBool(*alt.left, ctx, l, err, sub)) {
                        return false;
                    }
                    if (l) {
                        out = Cell{static_cast<std::int64_t>(1)};
                        outType = SqlType::Int;
                        return true;
                    }
                    bool r = false;
                    if (!evalQueryExprBool(*alt.right, ctx, r, err, sub)) {
                        return false;
                    }
                    out = Cell{static_cast<std::int64_t>(r ? 1 : 0)};
                    outType = SqlType::Int;
                    return true;
                }
                if (alt.op == "=" || alt.op == "==" || alt.op == "<>" || alt.op == "!=" || alt.op == "<" || alt.op == "<=" ||
                    alt.op == ">" || alt.op == ">=") {
                    std::optional<Cell> a;
                    std::optional<Cell> b;
                    SqlType ta{};
                    SqlType tb{};
                    if (!evalQueryExprScalar(*alt.left, ctx, a, ta, err, sub)) {
                        return false;
                    }
                    if (!evalQueryExprScalar(*alt.right, ctx, b, tb, err, sub)) {
                        return false;
                    }
                    bool res = false;
                    if (!applyCompareOpt(alt.op, a, b, res)) {
                        err = "无法比较的类型组合";
                        return false;
                    }
                    out = Cell{static_cast<std::int64_t>(res ? 1 : 0)};
                    outType = SqlType::Int;
                    return true;
                }
                if (alt.op == "+" || alt.op == "-" || alt.op == "*" || alt.op == "/") {
                    std::optional<Cell> a;
                    std::optional<Cell> b;
                    SqlType ta{};
                    SqlType tb{};
                    if (!evalQueryExprScalar(*alt.left, ctx, a, ta, err, sub)) {
                        return false;
                    }
                    if (!evalQueryExprScalar(*alt.right, ctx, b, tb, err, sub)) {
                        return false;
                    }
                    if (!a.has_value() || !b.has_value()) {
                        out = std::nullopt;
                        outType = SqlType::Int;
                        return true;
                    }
                    Cell o;
                    SqlType ot;
                    return evalArithmetic(alt.op, *a, ta, *b, tb, o, ot, err) && (out = o, outType = ot, true);
                }
                err = "不支持的二元表达式";
                return false;
            } else if constexpr (std::is_same_v<T, ast::QCall>) {
                return evalScalarCall(alt, ctx, out, outType, err, sub);
            } else if constexpr (std::is_same_v<T, ast::QInSubquery>) {
                err = "IN 子查询仅用于布尔上下文";
                return false;
            } else if constexpr (std::is_same_v<T, ast::QExistsSubquery>) {
                err = "EXISTS 仅用于布尔上下文";
                return false;
            }
            err = "表达式内部错误";
            return false;
        },
        expr.node);
}

bool evalQueryExprBool(const ast::QueryExpr& expr, const QueryRowContext& ctx, bool& out, std::string& err,
                       SubqueryRunner* sub) {
    return std::visit(
        [&](auto& alt) -> bool {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, ast::QUnary>) {
                if (alt.op != ast::QUnary::Op::Not) {
                    err = "未知一元运算符";
                    return false;
                }
                bool inner = false;
                if (!evalQueryExprBool(*alt.child, ctx, inner, err, sub)) {
                    return false;
                }
                out = !inner;
                return true;
            } else if constexpr (std::is_same_v<T, ast::QBinary>) {
                if (alt.op == "AND") {
                    bool a = false;
                    if (!evalQueryExprBool(*alt.left, ctx, a, err, sub)) {
                        return false;
                    }
                    if (!a) {
                        out = false;
                        return true;
                    }
                    return evalQueryExprBool(*alt.right, ctx, out, err, sub);
                }
                if (alt.op == "OR") {
                    bool a = false;
                    if (!evalQueryExprBool(*alt.left, ctx, a, err, sub)) {
                        return false;
                    }
                    if (a) {
                        out = true;
                        return true;
                    }
                    return evalQueryExprBool(*alt.right, ctx, out, err, sub);
                }
                if (alt.op == "=" || alt.op == "==" || alt.op == "<>" || alt.op == "!=" || alt.op == "<" || alt.op == "<=" ||
                    alt.op == ">" || alt.op == ">=") {
                    std::optional<Cell> a;
                    std::optional<Cell> b;
                    SqlType ta{};
                    SqlType tb{};
                    if (!evalQueryExprScalar(*alt.left, ctx, a, ta, err, sub)) {
                        return false;
                    }
                    if (!evalQueryExprScalar(*alt.right, ctx, b, tb, err, sub)) {
                        return false;
                    }
                    return applyCompareOpt(alt.op, a, b, out);
                }
                std::optional<Cell> s;
                SqlType st{};
                if (!evalQueryExprScalar(expr, ctx, s, st, err, sub)) {
                    return false;
                }
                out = cellTruthyOpt(s);
                return true;
            } else if constexpr (std::is_same_v<T, ast::QInSubquery>) {
                std::optional<Cell> lhs;
                SqlType lt{};
                if (!evalQueryExprScalar(*alt.left, ctx, lhs, lt, err, sub)) {
                    return false;
                }
                if (!lhs.has_value()) {
                    out = false;
                    return true;
                }
                if (!alt.inValues.empty()) {
                    bool found = false;
                    for (const Cell& v : alt.inValues) {
                        bool eq = false;
                        if (!applyCompare("=", *lhs, v, eq)) {
                            err = "IN 列表比较失败";
                            return false;
                        }
                        if (eq) {
                            found = true;
                            break;
                        }
                    }
                    out = alt.notIn ? !found : found;
                    return true;
                }
                if (!sub) {
                    err = "IN (子查询) 需要执行上下文";
                    return false;
                }
                return sub->evalIn(alt.subquerySql, &ctx, *lhs, alt.notIn, out, err);
            } else if constexpr (std::is_same_v<T, ast::QExistsSubquery>) {
                if (!sub) {
                    err = "EXISTS 子查询需要执行上下文";
                    return false;
                }
                return sub->evalExists(alt.subquerySql, &ctx, alt.notExists, out, err);
            } else {
                std::optional<Cell> s;
                SqlType st{};
                if (!evalQueryExprScalar(expr, ctx, s, st, err, sub)) {
                    return false;
                }
                out = cellTruthyOpt(s);
                return true;
            }
        },
        expr.node);
}
