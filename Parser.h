#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>

enum QueryType
{
    UNKNOWN,
    CONNECT_DB,
    SHOW_TABLES_Q,
    DESC_TABLE_Q,
    DROP_TABLE_Q,
    CREATE_TABLE,
    INSERT,
    SELECT,
    DELETE_Q,
    UPDATE_Q
};

struct Query
{
    QueryType type = UNKNOWN;

    std::string dbName;
    std::string table;

    std::vector<std::string> columns;
    std::vector<std::string> values;

    std::string whereColumn;
    std::string whereValue;

    bool selectAll = true;

    std::string updateColumn;
    std::string updateValue;
};

class Parser
{
public:
    Query parse(const std::string &sql) const;
};

#endif