#pragma once
#include <string>
#include <vector>
#include <mysql/mysql.h> // Include MySQL header
#include <hiredis/hiredis.h> // Include Hiredis header

// Redis connection stub
class RedisClient {
public:
    RedisClient();
    ~RedisClient();
    bool connect(const std::string& host, int port, const std::string& user, const std::string& password);
    bool set(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    // Add more as needed
private:
    redisContext* ctx;
};

// MySQL connection stub
class MySQLClient {
public:
    MySQLClient();
    ~MySQLClient();
    bool connect(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& db);
    bool exec(const std::string& query);
    bool query(const std::string& query, std::vector<std::vector<std::string>>& result);
    bool insert(const std::string& table, const std::vector<std::string>& columns, const std::vector<std::string>& values);
    bool update(const std::string& table, const std::vector<std::string>& columns, const std::vector<std::string>& values, const std::string& where);
    bool remove(const std::string& table, const std::string& where);
private:
    MYSQL* conn;
};
