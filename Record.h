#ifndef RECORD_H
#define RECORD_H

#include "Schema.h"
#include <vector>

/**
 * 记录结构
 * 表示表中的一行数据
 */
struct Record {
    std::vector<Cell> cells;  // 单元格数组，每个单元格对应一列
};

#endif
