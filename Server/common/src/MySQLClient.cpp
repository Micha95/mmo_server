#include "MySQLClient.h"
#include <mysql/mysql.h>

MySQLClient::MySQLClient() : conn(nullptr) {}
MySQLClient::~MySQLClient() { if (conn) mysql_close(conn); }

bool MySQLClient::connect(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& db) {
    if (conn) mysql_close(conn);
    conn = mysql_init(nullptr);
    if (!conn) return false;
    if (!mysql_real_connect(conn, host.c_str(), user.c_str(), password.c_str(), db.c_str(), port, nullptr, 0)) {
        mysql_close(conn); conn = nullptr;
        return false;
    }
    return true;
}

bool MySQLClient::exec(const std::string& query) {
    if (!conn) return false;
    return mysql_query(conn, query.c_str()) == 0;
}

bool MySQLClient::query(const std::string& query, std::vector<std::vector<std::string>>& result) {
    result.clear();
    if (!conn) return false;
    if (mysql_query(conn, query.c_str()) != 0) return false;
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return false;
    int num_fields = mysql_num_fields(res);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        std::vector<std::string> rowData;
        for (int i = 0; i < num_fields; ++i) {
            rowData.push_back(row[i] ? row[i] : "");
        }
        result.push_back(rowData);
    }
    mysql_free_result(res);
    return true;
}

bool MySQLClient::insert(const std::string& table, const std::vector<std::string>& columns, const std::vector<std::string>& values) {
    if (columns.size() != values.size() || columns.empty()) return false;
    std::string query = "INSERT INTO " + table + " (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) query += ",";
        query += columns[i];
    }
    query += ") VALUES (";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) query += ",";
        query += "'" + values[i] + "'";
    }
    query += ")";
    return exec(query);
}

bool MySQLClient::update(const std::string& table, const std::vector<std::string>& columns, const std::vector<std::string>& values, const std::string& where) {
    if (columns.size() != values.size() || columns.empty()) return false;
    std::string query = "UPDATE " + table + " SET ";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) query += ", ";
        query += columns[i] + "='" + values[i] + "'";
    }
    if (!where.empty()) query += " WHERE " + where;
    return exec(query);
}

bool MySQLClient::remove(const std::string& table, const std::string& where) {
    std::string query = "DELETE FROM " + table;
    if (!where.empty()) query += " WHERE " + where;
    return exec(query);
}

std::string MySQLClient::getLastError() const {
    if (!conn) return "No connection";
    return mysql_error(conn);
}
