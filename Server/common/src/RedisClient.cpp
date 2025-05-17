#include "RedisClient.h"
#include <hiredis/hiredis.h>

RedisClient::RedisClient() : ctx(nullptr) {}
RedisClient::~RedisClient() { if (ctx) redisFree(ctx); }

bool RedisClient::connect(const std::string& host, int port, const std::string& user, const std::string& password) {
    if (ctx) redisFree(ctx);
    ctx = redisConnect(host.c_str(), port);
    if (!ctx || ctx->err) {
        if (ctx) { redisFree(ctx); ctx = nullptr; }
        return false;
    }
    if (!password.empty()) {
        redisReply* reply = (redisReply*)redisCommand(ctx, "AUTH %s %s", user.c_str(), password.c_str());
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            redisFree(ctx); ctx = nullptr;
            return false;
        }
        freeReplyObject(reply);
    }
    return true;
}

bool RedisClient::set(const std::string& key, const std::string& value) {
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SET %s %s", key.c_str(), value.c_str());
    bool ok = reply && (reply->type == REDIS_REPLY_STATUS || reply->type == REDIS_REPLY_STRING);
    if (reply) freeReplyObject(reply);
    return ok;
}

bool RedisClient::get(const std::string& key, std::string& value) {
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "GET %s", key.c_str());
    if (reply && reply->type == REDIS_REPLY_STRING) {
        value = reply->str;
        freeReplyObject(reply);
        return true;
    }
    if (reply) freeReplyObject(reply);
    return false;
}

bool RedisClient::del(const std::string& key) {
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());
    bool ok = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
    if (reply) freeReplyObject(reply);
    return ok;
}

bool RedisClient::exists(const std::string& key) {
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "EXISTS %s", key.c_str());
    bool ok = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
    if (reply) freeReplyObject(reply);
    return ok;
}

bool RedisClient::expire(const std::string& key, int seconds) {
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "EXPIRE %s %d", key.c_str(), seconds);
    bool ok = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    if (reply) freeReplyObject(reply);
    return ok;
}
