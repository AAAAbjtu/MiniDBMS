#ifndef QUERYEVAL_H
#define QUERYEVAL_H

#include "Ast.h"
#include "Table.h"
#include <optional>
#include <string>
#include <vector>

/** 外连接无匹配侧行占位 */
inline constexpr size_t kNullRowIndex = static_cast<size_t>(-1);

/**
 * 多表查询单行绑定：表指针、当前行下标、逻辑名（别名或表名）
 */
struct QueryRowContext {
    std::vector<const Table*> tables;
    std::vector<size_t> rowIndices;
    std::vector<std::string> logicalNames;
};

/** 由 Executor 实现，用于 IN / EXISTS 子查询 */
struct SubqueryRunner {
    virtual ~SubqueryRunner() = default;
    virtual bool evalExists(const std::string& selectSql, const QueryRowContext* outer, bool notExists, bool& out,
                            std::string& err) = 0;
    virtual bool evalIn(const std::string& selectSql, const QueryRowContext* outer, const Cell& left, bool notIn,
                        bool& out, std::string& err) = 0;
};

bool evalQueryExprBool(const ast::QueryExpr& expr, const QueryRowContext& ctx, bool& out, std::string& err,
                       SubqueryRunner* sub = nullptr);

/**
 * 标量求值；out 无值表示 SQL NULL（如外连接补空侧）
 */
bool evalQueryExprScalar(const ast::QueryExpr& expr, const QueryRowContext& ctx, std::optional<Cell>& out,
                         SqlType& outType, std::string& err, SubqueryRunner* sub = nullptr);

/** 分组聚合求值（仅 SELECT 列表/HAVING 中聚合函数） */
bool evalAggregateExpr(const ast::QueryExpr& expr, const std::vector<QueryRowContext>& groupRows,
                       std::optional<Cell>& out, SqlType& outType, std::string& err);

bool exprContainsAggregate(const ast::QueryExpr& expr);

#endif
