#pragma once
#include <string>
#include <vector>
struct redisContext;

class RedisClient {
public:
    RedisClient();
    ~RedisClient();
    bool connect(const std::string& host, int port, const std::string& user, const std::string& password);
    bool set(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    bool expire(const std::string& key, int seconds);
private:
    redisContext* ctx;
};
