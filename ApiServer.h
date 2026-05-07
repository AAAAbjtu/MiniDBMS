#ifndef APISERVER_H
#define APISERVER_H

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace httplib {
    class Server;
}

class Executor;
class Parser;

class ApiServer {
public:
    explicit ApiServer(int port = 3000);
    ~ApiServer();

    void start();
    void stop();

private:
    struct Session {
        std::unique_ptr<Executor> executor;
        std::unique_ptr<Parser>   parser;
        std::string               token;
        std::chrono::steady_clock::time_point createdAt;
        std::chrono::steady_clock::time_point lastAccess;
    };

    std::map<std::string, std::unique_ptr<Session>> sessions_;
    std::mutex sessionsMutex_;
    std::mutex executeMutex_;

    std::unique_ptr<httplib::Server> server_;
    int port_;
    bool running_;

    std::string generateToken();
    Session*    getSession(const std::string& token);
    void        removeSession(const std::string& token);
    void        cleanupExpiredSessions();

    std::string captureExecute(Session& session, const std::string& sql);
    std::string classifyOutput(const std::string& text, bool& success);

    void setupRoutes();
};

#endif // APISERVER_H
