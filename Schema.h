#ifndef SCHEMA_H
#define SCHEMA_H

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

/**
 * 列类型：存储层与 SQL 语义统一
 */
enum class SqlType {
    Int,    // 整数类型（64位）
    Float,  // 浮点类型（双精度）
    Text    // 文本类型（UTF-8）
};

/**
 * 列定义
 * 包含列名和列类型
 */
struct ColumnDef {
    std::string name;  // 列名
    SqlType type = SqlType::Text;  // 列类型，默认文本类型
    bool notNull = false;     // NOT NULL：TEXT 不允许空串；整型/浮点允许 0
    bool primaryKey = false;  // 主键：隐含 NOT NULL，且列值全局唯一
    bool unique = false;      // UNIQUE（非主键列可单独声明）
    std::string checkExpr;    // CHECK 条件体（不含外层括号），空表示无
    std::string fkRefTable;   // 外键引用表，空表示无
    std::string fkRefCol;     // 外键引用列（须为被引用表的 PRIMARY KEY 或 UNIQUE）
};

/**
 * 单元格：与列类型一一对应
 * 使用 variant 存储不同类型的值
 */
using Cell = std::variant<std::int64_t, double, std::string>;

/**
 * 识别 SQL 类型关键字
 * 例如：INT, FLOAT, TEXT 等
 * @param word 类型关键字
 * @return 对应的 SqlType，如果无法识别则返回 nullopt
 */
std::optional<SqlType> tryParseSqlTypeKeyword(const std::string& word);

/**
 * 将字面量按列类型转为 Cell
 * @param raw 原始字面量字符串
 * @param t 目标列类型
 * @return 转换后的 Cell，如果转换失败返回 nullopt
 */
std::optional<Cell> parseLiteralToCell(const std::string& raw, SqlType t);

/**
 * 将 Cell 转换为字符串
 * @param c Cell 对象
 * @return 字符串表示
 */
std::string cellToString(const Cell& c);

/** 打印查询结果中的可空单元格 */
std::string cellToString(const std::optional<Cell>& c);

/**
 * 同类型相等比较（用于 WHERE 条件）
 * @param a 第一个 Cell
 * @param b 第二个 Cell
 * @param t 列类型
 * @return 是否相等
 */
bool cellEqualsTyped(const Cell& a, const Cell& b, SqlType t);

/**
 * 将无类型数字字面量解析为 Cell（无小数部分则为 INT，否则 FLOAT）
 */
std::optional<Cell> parseNumericLiteralCell(const std::string& raw);

/**
 * 用于表达式的真值：INT/FLOAT 非零、TEXT 非空串为真
 */
bool cellTruthy(const Cell& c);

/**
 * 宽松比较（用于二元比较表达式）：两操作数尽量提升为双精度或按字符串比较
 * @return 小于 -1，等于 0，大于 1；无法比较时返回 nullopt
 */
std::optional<int> cellCompareLoose(const Cell& a, const Cell& b);

/**
 * 将 SqlType 转换为字符串
 * @param t SqlType 枚举值
 * @return 对应的字符串表示
 */
std::string sqlTypeToString(SqlType t);

/**
 * 是否违反 NOT NULL 约束（本引擎无 SQL NULL，TEXT 空串视为违反）
 */
bool cellViolatesNotNull(const Cell& c, SqlType t);

#endif
