#ifndef MINIDB_H
#define MINIDB_H

#include <string>

class MiniDB {
public:
    MiniDB();
    void execute(const std::string& sql);

};

#endif