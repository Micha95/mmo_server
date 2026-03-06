// All includes at the top
#include "RedisClient.h"

#include <hiredis/hiredis.h>
#include <map>
#include <vector>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <functional>
#include "Log.h"


RedisClient::RedisClient() : ctx(nullptr), subCtx(nullptr) {}
RedisClient::~RedisClient() {
    if (ctx) redisFree(ctx);
    if (subCtx) redisFree(subCtx);
}

bool RedisClient::connect(const std::string& host, int port, const std::string& user, const std::string& password) {
    if (ctx) redisFree(ctx);
    ctx = redisConnect(host.c_str(), port);
    if (!ctx || ctx->err) {
        if (ctx) { redisFree(ctx); ctx = nullptr; }
        return false;
    }
    // Setup subCtx for subscription
    if (subCtx) redisFree(subCtx);
    subCtx = redisConnect(host.c_str(), port);
    if (!subCtx || subCtx->err) {
        if (subCtx) { redisFree(subCtx); subCtx = nullptr; }
        redisFree(ctx); ctx = nullptr;
        return false;
    }
    if (!password.empty()) {
        redisReply* reply = nullptr;
        if (!user.empty()) {
            reply = (redisReply*)redisCommand(ctx, "AUTH %s %s", user.c_str(), password.c_str());
        } else {
            reply = (redisReply*)redisCommand(ctx, "AUTH %s", password.c_str());
        }
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            redisFree(ctx); ctx = nullptr;
            redisFree(subCtx); subCtx = nullptr;
            return false;
        }
        freeReplyObject(reply);
        // Authenticate subCtx
        reply = nullptr;
        if (!user.empty()) {
            reply = (redisReply*)redisCommand(subCtx, "AUTH %s %s", user.c_str(), password.c_str());
        } else {
            reply = (redisReply*)redisCommand(subCtx, "AUTH %s", password.c_str());
        }
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            if (reply) freeReplyObject(reply);
            redisFree(ctx); ctx = nullptr;
            redisFree(subCtx); subCtx = nullptr;
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

bool RedisClient::hset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& fields) {
    if (!ctx) return false;
    std::vector<std::string> argStorage; // Keep all strings alive
    argStorage.push_back("HSET");
    argStorage.push_back(key);
    for (const auto& kv : fields) {
        argStorage.push_back(kv.first);
        argStorage.push_back(kv.second);
    }
    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    for (const auto& s : argStorage) {
        argv.push_back(s.c_str());
        argvlen.push_back(s.size());
    }
    redisReply* reply = (redisReply*)redisCommandArgv(ctx, argv.size(), argv.data(), argvlen.data());
    bool ok = reply && (reply->type == REDIS_REPLY_INTEGER || reply->type == REDIS_REPLY_STATUS);
    if (reply) freeReplyObject(reply);
    return ok;
}

bool RedisClient::keys(const std::string& pattern, std::vector<std::string>& outKeys) {
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "KEYS %s", pattern.c_str());
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    outKeys.clear();
    for (size_t i = 0; i < reply->elements; ++i) {
        outKeys.push_back(reply->element[i]->str);
    }
    freeReplyObject(reply);
    return true;
}

bool RedisClient::hgetall(const std::string& key, std::map<std::string, std::string>& outFields) {
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HGETALL %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_ARRAY || reply->elements % 2 != 0) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    outFields.clear();
    for (size_t i = 0; i < reply->elements; i += 2) {
        const char* fieldStr = reply->element[i]->str;
        const char* valueStr = reply->element[i+1]->str;
        std::string field = fieldStr ? fieldStr : "";
        std::string value = valueStr ? valueStr : "";
        outFields[field] = value;
    }
    freeReplyObject(reply);
    return true;
}

// Publish a message to a Redis channel
bool RedisClient::publish(const std::string& channel, const std::string& message) {
    if (!ctx) return false;
    LOG_DEBUG_EXT("Publishing to Redis channel '" + channel + "': " + message +"size=" + std::to_string(message.size()));
    const char* argv[3];
    size_t argvlen[3];
    argv[0] = "PUBLISH";
    argvlen[0] = 7;
    argv[1] = channel.c_str();
    argvlen[1] = channel.size();
    argv[2] = message.data();
    argvlen[2] = message.size();
    redisReply* reply = (redisReply*)redisCommandArgv(ctx, 3, argv, argvlen);
    bool ok = reply && (reply->type == REDIS_REPLY_INTEGER || reply->type == REDIS_REPLY_STATUS);
    if (reply) freeReplyObject(reply);
    return ok;
}


// Subscribe to a channel (sends SUBSCRIBE command)
bool RedisClient::subscribe(const std::string& channel) {
    if (!subCtx) return false;
    redisReply* reply = (redisReply*)redisCommand(subCtx, "SUBSCRIBE %s", channel.c_str());
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    freeReplyObject(reply);
    return true;
}

// Poll for a message (non-blocking)
bool RedisClient::pollMessage(std::string& outChannel, std::string& outPayload) {
    if (!subCtx) return false;

    //Use select() to check if there's data to read on subCtx->fd before calling redisGetReply, to avoid blocking
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (subCtx->fd >= 0) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(subCtx->fd, &rfds);
        int ret = select(subCtx->fd + 1, &rfds, nullptr, nullptr, &tv);
        if (ret <= 0) {
            return false;
        }
    }
    redisReply* reply = nullptr;
    bool gotMsg = false;
    int rc = redisGetReply(subCtx, (void**)&reply);
    if (rc == REDIS_OK) {
        if (reply) {
            LOG_DEBUG_EXT("Redis reply type: " + std::to_string(reply->type) + ", elements: " + std::to_string(reply->elements));
            if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 1) {
                std::string type = reply->element[0]->str ? reply->element[0]->str : "";
                LOG_DEBUG_EXT("Redis array reply type: " + type);
                if (type == "message" && reply->elements >= 3) {
                    // Handle channel as string (should be safe)
                    outChannel = reply->element[1]->str ? reply->element[1]->str : "";
                    // Handle payload as binary-safe string
                    if (reply->element[2]->str && reply->element[2]->len > 0) {
                        outPayload.assign(reply->element[2]->str, reply->element[2]->len);
                    } else {
                        outPayload.clear();
                    }
                    LOG_DEBUG_EXT("Received message: channel='" + outChannel + "', payload size=" + std::to_string(outPayload.size()));
                    gotMsg = true;
                }
            }
        }
    } else {
        LOG_ERROR("redisGetReply failed or returned no reply: " + std::to_string(rc));
    }
    if (reply) freeReplyObject(reply);
    return gotMsg;
    /*struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (subCtx->fd >= 0) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(subCtx->fd, &rfds);
        int ret = select(subCtx->fd + 1, &rfds, nullptr, nullptr, &tv);
        if (ret <= 0) return false; // No data available, don't block
    }*/
}
