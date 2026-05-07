/**
 * Executor 模块实现
 * 负责执行 SQL 语句
 */

#include "Executor.h"
#include "Ast.h"
#include "Schema.h"
#include <iostream>
#include <type_traits>
#include <utility>

namespace {

/**
 * 构建插入记录
 * 处理 INSERT 语句的值与列的匹配
 */
static bool buildInsertRecord(Table& t, const ast::InsertStmt& ins, Record& out, std::string& err) {
    const auto& sch = t.getSchema();
    if (sch.empty()) {
        err = "表无列定义";
        return false;
    }
    out.cells.resize(sch.size());
    // 初始化默认值
    for (size_t i = 0; i < sch.size(); ++i) {
        switch (sch[i].type) {
        case SqlType::Int:
            out.cells[i] = Cell{static_cast<std::int64_t>(0)};
            break;
        case SqlType::Float:
            out.cells[i] = Cell{0.0};
            break;
        case SqlType::Text:
            out.cells[i] = Cell{std::string{}};
            break;
        }
    }
    if (!ins.columns.empty()) {
        // 处理指定列插入
        for (size_t i = 0; i < ins.columns.size() && i < ins.values.size(); ++i) {
            int idx = t.columnIndex(ins.columns[i]);
            if (idx < 0) {
                continue;
            }
            auto c = parseLiteralToCell(ins.values[i], sch[static_cast<size_t>(idx)].type);
            if (!c.has_value()) {
                err = "值与列类型不匹配: " + ins.columns[i];
                return false;
            }
            out.cells[static_cast<size_t>(idx)] = *c;
        }
        return true;
    }
    // 处理全列插入
    if (ins.values.size() != sch.size()) {
        err = "列数与值数量不一致";
        return false;
    }
    for (size_t i = 0; i < sch.size(); ++i) {
        auto c = parseLiteralToCell(ins.values[i], sch[i].type);
        if (!c.has_value()) {
            err = "值与列类型不匹配";
            return false;
        }
        out.cells[i] = *c;
    }
    return true;
}

} // namespace

/** 构造函数 */
Executor::Executor() {
    FileManager::ensureDatabase(currentDb);
}

/** 确保数据库连接 */
bool Executor::ensureConnected() {
    return FileManager::ensureDatabase(currentDb);
}

/**
 * 加载表
 * 如果表已加载则返回现有表，否则从文件加载
 */
Table& Executor::loadTable(const std::string& tableName) {
    auto it = tables.find(tableName);
    if (it != tables.end()) {
        return it->second;
    }
    tables[tableName] = FileManager::load(currentDb, tableName);
    return tables[tableName];
}

/**
 * 打印查询结果
 */
void Executor::printRows(const Table& table, const std::vector<Record>& rows, const std::vector<std::string>& selectedColumns) {
    std::vector<int> idxs;  // 列索引
    std::vector<std::string> headers;  // 列名
    std::vector<std::string> allNames = table.getColumnNames();

    if (selectedColumns.empty()) {
        // 显示所有列
        headers = allNames;
        for (size_t i = 0; i < headers.size(); ++i) {
            idxs.push_back(static_cast<int>(i));
        }
    } else {
        // 显示指定列
        for (const auto& c : selectedColumns) {
            int idx = table.columnIndex(c);
            if (idx >= 0) {
                idxs.push_back(idx);
                headers.push_back(c);
            }
        }
    }

    // 打印表头
    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) {
            std::cout << " | ";
        }
        std::cout << headers[i];
    }
    std::cout << "\n";

    // 打印数据行
    for (const auto& r : rows) {
        for (size_t i = 0; i < idxs.size(); ++i) {
            if (i > 0) {
                std::cout << " | ";
            }
            int idx = idxs[i];
            if (idx >= 0 && idx < static_cast<int>(r.cells.size())) {
                std::cout << cellToString(r.cells[static_cast<size_t>(idx)]);
            }
        }
        std::cout << "\n";
    }
    std::cout << "(" << rows.size() << " rows)\n";
}

/**
 * 执行 SQL 语句
 */
void Executor::execute(const ParseResult& pr) {
    if (!pr.errorMsg.empty()) {
        std::cout << "SQL 错误: " << pr.errorMsg << "\n";
        return;
    }
    if (!pr.stmt.has_value()) {
        return;
    }
    if (!ensureConnected()) {
        std::cout << "Cannot connect db.\n";
        return;
    }

    std::visit(
        [this](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            
            // 执行 CONNECT 语句
            if constexpr (std::is_same_v<T, ast::ConnectStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::CONNECT))) {
                    std::cout << "Permission denied: you need CONNECT privilege.\n";
                    FileManager::addAuditLog(currentUser, "CONNECT", arg.database, "DENIED");
                    return;
                }
                const auto& s = arg;
                currentDb = s.database.empty() ? "default" : s.database;
                tables.clear();
                if (FileManager::ensureDatabase(currentDb)) {
                    std::cout << "Connected to db: " << currentDb << "\n";
                    FileManager::addAuditLog(currentUser, "CONNECT", currentDb, "SUCCESS");
                } else {
                    std::cout << "Connect failed.\n";
                    FileManager::addAuditLog(currentUser, "CONNECT", s.database, "FAILED");
                }
            }
            
            // 执行 CREATE DATABASE 语句
            else if constexpr (std::is_same_v<T, ast::CreateDatabaseStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::CREATE_DB))) {
                    std::cout << "Permission denied: you need CREATE DATABASE privilege.\n";
                    FileManager::addAuditLog(currentUser, "CREATE DATABASE", arg.database, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (FileManager::ensureDatabase(s.database)) {
                    std::cout << "Database created: " << s.database << "\n";
                    FileManager::addAuditLog(currentUser, "CREATE DATABASE", s.database, "SUCCESS");
                } else {
                    std::cout << "Create database failed.\n";
                    FileManager::addAuditLog(currentUser, "CREATE DATABASE", s.database, "FAILED");
                }
            }

            // 执行 DROP DATABASE 语句
            else if constexpr (std::is_same_v<T, ast::DropDatabaseStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DROP_DB))) {
                    std::cout << "Permission denied: you need DROP DATABASE privilege.\n";
                    FileManager::addAuditLog(currentUser, "DROP DATABASE", arg.database, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (FileManager::dropDatabase(s.database)) {
                    std::cout << "Database dropped: " << s.database << "\n";
                    FileManager::addAuditLog(currentUser, "DROP DATABASE", s.database, "SUCCESS");
                    if (currentDb == s.database) {
                        currentDb = "default";
                        tables.clear();
                        FileManager::ensureDatabase(currentDb);
                    }
                } else {
                    std::cout << "Drop database failed.\n";
                    FileManager::addAuditLog(currentUser, "DROP DATABASE", s.database, "FAILED");
                }
            }
            
            // 执行 SHOW TABLES 语句
            else if constexpr (std::is_same_v<T, ast::ShowTablesStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SHOW_TABLES))) {
                    std::cout << "Permission denied: you need SHOW TABLES privilege.\n";
                    FileManager::addAuditLog(currentUser, "SHOW TABLES", "-", "DENIED");
                    return;
                }
                (void)arg;
                auto names = FileManager::listTables(currentDb);
                std::cout << "Tables_in_" << currentDb << "\n";
                for (const auto& name : names) {
                    std::cout << name << "\n";
                }
                std::cout << "(" << names.size() << " tables)\n";
            }

            // 执行 SHOW DATABASES 语句
            else if constexpr (std::is_same_v<T, ast::ShowDatabasesStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SHOW_DATABASES))) {
                    std::cout << "Permission denied: you need SHOW DATABASES privilege.\n";
                    FileManager::addAuditLog(currentUser, "SHOW DATABASES", "-", "DENIED");
                    return;
                }
                (void)arg;
                auto names = FileManager::listDatabases();
                std::cout << "Databases\n";
                for (const auto& name : names) {
                    std::cout << name << "\n";
                }
                std::cout << "(" << names.size() << " databases)\n";
            }

            // 执行 DESC 语句
            else if constexpr (std::is_same_v<T, ast::DescribeStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DESCRIBE))) {
                    std::cout << "Permission denied: you need DESCRIBE privilege.\n";
                    FileManager::addAuditLog(currentUser, "DESCRIBE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                std::cout << "Field\tType\n";
                for (const auto& c : t.getSchema()) {
                    std::cout << c.name << "\t" << sqlTypeToString(c.type) << "\n";
                }
                std::cout << "(" << t.getSchema().size() << " columns)\n";
                FileManager::addAuditLog(currentUser, "DESCRIBE", s.table, "SUCCESS");
            }

            // 执行 DROP TABLE 语句
            else if constexpr (std::is_same_v<T, ast::DropTableStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DROP_TABLE))) {
                    std::cout << "Permission denied: you need DROP TABLE privilege.\n";
                    FileManager::addAuditLog(currentUser, "DROP TABLE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                if (FileManager::dropTable(currentDb, s.table)) {
                    tables.erase(s.table);
                    std::cout << "Table dropped: " << s.table << "\n";
                    FileManager::addAuditLog(currentUser, "DROP TABLE", s.table, "SUCCESS");
                } else {
                    std::cout << "Drop table failed: " << s.table << "\n";
                    FileManager::addAuditLog(currentUser, "DROP TABLE", s.table, "FAILED");
                }
            }
            
            // 执行 ALTER TABLE 语句
            else if constexpr (std::is_same_v<T, ast::AlterTableStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::ALTER_TABLE))) {
                    std::cout << "Permission denied: you need ALTER TABLE privilege.\n";
                    FileManager::addAuditLog(currentUser, "ALTER TABLE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                bool success = false;
                switch (s.op) {
                case ast::AlterOperation::AddColumn:
                    t.addColumn(s.columnName, s.columnType);
                    success = true;
                    std::cout << "Column added: " << s.columnName << "\n";
                    break;
                case ast::AlterOperation::DropColumn:
                    t.dropColumn(s.columnName);
                    success = true;
                    std::cout << "Column dropped: " << s.columnName << "\n";
                    break;
                case ast::AlterOperation::ModifyColumn:
                    success = t.modifyColumn(s.columnName, s.columnType);
                    if (success) {
                        std::cout << "Column modified: " << s.columnName << "\n";
                    } else {
                        std::cout << "Column not found: " << s.columnName << "\n";
                    }
                    break;
                }
                if (success) {
                    FileManager::save(currentDb, t);
                    FileManager::addAuditLog(currentUser, "ALTER TABLE", s.table, "SUCCESS");
                }
            }

            // 执行 CREATE TABLE 语句
            else if constexpr (std::is_same_v<T, ast::CreateTableStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::CREATE_TABLE))) {
                    std::cout << "Permission denied: you need CREATE TABLE privilege.\n";
                    FileManager::addAuditLog(currentUser, "CREATE TABLE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (s.columns.empty()) {
                    std::cout << "CREATE TABLE requires columns.\n";
                    return;
                }
                Table tab(s.table, s.columns);
                tables[s.table] = std::move(tab);
                FileManager::save(currentDb, tables[s.table]);
                std::cout << "Table created: " << s.table << "\n";
                FileManager::addAuditLog(currentUser, "CREATE TABLE", s.table, "SUCCESS");
            }

            // 执行 INSERT 语句
            else if constexpr (std::is_same_v<T, ast::InsertStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::INSERT))) {
                    std::cout << "Permission denied: you need INSERT privilege.\n";
                    FileManager::addAuditLog(currentUser, "INSERT", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                Record r;
                std::string err;
                if (!buildInsertRecord(t, s, r, err)) {
                    std::cout << err << "\n";
                    return;
                }
                t.insert(r);
                FileManager::save(currentDb, t);
                std::cout << "1 row inserted.\n";
                FileManager::addAuditLog(currentUser, "INSERT", s.table, "SUCCESS");
            }

            // 执行 SELECT 语句
            else if constexpr (std::is_same_v<T, ast::SelectStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SELECT))) {
                    std::cout << "Permission denied: you need SELECT privilege.\n";
                    FileManager::addAuditLog(currentUser, "SELECT", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                auto rows = t.select(s.whereColumn, s.whereValue);
                printRows(t, rows, s.selectAll ? std::vector<std::string>{} : s.columns);
                FileManager::addAuditLog(currentUser, "SELECT", s.table, "SUCCESS");
            }
            
            // 执行 DELETE 语句
            else if constexpr (std::is_same_v<T, ast::DeleteStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DELETE))) {
                    std::cout << "Permission denied: you need DELETE privilege.\n";
                    FileManager::addAuditLog(currentUser, "DELETE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                int n = t.deleteRows(s.whereColumn, s.whereValue);
                FileManager::save(currentDb, t);
                std::cout << n << " rows deleted.\n";
                FileManager::addAuditLog(currentUser, "DELETE", s.table, "SUCCESS");
            }

            // 执行 UPDATE 语句
            else if constexpr (std::is_same_v<T, ast::UpdateStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::UPDATE))) {
                    std::cout << "Permission denied: you need UPDATE privilege.\n";
                    FileManager::addAuditLog(currentUser, "UPDATE", arg.table, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                int n = t.updateRows(s.setColumn, s.setValue, s.whereColumn, s.whereValue);
                FileManager::save(currentDb, t);
                std::cout << n << " rows updated.\n";
                FileManager::addAuditLog(currentUser, "UPDATE", s.table, "SUCCESS");
            }

            // 执行 CREATE USER 语句
            else if constexpr (std::is_same_v<T, ast::CreateUserStmt>) {
                const auto& s = arg;
                if (s.username.empty()) {
                    std::cout << "Username cannot be empty.\n";
                    return;
                }

                // 检查是否有用户存在，如果没有则允许创建第一个管理员
                auto existingUsers = FileManager::listUsers();
                bool isFirstUser = existingUsers.empty();

                if (!isFirstUser) {
                    // 不是第一个用户，需要权限检查
                    if (currentUser.empty()) {
                        std::cout << "Permission denied: please login first.\n";
                        return;
                    }
                    if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::CREATE_USER))) {
                        std::cout << "Permission denied: you need CREATE USER privilege.\n";
                        FileManager::addAuditLog(currentUser, "CREATE USER", arg.username, "DENIED");
                        return;
                    }
                }

                // 确定角色
                int role = 2; // 默认 USER
                if (s.hasRole) {
                    switch (s.role) {
                        case ast::RoleType::ADMIN: role = 0; break;
                        case ast::RoleType::DBA: role = 1; break;
                        case ast::RoleType::USER: role = 2; break;
                    }
                } else if (isFirstUser) {
                    // 第一个用户默认是 ADMIN
                    role = 0;
                    std::cout << "INFO: First user created as ADMIN by default.\n";
                }

                if (FileManager::createUser(s.username, s.password, role)) {
                    std::string roleName = (role == 0) ? "ADMIN" : (role == 1) ? "DBA" : "USER";
                    std::cout << "User created: " << s.username << " (" << roleName << ")\n";
                    if (!isFirstUser) {
                        FileManager::addAuditLog(currentUser, "CREATE USER", s.username, "SUCCESS");
                    }
                } else {
                    std::cout << "User already exists or create failed: " << s.username << "\n";
                    if (!isFirstUser) {
                        FileManager::addAuditLog(currentUser, "CREATE USER", s.username, "FAILED");
                    }
                }
            }

            // 执行 DROP USER 语句
            else if constexpr (std::is_same_v<T, ast::DropUserStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DROP_USER))) {
                    std::cout << "Permission denied: you need DROP USER privilege.\n";
                    FileManager::addAuditLog(currentUser, "DROP USER", arg.username, "DENIED");
                    return;
                }
                const auto& s = arg;
                if (FileManager::dropUser(s.username)) {
                    std::cout << "User dropped: " << s.username << "\n";
                    if (currentUser == s.username) {
                        currentUser.clear();
                        std::cout << "You have been logged out.\n";
                    }
                    FileManager::addAuditLog(currentUser, "DROP USER", s.username, "SUCCESS");
                } else {
                    std::cout << "User not found: " << s.username << "\n";
                    FileManager::addAuditLog(currentUser, "DROP USER", s.username, "FAILED");
                }
            }

            // 执行 SHOW USERS 语句
            else if constexpr (std::is_same_v<T, ast::ShowUsersStmt>) {
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SHOW_USERS))) {
                    std::cout << "Permission denied: you need SHOW USERS privilege.\n";
                    FileManager::addAuditLog(currentUser, "SHOW USERS", "-", "DENIED");
                    return;
                }
                (void)arg;
                auto users = FileManager::listUsers();
                std::cout << "Username    | Role\n";
                std::cout << "------------|------\n";
                for (const auto& u : users) {
                    std::string roleName;
                    switch (static_cast<ast::RoleType>(u.role)) {
                    case ast::RoleType::ADMIN: roleName = "ADMIN"; break;
                    case ast::RoleType::DBA: roleName = "DBA"; break;
                    case ast::RoleType::USER: roleName = "USER"; break;
                    default: roleName = "NONE"; break;
                    }
                    std::cout << u.username << "     | " << roleName << "\n";
                }
                std::cout << "(" << users.size() << " users)\n";
            }

            // 执行 LOGIN 语句
            else if constexpr (std::is_same_v<T, ast::LoginStmt>) {
                const auto& s = arg;
                if (FileManager::validateUser(s.username, s.password)) {
                    currentUser = s.username;
                    std::cout << "Login successful: " << s.username << "\n";
                    FileManager::addAuditLog(s.username, "LOGIN", "-", "SUCCESS");
                } else {
                    std::cout << "Login failed: invalid username or password.\n";
                    FileManager::addAuditLog(s.username, "LOGIN", "-", "FAILED");
                }
            }

            // 执行 LOGOUT 语句
            else if constexpr (std::is_same_v<T, ast::LogoutStmt>) {
                (void)arg;
                if (currentUser.empty()) {
                    std::cout << "No user is currently logged in.\n";
                } else {
                    std::cout << "Logged out: " << currentUser << "\n";
                    FileManager::addAuditLog(currentUser, "LOGOUT", "-", "SUCCESS");
                    currentUser.clear();
                }
            }

            // 执行 CREATE ROLE 语句
            else if constexpr (std::is_same_v<T, ast::CreateRoleStmt>) {
                (void)arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::CREATE_ROLE))) {
                    std::cout << "Permission denied: you need CREATE ROLE privilege.\n";
                    FileManager::addAuditLog(currentUser, "CREATE ROLE", "-", "DENIED");
                    return;
                }
                std::string roleName;
                switch (arg.role) {
                case ast::RoleType::ADMIN: roleName = "ADMIN"; break;
                case ast::RoleType::DBA: roleName = "DBA"; break;
                case ast::RoleType::USER: roleName = "USER"; break;
                }
                std::cout << "Role created: " << roleName << " (role assignment is automatic based on user creation)\n";
                FileManager::addAuditLog(currentUser, "CREATE ROLE", roleName, "SUCCESS");
            }

            // 执行 DROP ROLE 语句
            else if constexpr (std::is_same_v<T, ast::DropRoleStmt>) {
                (void)arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::DROP_ROLE))) {
                    std::cout << "Permission denied: you need DROP ROLE privilege.\n";
                    FileManager::addAuditLog(currentUser, "DROP ROLE", "-", "DENIED");
                    return;
                }
                std::string roleName;
                switch (arg.role) {
                case ast::RoleType::ADMIN: roleName = "ADMIN"; break;
                case ast::RoleType::DBA: roleName = "DBA"; break;
                case ast::RoleType::USER: roleName = "USER"; break;
                }
                std::cout << "Role dropped: " << roleName << "\n";
                FileManager::addAuditLog(currentUser, "DROP ROLE", roleName, "SUCCESS");
            }

            // 执行 GRANT 语句
            else if constexpr (std::is_same_v<T, ast::GrantStmt>) {
                const auto& s = arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::GRANT))) {
                    std::cout << "Permission denied: you need GRANT privilege.\n";
                    FileManager::addAuditLog(currentUser, "GRANT", "-", "DENIED");
                    return;
                }
                if (s.hasRole) {
                    UserInfo targetUser = FileManager::getUser(s.username);
                    if (targetUser.username.empty()) {
                        std::cout << "User not found: " << s.username << "\n";
                        FileManager::addAuditLog(currentUser, "GRANT ROLE", s.username, "FAILED");
                        return;
                    }
                    targetUser.role = static_cast<int>(s.role);
                    FileManager::updateUser(targetUser);
                    std::string roleName;
                    switch (s.role) {
                    case ast::RoleType::ADMIN: roleName = "ADMIN"; break;
                    case ast::RoleType::DBA: roleName = "DBA"; break;
                    case ast::RoleType::USER: roleName = "USER"; break;
                    }
                    std::cout << "Role " << roleName << " granted to: " << s.username << "\n";
                    FileManager::addAuditLog(currentUser, "GRANT ROLE", s.username, "SUCCESS");
                } else {
                    for (auto priv : s.privileges) {
                        FileManager::grantPrivilege(s.username, static_cast<int>(priv));
                    }
                    std::cout << "Privileges granted to: " << s.username << "\n";
                    FileManager::addAuditLog(currentUser, "GRANT PRIVILEGES", s.username, "SUCCESS");
                }
            }

            // 执行 REVOKE 语句
            else if constexpr (std::is_same_v<T, ast::RevokeStmt>) {
                const auto& s = arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::REVOKE))) {
                    std::cout << "Permission denied: you need REVOKE privilege.\n";
                    FileManager::addAuditLog(currentUser, "REVOKE", "-", "DENIED");
                    return;
                }
                if (s.hasRole) {
                    UserInfo targetUser = FileManager::getUser(s.username);
                    if (targetUser.username.empty()) {
                        std::cout << "User not found: " << s.username << "\n";
                        FileManager::addAuditLog(currentUser, "REVOKE ROLE", s.username, "FAILED");
                        return;
                    }
                    targetUser.role = 0;
                    FileManager::updateUser(targetUser);
                    std::cout << "Role revoked from: " << s.username << "\n";
                    FileManager::addAuditLog(currentUser, "REVOKE ROLE", s.username, "SUCCESS");
                } else {
                    for (auto priv : s.privileges) {
                        FileManager::revokePrivilege(s.username, static_cast<int>(priv));
                    }
                    std::cout << "Privileges revoked from: " << s.username << "\n";
                    FileManager::addAuditLog(currentUser, "REVOKE PRIVILEGES", s.username, "SUCCESS");
                }
            }

            // 执行 SHOW GRANTS FOR 语句
            else if constexpr (std::is_same_v<T, ast::ShowGrantsStmt>) {
                const auto& s = arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SHOW_GRANTS))) {
                    std::cout << "Permission denied: you need SHOW GRANTS privilege.\n";
                    FileManager::addAuditLog(currentUser, "SHOW GRANTS", "-", "DENIED");
                    return;
                }
                UserInfo user = FileManager::getUser(s.username);
                if (user.username.empty()) {
                    std::cout << "User not found: " << s.username << "\n";
                    return;
                }
                std::cout << "Grants for " << s.username << ":\n";
                std::string roleName;
                switch (static_cast<ast::RoleType>(user.role)) {
                case ast::RoleType::ADMIN: roleName = "ADMIN"; break;
                case ast::RoleType::DBA: roleName = "DBA"; break;
                case ast::RoleType::USER: roleName = "USER"; break;
                default: roleName = "NONE"; break;
                }
                std::cout << "  Role: " << roleName << "\n";
                if (!user.privileges.empty()) {
                    std::cout << "  Privileges: ";
                    for (size_t i = 0; i < user.privileges.size(); ++i) {
                        if (i > 0) std::cout << ", ";
                        std::cout << user.privileges[i];
                    }
                    std::cout << "\n";
                }
            }

            // 执行 SHOW AUDIT LOG 语句
            else if constexpr (std::is_same_v<T, ast::ShowAuditLogStmt>) {
                (void)arg;
                if (currentUser.empty()) {
                    std::cout << "Permission denied: please login first.\n";
                    return;
                }
                if (!FileManager::hasPrivilege(currentUser, static_cast<int>(ast::Privilege::SHOW_AUDIT))) {
                    std::cout << "Permission denied: you need SHOW AUDIT privilege.\n";
                    return;
                }
                auto logs = FileManager::getAuditLogs();
                std::cout << "Timestamp            | User     | Action              | Target | Result\n";
                std::cout << "---------------------|----------|---------------------|--------|--------\n";
                for (const auto& log : logs) {
                    std::cout << log.timestamp;
                    std::cout.width(20 - log.timestamp.length());
                    std::cout << " | ";
                    std::cout.width(8);
                    std::cout << log.username << " | ";
                    std::cout.width(19);
                    std::cout << log.action << " | ";
                    std::cout.width(6);
                    std::cout << log.target << " | ";
                    std::cout << log.result << "\n";
                }
                std::cout << "(" << logs.size() << " log entries)\n";
            }
        },
        *pr.stmt);
}
