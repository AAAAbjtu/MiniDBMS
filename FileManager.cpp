/**
 * FileManager 模块实现
 * 负责数据库和表的文件读写操作
 */

#include "FileManager.h"
#include "Schema.h"
#include <algorithm>
#include <cerrno>
#include <variant>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <direct.h>
#include <fstream>
#include <io.h>
#include <sstream>
#include <vector>

namespace {

/**
 * 递归删除目录
 * @param path 目录路径
 * @return 是否成功
 */
bool removeDirectoryRecursive(const std::string& path) {
    std::string pattern = path + "/*";
    _finddata_t fileInfo;
    intptr_t handle = _findfirst(pattern.c_str(), &fileInfo);
    if (handle == -1) {
        return false;
    }

    bool success = true;
    do {
        std::string name = fileInfo.name;
        if (name == "." || name == "..") {
            continue;
        }
        std::string fullPath = path + "/" + name;
        if (fileInfo.attrib & _A_SUBDIR) {
            if (!removeDirectoryRecursive(fullPath)) {
                success = false;
            }
        } else {
            if (std::remove(fullPath.c_str()) != 0) {
                success = false;
            }
        }
    } while (_findnext(handle, &fileInfo) == 0);

    _findclose(handle);

    if (success && _rmdir(path.c_str()) != 0) {
        return false;
    }
    return success;
}

/**
 * 获取数据库目录路径
 * @param dbName 数据库名
 * @return 目录路径，如 "data_demo"
 */
std::string dbDir(const std::string& dbName) {
    return "data_" + dbName;
}

/**
 * 获取表文件路径
 * @param dbName 数据库名
 * @param tableName 表名
 * @return 表文件完整路径
 */
std::string tableFile(const std::string& dbName, const std::string& tableName) {
    return dbDir(dbName) + "/" + tableName + ".tbl";
}

/**
 * V1 格式魔数：MINIDB + 0x01
 */
static const unsigned char kMagicV1[8] = {'M', 'I', 'N', 'I', 'D', 'B', 0x01, 0x00};
/**
 * V2 格式魔数：MINIDB + 0x02
 */
static const unsigned char kMagicV2[8] = {'M', 'I', 'N', 'I', 'D', 'B', 0x02, 0x00};
/**
 * V3 格式魔数：MINIDB + 0x03（列定义含 NOT NULL / PRIMARY KEY 标志）
 */
static const unsigned char kMagicV3[8] = {'M', 'I', 'N', 'I', 'D', 'B', 0x03, 0x00};
/**
 * V4：在 V3 基础上增加 UNIQUE / CHECK / REFERENCES 元数据
 */
static const unsigned char kMagicV4[8] = {'M', 'I', 'N', 'I', 'D', 'B', 0x04, 0x00};

/** bit0 NOT NULL, bit1 PRIMARY KEY, bit2 UNIQUE, bit8 FK 载荷, bit16 CHECK 载荷 */
static std::uint8_t columnFlagsByteV4(const ColumnDef& c) {
    std::uint8_t f = 0;
    if (c.notNull) {
        f |= 1u;
    }
    if (c.primaryKey) {
        f |= 2u;
    }
    if (c.unique) {
        f |= 4u;
    }
    if (!c.fkRefTable.empty()) {
        f |= 8u;
    }
    if (!c.checkExpr.empty()) {
        f |= 16u;
    }
    return f;
}

static void applyColumnFlagsV3(ColumnDef& cd, std::uint8_t f) {
    cd.notNull = (f & 1u) != 0;
    cd.primaryKey = (f & 2u) != 0;
}

static void applyColumnFlagsV4(ColumnDef& cd, std::uint8_t f) {
    cd.notNull = (f & 1u) != 0;
    cd.primaryKey = (f & 2u) != 0;
    cd.unique = (f & 4u) != 0;
}

/**
 * 写入 32 位无符号整数（小端序）
 */
static void writeU32(std::ostream& o, std::uint32_t v) {
    o.put(static_cast<char>(v & 0xFF));
    o.put(static_cast<char>((v >> 8) & 0xFF));
    o.put(static_cast<char>((v >> 16) & 0xFF));
    o.put(static_cast<char>((v >> 24) & 0xFF));
}

/**
 * 读取 32 位无符号整数（小端序）
 */
static std::uint32_t readU32(std::istream& in) {
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if (in.gcount() != 4) {
        return 0;
    }
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}

/**
 * 写入字符串（长度前缀 + 内容）
 */
static void writeString(std::ostream& o, const std::string& s) {
    std::uint32_t len = static_cast<std::uint32_t>(s.size());
    writeU32(o, len);
    if (len > 0) {
        o.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
}

/**
 * 读取字符串（长度前缀 + 内容）
 */
static std::string readString(std::istream& in) {
    std::uint32_t len = readU32(in);
    if (len == 0) {
        return "";
    }
    std::string s(len, '\0');
    in.read(&s[0], static_cast<std::streamsize>(len));
    if (static_cast<std::uint32_t>(in.gcount()) != len) {
        return "";
    }
    return s;
}

/**
 * 写入 64 位有符号整数
 */
static void writeI64(std::ostream& o, std::int64_t v) {
    for (int i = 0; i < 8; ++i) {
        o.put(static_cast<char>((static_cast<std::uint64_t>(v) >> (8 * i)) & 0xFF));
    }
}

/**
 * 读取 64 位有符号整数
 */
static std::int64_t readI64(std::istream& in) {
    unsigned char b[8];
    in.read(reinterpret_cast<char*>(b), 8);
    if (in.gcount() != 8) {
        return 0;
    }
    std::uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= static_cast<std::uint64_t>(b[i]) << (8 * i);
    }
    return static_cast<std::int64_t>(u);
}

/**
 * 写入 64 位浮点数
 */
static void writeF64(std::ostream& o, double d) {
    static_assert(sizeof(double) == 8, "");
    unsigned char b[8];
    std::memcpy(b, &d, 8);
    o.write(reinterpret_cast<const char*>(b), 8);
}

/**
 * 读取 64 位浮点数
 */
static double readF64(std::istream& in) {
    unsigned char b[8];
    in.read(reinterpret_cast<char*>(b), 8);
    if (in.gcount() != 8) {
        return 0;
    }
    double d;
    std::memcpy(&d, b, 8);
    return d;
}

/**
 * SqlType 转类型标签
 * Int=0, Float=1, Text=2
 */
static std::uint8_t typeTag(SqlType t) {
    switch (t) {
    case SqlType::Int:
        return 0;
    case SqlType::Float:
        return 1;
    case SqlType::Text:
        return 2;
    }
    return 2;
}

/**
 * 类型标签转 SqlType
 */
static SqlType tagType(std::uint8_t tag) {
    switch (tag) {
    case 0:
        return SqlType::Int;
    case 1:
        return SqlType::Float;
    default:
        return SqlType::Text;
    }
}

/**
 * 写入单元格到文件
 */
static void writeCell(std::ostream& o, const Cell& c, SqlType t) {
    if (t == SqlType::Int) {
        std::int64_t v = std::get<std::int64_t>(c);
        writeI64(o, v);
    } else if (t == SqlType::Float) {
        double v = std::get<double>(c);
        writeF64(o, v);
    } else {
        const std::string& s = std::get<std::string>(c);
        writeString(o, s);
    }
}

/**
 * 从文件读取单元格
 */
static Cell readCell(std::istream& in, SqlType t) {
    if (t == SqlType::Int) {
        return Cell{readI64(in)};
    }
    if (t == SqlType::Float) {
        return Cell{readF64(in)};
    }
    return Cell{readString(in)};
}

/**
 * 加载旧版文本格式表
 * 格式：#schema,col1,col2,... 后跟 CSV 数据
 */
static Table loadLegacyText(std::istream& file, const std::string& tableName) {
    Table t(tableName);
    std::string line;
    bool firstLine = true;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() && firstLine) {
            continue;
        }
        std::stringstream ss(line);
        std::string v;
        std::vector<std::string> parts;
        while (std::getline(ss, v, ',')) {
            parts.push_back(v);
        }
        if (firstLine) {
            firstLine = false;
            if (!parts.empty() && parts[0] == "#schema") {
                std::vector<ColumnDef> schema;
                for (size_t i = 1; i < parts.size(); ++i) {
                    ColumnDef cd;
                    cd.name = parts[i];
                    cd.type = SqlType::Text;
                    schema.push_back(std::move(cd));
                }
                t.setSchema(schema);
                continue;
            }
        }
        Record r;
        for (const auto& p : parts) {
            r.cells.push_back(Cell{p});
        }
        t.insert(r);
    }
    return t;
}

/**
 * 加载 V1 二进制格式表
 * 格式：魔数 + 列数 + 列名列表 + 行数 + 数据（均为字符串）
 */
static Table loadBinaryV1(std::istream& in, const std::string& tableName) {
    Table t(tableName);
    std::uint32_t ncols = readU32(in);
    if (ncols > 65536) {
        return t;
    }
    std::vector<ColumnDef> schema;
    schema.reserve(ncols);
    for (std::uint32_t i = 0; i < ncols; ++i) {
        ColumnDef cd;
        cd.name = readString(in);
        cd.type = SqlType::Text;
        schema.push_back(std::move(cd));
    }
    t.setSchema(schema);
    std::uint32_t nrows = readU32(in);
    for (std::uint32_t r = 0; r < nrows; ++r) {
        Record rec;
        for (std::uint32_t i = 0; i < ncols; ++i) {
            rec.cells.push_back(Cell{readString(in)});
        }
        t.insert(rec);
    }
    return t;
}

/**
 * 加载 V2 二进制格式表
 * 格式：魔数 + 列数 + (列名+类型标签)列表 + 行数 + 数据
 */
static Table loadBinaryV2(std::istream& in, const std::string& tableName) {
    Table t(tableName);
    std::uint32_t ncols = readU32(in);
    if (ncols > 65536) {
        return t;
    }
    std::vector<ColumnDef> schema;
    schema.reserve(ncols);
    for (std::uint32_t i = 0; i < ncols; ++i) {
        ColumnDef cd;
        cd.name = readString(in);
        unsigned char tag = 0;
        in.read(reinterpret_cast<char*>(&tag), 1);
        if (in.gcount() != 1) {
            return t;
        }
        cd.type = tagType(tag);
        schema.push_back(std::move(cd));
    }
    t.setSchema(schema);
    std::uint32_t nrows = readU32(in);
    const auto& sch = t.getSchema();
    for (std::uint32_t r = 0; r < nrows; ++r) {
        Record rec;
        for (std::uint32_t i = 0; i < ncols; ++i) {
            rec.cells.push_back(readCell(in, sch[i].type));
        }
        t.insert(rec);
    }
    return t;
}

/**
 * 加载 V3 二进制格式表
 * 格式：魔数 + 列数 + (列名+类型标签+flags)列表 + 行数 + 数据
 */
static Table loadBinaryV3(std::istream& in, const std::string& tableName) {
    Table t(tableName);
    std::uint32_t ncols = readU32(in);
    if (ncols > 65536) {
        return t;
    }
    std::vector<ColumnDef> schema;
    schema.reserve(ncols);
    for (std::uint32_t i = 0; i < ncols; ++i) {
        ColumnDef cd;
        cd.name = readString(in);
        unsigned char tag = 0;
        in.read(reinterpret_cast<char*>(&tag), 1);
        if (in.gcount() != 1) {
            return t;
        }
        cd.type = tagType(tag);
        unsigned char flags = 0;
        in.read(reinterpret_cast<char*>(&flags), 1);
        if (in.gcount() != 1) {
            return t;
        }
        applyColumnFlagsV3(cd, static_cast<std::uint8_t>(flags & 3u));
        schema.push_back(std::move(cd));
    }
    t.setSchema(schema);
    std::uint32_t nrows = readU32(in);
    const auto& sch = t.getSchema();
    for (std::uint32_t r = 0; r < nrows; ++r) {
        Record rec;
        for (std::uint32_t i = 0; i < ncols; ++i) {
            rec.cells.push_back(readCell(in, sch[i].type));
        }
        t.insert(rec);
    }
    return t;
}

/**
 * 加载 V4 二进制格式表
 */
static Table loadBinaryV4(std::istream& in, const std::string& tableName) {
    Table t(tableName);
    std::uint32_t ncols = readU32(in);
    if (ncols > 65536) {
        return t;
    }
    std::vector<ColumnDef> schema;
    schema.reserve(ncols);
    for (std::uint32_t i = 0; i < ncols; ++i) {
        ColumnDef cd;
        cd.name = readString(in);
        unsigned char tag = 0;
        in.read(reinterpret_cast<char*>(&tag), 1);
        if (in.gcount() != 1) {
            return t;
        }
        cd.type = tagType(tag);
        unsigned char flags = 0;
        in.read(reinterpret_cast<char*>(&flags), 1);
        if (in.gcount() != 1) {
            return t;
        }
        applyColumnFlagsV4(cd, flags);
        if ((flags & 8u) != 0) {
            cd.fkRefTable = readString(in);
            cd.fkRefCol = readString(in);
        }
        if ((flags & 16u) != 0) {
            cd.checkExpr = readString(in);
        }
        schema.push_back(std::move(cd));
    }
    t.setSchema(schema);
    std::uint32_t nrows = readU32(in);
    const auto& sch = t.getSchema();
    for (std::uint32_t r = 0; r < nrows; ++r) {
        Record rec;
        for (std::uint32_t i = 0; i < ncols; ++i) {
            rec.cells.push_back(readCell(in, sch[i].type));
        }
        t.insert(rec);
    }
    return t;
}

} // namespace

/**
 * 确保数据库目录存在，不存在则创建
 */
bool FileManager::ensureDatabase(const std::string& dbName) {
    if (dbName.empty()) {
        return false;
    }
    std::string dir = dbDir(dbName);
    int rc = _mkdir(dir.c_str());
    return rc == 0 || errno == EEXIST;
}

/**
 * 删除数据库目录
 */
bool FileManager::dropDatabase(const std::string& dbName) {
    if (dbName.empty()) {
        return false;
    }
    std::string dir = dbDir(dbName);
    return removeDirectoryRecursive(dir);
}

/**
 * 列出所有数据库
 */
std::vector<std::string> FileManager::listDatabases() {
    std::vector<std::string> result;
    std::string pattern = "data_*";
    _finddata_t fileInfo;
    intptr_t handle = _findfirst(pattern.c_str(), &fileInfo);
    if (handle == -1) {
        return result;
    }

    do {
        std::string name = fileInfo.name;
        if (name.size() > 5 && name.substr(0, 5) == "data_") {
            result.push_back(name.substr(5));
        }
    } while (_findnext(handle, &fileInfo) == 0);

    _findclose(handle);
    return result;
}

/**
 * 检查表是否存在
 */
bool FileManager::tableExists(const std::string& dbName, const std::string& tableName) {
    std::ifstream f(tableFile(dbName, tableName));
    return f.good();
}

/**
 * 列出数据库中的所有表
 */
std::vector<std::string> FileManager::listTables(const std::string& dbName) {
    std::vector<std::string> result;
    std::string pattern = dbDir(dbName) + "/*.tbl";
    _finddata_t fileInfo;
    intptr_t handle = _findfirst(pattern.c_str(), &fileInfo);
    if (handle == -1) {
        return result;
    }

    do {
        std::string name = fileInfo.name;
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".tbl") {
            result.push_back(name.substr(0, name.size() - 4));
        }
    } while (_findnext(handle, &fileInfo) == 0);

    _findclose(handle);
    return result;
}

/**
 * 删除表文件
 */
bool FileManager::dropTable(const std::string& dbName, const std::string& tableName) {
    std::string path = tableFile(dbName, tableName);
    return std::remove(path.c_str()) == 0;
}

/**
 * 保存表到文件（V2 二进制格式）
 */
void FileManager::save(const std::string& dbName, const Table& table) {
    ensureDatabase(dbName);
    std::ofstream file(tableFile(dbName, table.getName()), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(kMagicV4), 8);
    const auto& sch = table.getSchema();
    std::uint32_t ncols = static_cast<std::uint32_t>(sch.size());
    writeU32(file, ncols);
    for (const auto& c : sch) {
        writeString(file, c.name);
        unsigned char tag = typeTag(c.type);
        file.put(static_cast<char>(tag));
        std::uint8_t fb = columnFlagsByteV4(c);
        file.put(static_cast<char>(fb));
        if (!c.fkRefTable.empty()) {
            writeString(file, c.fkRefTable);
            writeString(file, c.fkRefCol);
        }
        if (!c.checkExpr.empty()) {
            writeString(file, c.checkExpr);
        }
    }
    const auto& recs = table.getRecords();
    writeU32(file, static_cast<std::uint32_t>(recs.size()));
    for (const auto& r : recs) {
        for (std::uint32_t i = 0; i < ncols; ++i) {
            Cell cell;
            if (i < r.cells.size()) {
                cell = r.cells[i];
            } else {
                switch (sch[i].type) {
                case SqlType::Int:
                    cell = Cell{static_cast<std::int64_t>(0)};
                    break;
                case SqlType::Float:
                    cell = Cell{0.0};
                    break;
                case SqlType::Text:
                    cell = Cell{std::string{}};
                    break;
                }
            }
            if (sch[i].type == SqlType::Text && !std::holds_alternative<std::string>(cell)) {
                cell = Cell{cellToString(cell)};
            }
            writeCell(file, cell, sch[i].type);
        }
    }
}

/**
 * 从文件加载表（自动识别格式）
 */
Table FileManager::load(const std::string& dbName, const std::string& tableName) {
    std::string path = tableFile(dbName, tableName);
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return Table(tableName);
    }

    char head[8];
    file.read(head, 8);
    if (file.gcount() == 8) {
        if (std::memcmp(head, kMagicV4, 8) == 0) {
            return loadBinaryV4(file, tableName);
        }
        if (std::memcmp(head, kMagicV3, 8) == 0) {
            return loadBinaryV3(file, tableName);
        }
        if (std::memcmp(head, kMagicV2, 8) == 0) {
            return loadBinaryV2(file, tableName);
        }
        if (std::memcmp(head, kMagicV1, 8) == 0) {
            return loadBinaryV1(file, tableName);
        }
    }

    file.clear();
    file.seekg(0);
    return loadLegacyText(file, tableName);
}

/**
 * 用户文件路径
 */
static std::string usersFile() {
    return "system/users.dat";
}

/**
 * 系统目录路径
 */
static std::string systemDir() {
    return "system";
}

/**
 * 确保系统目录存在
 */
static bool ensureSystemDir() {
    int rc = _mkdir(systemDir().c_str());
    return rc == 0 || errno == EEXIST;
}

/**
 * 简单的 SHA-256 哈希实现（用于密码加密）
 * 这是一个简化实现，用于演示目的
 */
static std::string sha256(const std::string& input) {
    std::uint32_t hash[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char byte = static_cast<unsigned char>(input[i]);
        hash[0] ^= ((byte * 0x9e3779b9) + 0x9e3779b9 + (i << 6) + (i >> 2));

        for (int j = 0; j < 8; ++j) {
            hash[j] = (hash[j] ^ (hash[(j + 1) % 8] << 5) ^ (hash[(j + 1) % 8] >> 2)) + 0x9e3779b9;
            hash[j] ^= (hash[(j + 3) % 8] << 3) ^ (hash[(j + 3) % 8] >> 5);
        }
    }

    char result[65];
    for (int i = 0; i < 8; ++i) {
        snprintf(result + i * 8, 9, "%08x", hash[i]);
    }
    result[64] = '\0';
    return std::string(result);
}

/**
 * 加载所有用户
 */
static std::vector<UserInfo> loadUsers() {
    std::vector<UserInfo> users;
    if (!ensureSystemDir()) {
        return users;
    }

    std::ifstream file(usersFile(), std::ios::binary);
    if (!file.good()) {
        return users;
    }

    std::uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&count), 4);
    if (file.gcount() != 4) {
        return users;
    }

    for (std::uint32_t i = 0; i < count; ++i) {
        UserInfo u;
        std::uint32_t nameLen = 0;
        file.read(reinterpret_cast<char*>(&nameLen), 4);
        if (file.gcount() != 4) break;

        u.username.resize(nameLen);
        file.read(&u.username[0], nameLen);
        if (file.gcount() != static_cast<std::streamsize>(nameLen)) break;

        std::uint32_t hashLen = 0;
        file.read(reinterpret_cast<char*>(&hashLen), 4);
        if (file.gcount() != 4) break;

        u.passwordHash.resize(hashLen);
        file.read(&u.passwordHash[0], hashLen);
        if (file.gcount() != static_cast<std::streamsize>(hashLen)) break;

        // 兼容性处理：尝试读取角色，如果失败则设为默认值
        if (file.read(reinterpret_cast<char*>(&u.role), 4)) {
            // 成功读取角色
        } else {
            // 旧格式没有角色字段，设为默认 USER 角色
            u.role = 2;
            users.push_back(u);
            continue;
        }

        // 兼容性处理：尝试读取权限列表
        std::uint32_t privCount = 0;
        if (file.read(reinterpret_cast<char*>(&privCount), 4)) {
            for (std::uint32_t j = 0; j < privCount; ++j) {
                int priv = 0;
                if (file.read(reinterpret_cast<char*>(&priv), 4)) {
                    u.privileges.push_back(priv);
                }
            }
        }

        users.push_back(u);
    }

    return users;
}

/**
 * 保存所有用户
 */
static bool saveUsers(const std::vector<UserInfo>& users) {
    if (!ensureSystemDir()) {
        return false;
    }

    std::ofstream file(usersFile(), std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        return false;
    }

    std::uint32_t count = static_cast<std::uint32_t>(users.size());
    file.write(reinterpret_cast<const char*>(&count), 4);

    for (const auto& u : users) {
        std::uint32_t nameLen = static_cast<std::uint32_t>(u.username.size());
        file.write(reinterpret_cast<const char*>(&nameLen), 4);
        file.write(u.username.data(), nameLen);

        std::uint32_t hashLen = static_cast<std::uint32_t>(u.passwordHash.size());
        file.write(reinterpret_cast<const char*>(&hashLen), 4);
        file.write(u.passwordHash.data(), hashLen);

        file.write(reinterpret_cast<const char*>(&u.role), 4);

        std::uint32_t privCount = static_cast<std::uint32_t>(u.privileges.size());
        file.write(reinterpret_cast<const char*>(&privCount), 4);
        for (int priv : u.privileges) {
            file.write(reinterpret_cast<const char*>(&priv), 4);
        }
    }

    return file.good();
}

/**
 * 创建用户
 */
bool FileManager::createUser(const std::string& username, const std::string& password, int role) {
    if (username.empty()) {
        return false;
    }

    auto users = loadUsers();

    for (const auto& u : users) {
        if (u.username == username) {
            return false;
        }
    }

    UserInfo newUser;
    newUser.username = username;
    newUser.passwordHash = sha256(password);
    newUser.role = role;
    users.push_back(newUser);

    return saveUsers(users);
}

/**
 * 删除用户
 */
bool FileManager::dropUser(const std::string& username) {
    auto users = loadUsers();
    auto it = std::remove_if(users.begin(), users.end(),
                              [&username](const UserInfo& u) { return u.username == username; });

    if (it == users.end()) {
        return false;
    }

    users.erase(it, users.end());
    return saveUsers(users);
}

/**
 * 列出所有用户
 */
std::vector<UserInfo> FileManager::listUsers() {
    return loadUsers();
}

/**
 * 验证用户登录
 */
bool FileManager::validateUser(const std::string& username, const std::string& password) {
    auto users = loadUsers();
    std::string hash = sha256(password);

    for (const auto& u : users) {
        if (u.username == username && u.passwordHash == hash) {
            return true;
        }
    }
    return false;
}

/**
 * 获取用户信息
 */
UserInfo FileManager::getUser(const std::string& username) {
    auto users = loadUsers();
    for (const auto& u : users) {
        if (u.username == username) {
            return u;
        }
    }
    return UserInfo{};
}

/**
 * 更新用户信息
 */
bool FileManager::updateUser(const UserInfo& user) {
    auto users = loadUsers();
    for (auto& u : users) {
        if (u.username == user.username) {
            u = user;
            return saveUsers(users);
        }
    }
    return false;
}

/**
 * 授予用户权限
 */
bool FileManager::grantPrivilege(const std::string& username, int privilege) {
    auto users = loadUsers();
    for (auto& u : users) {
        if (u.username == username) {
            for (int p : u.privileges) {
                if (p == privilege) {
                    return true;
                }
            }
            u.privileges.push_back(privilege);
            return saveUsers(users);
        }
    }
    return false;
}

/**
 * 撤销用户权限
 */
bool FileManager::revokePrivilege(const std::string& username, int privilege) {
    auto users = loadUsers();
    for (auto& u : users) {
        if (u.username == username) {
            auto it = std::remove(u.privileges.begin(), u.privileges.end(), privilege);
            u.privileges.erase(it, u.privileges.end());
            return saveUsers(users);
        }
    }
    return false;
}

/**
 * 检查用户是否有指定权限
 */
bool FileManager::hasPrivilege(const std::string& username, int privilege) {
    auto users = loadUsers();
    for (const auto& u : users) {
        if (u.username == username) {
            // ADMIN (role=0) 拥有所有权限
            if (u.role == 0) return true;
            // DBA (role=1) 拥有数据库和表管理权限（privilege >= 10）
            if (u.role == 1 && privilege >= 10) return true;
            // USER (role=2) 拥有基本 CRUD 权限（privilege >= 16，即 INSERT/SELECT/UPDATE/DELETE）
            if (u.role == 2 && privilege >= 16) return true;
            // 检查额外授予的权限
            for (int p : u.privileges) {
                if (p == privilege) return true;
            }
        }
    }
    return false;
}

/**
 * 审计日志文件路径
 */
static std::string auditFile() {
    return "system/audit.log";
}

/**
 * 添加审计日志
 */
void FileManager::addAuditLog(const std::string& username, const std::string& action, const std::string& target, const std::string& result) {
    if (!ensureSystemDir()) {
        return;
    }

    std::ofstream file(auditFile(), std::ios::app);
    if (!file.good()) {
        return;
    }

    time_t now = time(nullptr);
    char timestamp[32];
    struct tm tmBuf;
#ifdef _MSC_VER
    localtime_s(&tmBuf, &now);
#else
    tmBuf = *localtime(&now);
#endif
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tmBuf);

    file << "[" << timestamp << "] "
         << "user=" << username << " "
         << "action=" << action << " "
         << "target=" << target << " "
         << "result=" << result << "\n";
}

/**
 * 获取审计日志
 */
std::vector<AuditLogEntry> FileManager::getAuditLogs() {
    std::vector<AuditLogEntry> logs;
    if (!ensureSystemDir()) {
        return logs;
    }

    std::ifstream file(auditFile());
    if (!file.good()) {
        return logs;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        AuditLogEntry entry;
        entry.timestamp = "";
        entry.username = "";
        entry.action = "";
        entry.target = "";
        entry.result = "";

        size_t pos = 0;
        if (line[0] == '[') {
            size_t closeBracket = line.find(']');
            if (closeBracket != std::string::npos) {
                entry.timestamp = line.substr(1, closeBracket - 1);
                pos = closeBracket + 2;
            }
        }

        while (pos < line.size()) {
            size_t eqPos = line.find('=', pos);
            if (eqPos == std::string::npos) break;
            size_t spacePos = line.find(' ', eqPos + 1);
            if (spacePos == std::string::npos) spacePos = line.size();

            std::string key = line.substr(pos, eqPos - pos);
            std::string value = line.substr(eqPos + 1, spacePos - eqPos - 1);

            if (key == "user") entry.username = value;
            else if (key == "action") entry.action = value;
            else if (key == "target") entry.target = value;
            else if (key == "result") entry.result = value;

            pos = spacePos + 1;
        }

        if (!entry.action.empty()) {
            logs.push_back(entry);
        }
    }

    return logs;
}

std::vector<IncomingForeignKey> FileManager::listIncomingForeignKeys(const std::string& dbName,
                                                                     const std::string& parentTable) {
    std::vector<IncomingForeignKey> out;
    for (const std::string& tn : listTables(dbName)) {
        Table t = load(dbName, tn);
        for (const auto& col : t.getSchema()) {
            if (!col.fkRefTable.empty() && col.fkRefTable == parentTable) {
                IncomingForeignKey e;
                e.childTable = tn;
                e.childFkColumn = col.name;
                e.parentReferencedColumn = col.fkRefCol;
                out.push_back(std::move(e));
            }
        }
    }
    return out;
}

bool FileManager::isParentColumnReferencedByFk(const std::string& dbName, const std::string& parentTable,
                                                const std::string& parentColumn) {
    for (const auto& e : listIncomingForeignKeys(dbName, parentTable)) {
        if (e.parentReferencedColumn == parentColumn) {
            return true;
        }
    }
    return false;
}

bool FileManager::isTableReferencedByForeignKeys(const std::string& dbName, const std::string& parentTable) {
    return !listIncomingForeignKeys(dbName, parentTable).empty();
}
