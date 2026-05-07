# MiniDB

MiniDB 是一款基于 C++17 开发的轻量级命令行数据库管理系统，采用模块化设计，实现了关系型数据库核心功能。支持强类型列、二进制存储、用户权限管理和审计日志。

A minimal DBMS in C++ with typed columns, binary storage, user/RBAC, and audit logging.

## 数据类型 / Data Types

| SQL 关键字 | 存储类型 |
|------------|----------|
| `INT`, `INTEGER`, `BIGINT`, `SMALLINT`, `TINYINT` | 64 位整数 |
| `FLOAT`, `REAL`, `DOUBLE` | 双精度浮点 |
| `TEXT`, `VARCHAR`, `STRING`, `CHAR` | UTF-8 文本 |

## SQL 示例 / Examples

```sql
-- 用户管理
CREATE USER admin IDENTIFIED BY '123456' ROLE ADMIN;
LOGIN admin IDENTIFIED BY '123456';

-- 数据库 & 表
CREATE DATABASE demo;
CONNECT demo;
CREATE TABLE student (id INT, name TEXT, score FLOAT);

-- 增删改查
INSERT INTO student VALUES (1, 'Tom', 93.5);
INSERT INTO student (name, score) VALUES ('Jane', 88);
SELECT * FROM student WHERE id = 1;
UPDATE student SET score = 95.0 WHERE id = 1;
DELETE FROM student WHERE id = 1;

-- DDL
ALTER TABLE student ADD age INT;
ALTER TABLE student DROP COLUMN age;
DESC student;
SHOW TABLES;
SHOW DATABASES;

-- 权限 & 审计
SHOW USERS;
GRANT SELECT, INSERT TO user1;
SHOW GRANTS FOR user1;
SHOW AUDIT LOG;
```

## Notes

- 数据目录：`data_<库名>/`，表文件：`*.tbl`（V2 二进制格式，带魔数 `MINIDB` + `0x02`）
- 用户数据：`system/users.dat`，审计日志：`system/audit.log`
- 可兼容读取旧版文本格式和 V1 二进制格式的表文件
- `WHERE` 为单列等值条件；未列出的高级 SQL 未实现
- 默认角色：ADMIN（全权限）、DBA（数据库/表管理）、USER（基本 CRUD）
