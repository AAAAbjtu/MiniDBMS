/**
 * Schema 模块实现
 * 负责数据类型转换和比较操作
 */

#include "Schema.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace {

/**
 * 字符串转大写
 * @param s 输入字符串
 * @return 大写后的字符串
 */
std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

/**
 * 去除字符串首尾空白字符
 * @param s 输入字符串
 * @return 去除空白后的字符串
 */
std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

} // namespace

/**
 * 将 SQL 类型关键字转换为 SqlType 枚举
 * 支持的类型：INT/INTEGER/BIGINT, FLOAT/REAL/DOUBLE, TEXT/VARCHAR/STRING/CHAR
 * @param word SQL 类型关键字
 * @return 对应的 SqlType，无法识别返回 nullopt
 */
std::optional<SqlType> tryParseSqlTypeKeyword(const std::string& word) {
    std::string u = upper(trim(word));
    if (u == "INT" || u == "INTEGER" || u == "BIGINT" || u == "SMALLINT" || u == "TINYINT") {
        return SqlType::Int;
    }
    if (u == "FLOAT" || u == "REAL" || u == "DOUBLE") {
        return SqlType::Float;
    }
    if (u == "TEXT" || u == "VARCHAR" || u == "STRING" || u == "CHAR") {
        return SqlType::Text;
    }
    return std::nullopt;
}

/**
 * 将字符串字面量转换为指定类型的 Cell
 * @param raw 原始字符串
 * @param t 目标类型
 * @return 转换后的 Cell，失败返回 nullopt
 */
std::optional<Cell> parseLiteralToCell(const std::string& raw, SqlType t) {
    std::string s = trim(raw);
    try {
        if (t == SqlType::Int) {
            size_t idx = 0;
            std::int64_t v = static_cast<std::int64_t>(std::stoll(s, &idx, 10));
            if (idx != s.size()) {
                return std::nullopt;
            }
            return Cell{v};
        }
        if (t == SqlType::Float) {
            size_t idx = 0;
            double v = std::stod(s, &idx);
            if (idx != s.size()) {
                return std::nullopt;
            }
            return Cell{v};
        }
        return Cell{s};
    } catch (...) {
        return std::nullopt;
    }
}

/**
 * 将 Cell 转换为字符串
 * @param c Cell 对象
 * @return 字符串表示
 */
std::string cellToString(const std::optional<Cell>& c) {
    if (!c.has_value()) {
        return "NULL";
    }
    return cellToString(*c);
}

std::string cellToString(const Cell& c) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream os;
                os << v;
                return os.str();
            } else {
                return v;
            }
        },
        c);
}

/**
 * 将 SqlType 转换为字符串
 * @param t SqlType 枚举值
 * @return 类型名称
 */
std::string sqlTypeToString(SqlType t) {
    switch (t) {
    case SqlType::Int:
        return "INT";
    case SqlType::Float:
        return "FLOAT";
    case SqlType::Text:
        return "TEXT";
    }
    return "TEXT";
}

bool cellViolatesNotNull(const Cell& c, SqlType t) {
    if (t != SqlType::Text) {
        return false;
    }
    const auto* s = std::get_if<std::string>(&c);
    return !s || s->empty();
}

/**
 * 将无类型数字字面量解析为 Cell
 */
std::optional<Cell> parseNumericLiteralCell(const std::string& raw) {
    std::string s = trim(raw);
    if (s.empty()) {
        return std::nullopt;
    }
    try {
        bool hasDot = s.find('.') != std::string::npos;
        if (hasDot) {
            size_t idx = 0;
            double v = std::stod(s, &idx);
            if (idx != s.size()) {
                return std::nullopt;
            }
            return Cell{v};
        }
        size_t idx = 0;
        std::int64_t v = static_cast<std::int64_t>(std::stoll(s, &idx, 10));
        if (idx != s.size()) {
            return std::nullopt;
        }
        return Cell{v};
    } catch (...) {
        return std::nullopt;
    }
}

bool cellTruthy(const Cell& c) {
    if (const auto* i = std::get_if<std::int64_t>(&c)) {
        return *i != 0;
    }
    if (const auto* d = std::get_if<double>(&c)) {
        return *d != 0.0 && !std::isnan(*d);
    }
    if (const auto* s = std::get_if<std::string>(&c)) {
        return !s->empty();
    }
    return false;
}

static std::optional<int> compareStrings(const Cell& a, const Cell& b) {
    const auto* sa = std::get_if<std::string>(&a);
    const auto* sb = std::get_if<std::string>(&b);
    if (!sa || !sb) {
        return std::nullopt;
    }
    if (*sa < *sb) {
        return -1;
    }
    if (*sa > *sb) {
        return 1;
    }
    return 0;
}

std::optional<int> cellCompareLoose(const Cell& a, const Cell& b) {
    if (auto cs = compareStrings(a, b)) {
        return cs;
    }
    if (std::holds_alternative<std::string>(a) || std::holds_alternative<std::string>(b)) {
        return std::nullopt;
    }
    double da = 0.0;
    double db = 0.0;
    if (const auto* ia = std::get_if<std::int64_t>(&a)) {
        da = static_cast<double>(*ia);
    } else if (const auto* fa = std::get_if<double>(&a)) {
        da = *fa;
    } else {
        return std::nullopt;
    }
    if (const auto* ib = std::get_if<std::int64_t>(&b)) {
        db = static_cast<double>(*ib);
    } else if (const auto* fb = std::get_if<double>(&b)) {
        db = *fb;
    } else {
        return std::nullopt;
    }
    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }
    return 0;
}

/**
 * 同类型相等比较
 * 用于 WHERE 条件的比较运算
 */
bool cellEqualsTyped(const Cell& a, const Cell& b, SqlType t) {
    if (t == SqlType::Int) {
        const auto* pa = std::get_if<std::int64_t>(&a);
        const auto* pb = std::get_if<std::int64_t>(&b);
        if (!pa || !pb) {
            return false;
        }
        return *pa == *pb;
    }
    if (t == SqlType::Float) {
        const auto* pa = std::get_if<double>(&a);
        const auto* pb = std::get_if<double>(&b);
        if (!pa || !pb) {
            return false;
        }
        return *pa == *pb || (std::isnan(*pa) && std::isnan(*pb));
    }
    const auto* pa = std::get_if<std::string>(&a);
    const auto* pb = std::get_if<std::string>(&b);
    if (!pa || !pb) {
        return false;
    }
    return *pa == *pb;
}
