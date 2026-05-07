#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "Table.h"
#include <string>
#include <vector>

/**
 * 用户信息结构体
 */
struct UserInfo {
    std::string username;      // 用户名
    std::string passwordHash;  // 密码哈希（SHA-256）
    int role;                  // 角色类型 (0=USER, 1=DBA, 2=ADMIN)
    std::vector<int> privileges;  // 额外权限列表
};

/**
 * 审计日志结构体
 */
struct AuditLogEntry {
    std::string timestamp;  // 时间戳
    std::string username;   // 用户名
    std::string action;     // 操作类型
    std::string target;     // 操作目标
    std::string result;     // 操作结果
};

/**
 * 文件管理器类
 * 负责数据库和表的文件操作
 */
class FileManager {
public:
    /**
     * 确保数据库目录存在
     * 如果不存在则创建
     * @param dbName 数据库名称
     * @return 是否成功
     */
    static bool ensureDatabase(const std::string& dbName);

    /**
     * 删除数据库
     * 递归删除数据库目录及其所有表文件
     * @param dbName 数据库名称
     * @return 是否成功
     */
    static bool dropDatabase(const std::string& dbName);

    /**
     * 列出所有数据库
     * @return 数据库名称列表
     */
    static std::vector<std::string> listDatabases();

    /**
     * 检查表是否存在
     * @param dbName 数据库名称
     * @param tableName 表名
     * @return 是否存在
     */
    static bool tableExists(const std::string& dbName, const std::string& tableName);

    /**
     * 列出数据库中的所有表
     * @param dbName 数据库名称
     * @return 表名列表
     */
    static std::vector<std::string> listTables(const std::string& dbName);

    /**
     * 删除表
     * @param dbName 数据库名称
     * @param tableName 表名
     * @return 是否成功
     */
    static bool dropTable(const std::string& dbName, const std::string& tableName);

    /**
     * 保存表到文件
     * @param dbName 数据库名称
     * @param table 表对象
     */
    static void save(const std::string& dbName, const Table& table);

    /**
     * 从文件加载表
     * @param dbName 数据库名称
     * @param tableName 表名
     * @return 表对象
     */
    static Table load(const std::string& dbName, const std::string& tableName);

    /**
     * 创建用户
     * @param username 用户名
     * @param password 密码（明文）
     * @param role 角色类型
     * @return 是否成功
     */
    static bool createUser(const std::string& username, const std::string& password, int role = 0);

    /**
     * 删除用户
     * @param username 用户名
     * @return 是否成功
     */
    static bool dropUser(const std::string& username);

    /**
     * 列出所有用户
     * @return 用户信息列表
     */
    static std::vector<UserInfo> listUsers();

    /**
     * 验证用户登录
     * @param username 用户名
     * @param password 密码（明文）
     * @return 是否验证成功
     */
    static bool validateUser(const std::string& username, const std::string& password);

    /**
     * 获取用户信息
     * @param username 用户名
     * @return 用户信息（如果不存在返回空对象）
     */
    static UserInfo getUser(const std::string& username);

    /**
     * 更新用户信息
     * @param user 用户信息
     * @return 是否成功
     */
    static bool updateUser(const UserInfo& user);

    /**
     * 授予用户权限
     * @param username 用户名
     * @param privilege 权限类型
     * @return 是否成功
     */
    static bool grantPrivilege(const std::string& username, int privilege);

    /**
     * 撤销用户权限
     * @param username 用户名
     * @param privilege 权限类型
     * @return 是否成功
     */
    static bool revokePrivilege(const std::string& username, int privilege);

    /**
     * 检查用户是否有指定权限
     * @param username 用户名
     * @param privilege 权限类型
     * @return 是否有权限
     */
    static bool hasPrivilege(const std::string& username, int privilege);

    /**
     * 添加审计日志
     * @param username 用户名
     * @param action 操作类型
     * @param target 操作目标
     * @param result 操作结果
     */
    static void addAuditLog(const std::string& username, const std::string& action, const std::string& target, const std::string& result);

    /**
     * 获取审计日志
     * @return 审计日志列表
     */
    static std::vector<AuditLogEntry> getAuditLogs();
};

#endif