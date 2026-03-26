#include "MiniDB.h"
#include "Executor.h"
#include "Parser.h"

Parser parser;
Executor executor;

MiniDB::MiniDB() {}

void MiniDB::execute(const std::string& sql) {
    Query q = parser.parse(sql);
    executor.execute(q);
}