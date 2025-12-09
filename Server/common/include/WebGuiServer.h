#pragma once
#include <thread>
#include <atomic>
#include <string>
#include <memory>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>

// Forward declarations for server data access
template<typename T> class ThreadSafeQueue;
class BaseServer;

class WebGuiServer {
public:
    WebGuiServer(BaseServer* server, int port);
    ~WebGuiServer();
    void Start();
    void Stop();
    bool IsRunning() const { return running; }
private:
    void Run();
    void HandleRequest(boost::beast::http::request<boost::beast::http::string_body>&& req,
                       boost::beast::tcp_stream&& stream);
    void RouteRequest(const std::string& target, boost::beast::http::request<boost::beast::http::string_body>&& req, boost::beast::tcp_stream&& stream);
    void HandleSessions(boost::beast::http::request<boost::beast::http::string_body>&& req, boost::beast::tcp_stream&& stream);
    void HandleShards(boost::beast::http::request<boost::beast::http::string_body>&& req, boost::beast::tcp_stream&& stream);
    void HandleEntities(boost::beast::http::request<boost::beast::http::string_body>&& req, boost::beast::tcp_stream&& stream);
    void HandleLogs(boost::beast::http::request<boost::beast::http::string_body>&& req, boost::beast::tcp_stream&& stream);
    void HandleFrontend(boost::beast::http::request<boost::beast::http::string_body>&& req, boost::beast::tcp_stream&& stream);
    std::thread serverThread;
    std::atomic<bool> running{false};
    int port;
    BaseServer* server;
};
