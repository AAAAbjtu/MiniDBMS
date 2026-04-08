#include "Parser.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace
{

    std::string trim(const std::string &s)
    {
        size_t left = 0;
        while (left < s.size() && std::isspace(static_cast<unsigned char>(s[left])))
        {
            ++left;
        }

        size_t right = s.size();
        while (right > left && std::isspace(static_cast<unsigned char>(s[right - 1])))
        {
            --right;
        }

        return s.substr(left, right - left);
    }

    std::string stripQuotes(const std::string &s)
    {
        std::string t = trim(s);
        if (t.size() < 2)
        {
            return t;
        }

        char first = t.front();
        char last = t.back();
        if ((first == '\'' && last == '\'') || (first == '"' && last == '"') || (first == '`' && last == '`'))
        {
            return t.substr(1, t.size() - 2);
        }
        return t;
    }

    std::vector<std::string> splitCommaAware(const std::string &s)
    {
        std::vector<std::string> parts;
        std::string current;

        bool inSingleQuote = false;
        bool inDoubleQuote = false;

        for (char ch : s)
        {
            if (ch == '\'' && !inDoubleQuote)
            {
                inSingleQuote = !inSingleQuote;
                current.push_back(ch);
                continue;
            }
            if (ch == '"' && !inSingleQuote)
            {
                inDoubleQuote = !inDoubleQuote;
                current.push_back(ch);
                continue;
            }

            if (ch == ',' && !inSingleQuote && !inDoubleQuote)
            {
                parts.push_back(trim(current));
                current.clear();
                continue;
            }

            current.push_back(ch);
        }

        if (!current.empty() || !parts.empty())
        {
            parts.push_back(trim(current));
        }

        return parts;
    }

    bool parseAssignment(const std::string &clause, std::string &column, std::string &value)
    {
        bool inSingleQuote = false;
        bool inDoubleQuote = false;

        for (size_t i = 0; i < clause.size(); ++i)
        {
            char ch = clause[i];
            if (ch == '\'' && !inDoubleQuote)
            {
                inSingleQuote = !inSingleQuote;
                continue;
            }
            if (ch == '"' && !inSingleQuote)
            {
                inDoubleQuote = !inDoubleQuote;
                continue;
            }
            if (ch == '=' && !inSingleQuote && !inDoubleQuote)
            {
                column = stripQuotes(clause.substr(0, i));
                value = stripQuotes(clause.substr(i + 1));
                return !column.empty();
            }
        }

        return false;
    }

    bool parseOptionalWhere(const std::string &whereClause, Query &q)
    {
        if (whereClause.empty())
        {
            return true;
        }

        return parseAssignment(whereClause, q.whereColumn, q.whereValue);
    }

    std::vector<std::string> parseNameList(const std::string &s)
    {
        std::vector<std::string> result;
        auto raw = splitCommaAware(s);
        for (const auto &item : raw)
        {
            std::string v = stripQuotes(item);
            if (!v.empty())
            {
                result.push_back(v);
            }
        }
        return result;
    }

    std::vector<std::string> parseValueList(const std::string &s)
    {
        std::vector<std::string> result;
        auto raw = splitCommaAware(s);
        for (const auto &item : raw)
        {
            result.push_back(stripQuotes(item));
        }
        return result;
    }

} // namespace

Query Parser::parse(const std::string &sql) const
{
    Query q;

    std::string input = trim(sql);
    if (input.empty())
    {
        return q;
    }

    if (!input.empty() && input.back() == ';')
    {
        input.pop_back();
        input = trim(input);
    }

    std::smatch m;

    const std::regex useRe(R"(^USE\s+([A-Za-z_][A-Za-z0-9_]*)$)", std::regex::icase);
    const std::regex connectRe(R"(^CONNECT\s+([A-Za-z_][A-Za-z0-9_]*)$)", std::regex::icase);
    const std::regex showTablesRe(R"(^SHOW\s+TABLES$)", std::regex::icase);
    const std::regex descRe(R"(^DESC(?:RIBE)?\s+(?:TABLE\s+)?([A-Za-z_][A-Za-z0-9_]*)$)", std::regex::icase);
    const std::regex dropTableRe(R"(^DROP\s+TABLE\s+([A-Za-z_][A-Za-z0-9_]*)$)", std::regex::icase);
    const std::regex createTableRe(R"(^CREATE\s+TABLE\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)$)", std::regex::icase);
    const std::regex insertRe(R"(^INSERT\s+INTO\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\(([^)]*)\)\s*)?VALUES\s*\((.*)\)$)", std::regex::icase);
    const std::regex selectRe(R"(^SELECT\s+(.*?)\s+FROM\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+WHERE\s+(.+))?$)", std::regex::icase);
    const std::regex deleteRe(R"(^DELETE\s+FROM\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+WHERE\s+(.+))?$)", std::regex::icase);
    const std::regex updateRe(R"(^UPDATE\s+([A-Za-z_][A-Za-z0-9_]*)\s+SET\s+(.+?)(?:\s+WHERE\s+(.+))?$)", std::regex::icase);

    if (std::regex_match(input, m, useRe) || std::regex_match(input, m, connectRe))
    {
        q.type = CONNECT_DB;
        q.dbName = m[1].str();
        return q;
    }

    if (std::regex_match(input, showTablesRe))
    {
        q.type = SHOW_TABLES_Q;
        return q;
    }

    if (std::regex_match(input, m, descRe))
    {
        q.type = DESC_TABLE_Q;
        q.table = m[1].str();
        return q;
    }

    if (std::regex_match(input, m, dropTableRe))
    {
        q.type = DROP_TABLE_Q;
        q.table = m[1].str();
        return q;
    }

    if (std::regex_match(input, m, createTableRe))
    {
        q.type = CREATE_TABLE;
        q.table = m[1].str();
        q.columns = parseNameList(m[2].str());
        if (q.columns.empty())
        {
            q.type = UNKNOWN;
        }
        return q;
    }

    if (std::regex_match(input, m, insertRe))
    {
        q.type = INSERT;
        q.table = m[1].str();

        std::string columnsPart = trim(m[2].str());
        if (!columnsPart.empty())
        {
            q.columns = parseNameList(columnsPart);
        }

        q.values = parseValueList(m[3].str());
        if (q.values.empty())
        {
            q.type = UNKNOWN;
        }
        return q;
    }

    if (std::regex_match(input, m, selectRe))
    {
        q.type = SELECT;
        q.table = m[2].str();

        std::string selectPart = trim(m[1].str());
        if (selectPart == "*")
        {
            q.selectAll = true;
        }
        else
        {
            q.selectAll = false;
            q.columns = parseNameList(selectPart);
            if (q.columns.empty())
            {
                q.type = UNKNOWN;
                return q;
            }
        }

        if (!parseOptionalWhere(trim(m[3].str()), q))
        {
            q.type = UNKNOWN;
        }
        return q;
    }

    if (std::regex_match(input, m, deleteRe))
    {
        q.type = DELETE_Q;
        q.table = m[1].str();

        if (!parseOptionalWhere(trim(m[2].str()), q))
        {
            q.type = UNKNOWN;
        }
        return q;
    }

    if (std::regex_match(input, m, updateRe))
    {
        q.type = UPDATE_Q;
        q.table = m[1].str();

        if (!parseAssignment(trim(m[2].str()), q.updateColumn, q.updateValue))
        {
            q.type = UNKNOWN;
            return q;
        }

        if (!parseOptionalWhere(trim(m[3].str()), q))
        {
            q.type = UNKNOWN;
        }
        return q;
    }

    return q;
}
