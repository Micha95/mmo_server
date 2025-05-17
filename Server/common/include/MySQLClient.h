#pragma once
#include <string>
#include <vector>
struct MYSQL;

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
    std::string getLastError() const;
private:
    MYSQL* conn;
};
