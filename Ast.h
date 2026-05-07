#ifndef AST_H
#define AST_H

#include "Schema.h"
#include <string>
#include <variant>
#include <vector>

/**
 * 抽象语法树（AST）定义
 * 用于表示解析后的 SQL 语句结构
 */
namespace ast {

/**
 * 权限类型枚举
 */
enum class Privilege {
    CREATE_USER,   // 创建用户
    DROP_USER,     // 删除用户
    SHOW_USERS,    // 查看用户
    CREATE_ROLE,   // 创建角色
    DROP_ROLE,     // 删除角色
    GRANT,         // 授予权限
    REVOKE,        // 撤销权限
    SHOW_GRANTS,   // 查看权限
    SHOW_AUDIT,    // 查看审计日志
    CREATE_DB,     // 创建数据库
    DROP_DB,       // 删除数据库
    SHOW_DATABASES, // 显示数据库
    CONNECT,       // 连接数据库
    CREATE_TABLE,  // 创建表
    DROP_TABLE,    // 删除表
    ALTER_TABLE,   // 修改表
    SHOW_TABLES,   // 显示表
    DESCRIBE,      // 查看表结构
    INSERT,        // 插入数据
    SELECT,        // 查询数据
    UPDATE,        // 更新数据
    DELETE         // 删除数据
};

/**
 * 角色类型枚举
 */
enum class RoleType {
    ADMIN,  // 管理员：拥有所有权限
    DBA,    // 数据库管理员：管理数据库和表
    USER    // 普通用户：基本 CRUD
};

/**
 * 连接数据库语句
 * 例如：CONNECT demo;
 */
struct ConnectStmt {
    std::string database;  // 数据库名称
};

/**
 * 创建数据库语句
 * 例如：CREATE DATABASE test;
 */
struct CreateDatabaseStmt {
    std::string database;  // 数据库名称
};

/**
 * 删除数据库语句
 * 例如：DROP DATABASE test;
 */
struct DropDatabaseStmt {
    std::string database;  // 数据库名称
};

/**
 * 显示所有数据库语句
 * 例如：SHOW DATABASES;
 */
struct ShowDatabasesStmt {
};

/**
 * 显示所有表语句
 * 例如：SHOW TABLES;
 */
struct ShowTablesStmt {
};

/**
 * 显示表结构语句
 * 例如：DESC student;
 */
struct DescribeStmt {
    std::string table;  // 表名
};

/**
 * 删除表语句
 * 例如：DROP TABLE student;
 */
struct DropTableStmt {
    std::string table;  // 表名
};

/**
 * ALTER TABLE 操作类型
 */
enum class AlterOperation {
    AddColumn,      // 添加列
    DropColumn,     // 删除列
    ModifyColumn    // 修改列类型
};

/**
 * 修改表语句
 * 例如：ALTER TABLE student ADD age INT;
 */
struct AlterTableStmt {
    std::string table;      // 表名
    AlterOperation op;      // 操作类型
    std::string columnName; // 列名
    SqlType columnType;     // 列类型
};

/**
 * 创建表语句
 * 例如：CREATE TABLE student (id INT, name TEXT, score FLOAT);
 */
struct CreateTableStmt {
    std::string table;              // 表名
    std::vector<ColumnDef> columns; // 列定义
};

/**
 * 插入数据语句
 * 例如：INSERT INTO student VALUES (1, 'Tom', 93.5);
 */
struct InsertStmt {
    std::string table;                // 表名
    std::vector<std::string> columns; // 指定的列名（可选）
    std::vector<std::string> values;  // 插入的值
};

/**
 * 查询语句
 * 例如：SELECT * FROM student WHERE id = 1;
 */
struct SelectStmt {
    bool selectAll = false;           // 是否查询所有列
    std::vector<std::string> columns; // 查询的列名
    std::string table;                // 表名
    std::string whereColumn;          // WHERE 条件列
    std::string whereValue;           // WHERE 条件值
};

/**
 * 删除数据语句
 * 例如：DELETE FROM student WHERE id = 1;
 */
struct DeleteStmt {
    std::string table;          // 表名
    std::string whereColumn;    // WHERE 条件列
    std::string whereValue;     // WHERE 条件值
};

/**
 * 更新数据语句
 * 例如：UPDATE student SET score = 95 WHERE id = 1;
 */
struct UpdateStmt {
    std::string table;          // 表名
    std::string setColumn;      // 要更新的列
    std::string setValue;       // 新值
    std::string whereColumn;    // WHERE 条件列
    std::string whereValue;     // WHERE 条件值
};

/**
 * 创建用户语句
 * 例如：CREATE USER admin IDENTIFIED BY '123456' ROLE ADMIN;
 */
struct CreateUserStmt {
    std::string username;  // 用户名
    std::string password;  // 密码（明文，后续需加密存储）
    bool hasRole;  // 是否指定了角色
    RoleType role;  // 角色（仅当 hasRole 为 true 时有效）
};

/**
 * 删除用户语句
 * 例如：DROP USER admin;
 */
struct DropUserStmt {
    std::string username;  // 用户名
};

/**
 * 显示所有用户语句
 * 例如：SHOW USERS;
 */
struct ShowUsersStmt {
};

/**
 * 登录语句
 * 例如：LOGIN admin IDENTIFIED BY '123456';
 */
struct LoginStmt {
    std::string username;  // 用户名
    std::string password;  // 密码
};

/**
 * 登出语句
 * 例如：LOGOUT;
 */
struct LogoutStmt {
};

/**
 * 创建角色语句
 * 例如：CREATE ROLE admin;
 */
struct CreateRoleStmt {
    RoleType role;  // 角色类型
};

/**
 * 删除角色语句
 * 例如：DROP ROLE admin;
 */
struct DropRoleStmt {
    RoleType role;  // 角色类型
};

/**
 * 授权语句
 * 例如：GRANT SELECT, INSERT TO user;
 * 例如：GRANT ADMIN TO admin_user;
 */
struct GrantStmt {
    std::vector<Privilege> privileges;  // 权限列表
    bool hasRole = false;              // 是否有角色授予
    RoleType role;                      // 角色（如果授予角色）
    std::string username;                // 用户名
};

/**
 * 撤销权限语句
 * 例如：REVOKE SELECT FROM user;
 * 例如：REVOKE ADMIN FROM admin_user;
 */
struct RevokeStmt {
    std::vector<Privilege> privileges;  // 权限列表
    bool hasRole = false;               // 是否有角色撤销
    RoleType role;                      // 角色（如果撤销角色）
    std::string username;                // 用户名
};

/**
 * 查看用户权限语句
 * 例如：SHOW GRANTS FOR user;
 */
struct ShowGrantsStmt {
    std::string username;  // 用户名
};

/**
 * 查看审计日志语句
 * 例如：SHOW AUDIT LOG;
 */
struct ShowAuditLogStmt {
};

/**
 * 语句变体类型
 * 包含所有支持的 SQL 语句类型
 */
using Stmt = std::variant<ConnectStmt, CreateDatabaseStmt, DropDatabaseStmt, ShowDatabasesStmt, ShowTablesStmt, DescribeStmt,
                            DropTableStmt, AlterTableStmt, CreateTableStmt, InsertStmt, SelectStmt, DeleteStmt, UpdateStmt,
                            CreateUserStmt, DropUserStmt, ShowUsersStmt, LoginStmt, LogoutStmt,
                            CreateRoleStmt, DropRoleStmt, GrantStmt, RevokeStmt, ShowGrantsStmt, ShowAuditLogStmt>;

} // namespace ast

#endif
