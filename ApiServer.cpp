#include "httplib.h"
// Windows headers (included via httplib.h) define DELETE as a macro,
// which conflicts with ast::Privilege::DELETE in Ast.h.
#ifdef DELETE
#undef DELETE
#endif
#include "ApiServer.h"
#include "Executor.h"
#include "Parser.h"
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

using json = nlohmann::json;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

// Served as fallback if file not found
const char* kFallbackHtml = R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>MiniDB Web Console</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', system-ui, sans-serif; background: #0d1117; color: #c9d1d9;
       display: flex; justify-content: center; align-items: center; min-height: 100vh; }
.card { background: #161b22; border: 1px solid #30363d; border-radius: 8px; padding: 40px;
        text-align: center; max-width: 500px; width: 90%; }
h1 { font-size: 24px; margin-bottom: 8px; color: #58a6ff; }
p { color: #8b949e; line-height: 1.6; }
code { background: #21262d; padding: 2px 6px; border-radius: 3px; font-size: 13px; }
</style>
</head>
<body>
<div class="card">
<h1>MiniDB Web Console</h1>
<p>The web frontend file was not found.</p>
<p>Place <code>web/index.html</code> in the project directory,<br>
or set <code>WEB_ROOT</code> in CMake to point to the correct location.</p>
</div>
</body>
</html>)";

} // namespace

// ---- Session helpers ----

std::string ApiServer::generateToken() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<unsigned long long> dist;
    std::ostringstream oss;
    oss << std::hex << dist(gen) << dist(gen);
    return oss.str();
}

ApiServer::Session* ApiServer::getSession(const std::string& token) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return nullptr;
    it->second->lastAccess = std::chrono::steady_clock::now();
    return it->second.get();
}

void ApiServer::removeSession(const std::string& token) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    sessions_.erase(token);
}

void ApiServer::cleanupExpiredSessions() {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto now = std::chrono::steady_clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        auto age = std::chrono::duration_cast<std::chrono::minutes>(now - it->second->lastAccess).count();
        if (age > 30) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---- Core execution capture ----

std::string ApiServer::captureExecute(Session& session, const std::string& sql) {
    ParseResult pr = session.parser->parse(sql);

    std::ostringstream oss;
    std::streambuf* oldBuf = std::cout.rdbuf(oss.rdbuf());

    session.executor->execute(pr);

    std::cout.rdbuf(oldBuf);
    return oss.str();
}

std::string ApiServer::classifyOutput(const std::string& text, bool& success) {
    if (text.find("SQL 错误") != std::string::npos ||
        text.find("Permission denied") != std::string::npos ||
        text.find("Login failed") != std::string::npos ||
        text.find("failed") != std::string::npos) {
        success = false;
        return "error";
    }
    if (text.find(" | ") != std::string::npos) {
        success = true;
        return "table";
    }
    success = true;
    return "info";
}

// ---- Route setup ----

void ApiServer::setupRoutes() {
    // Serve frontend
    server_->Get("/", [this](const httplib::Request&, httplib::Response& res) {
        std::string html;
        // Try WEB_ROOT index.html (set by CMake)
#ifdef WEB_ROOT
        html = readFile(std::string(WEB_ROOT) + "/index.html");
#endif
        // Fallback: try relative paths
        if (html.empty()) html = readFile("web/index.html");
        if (html.empty()) html = readFile("../web/index.html");
        if (html.empty()) html = kFallbackHtml;

        res.set_content(html, "text/html");
    });

    // POST /api/login
    server_->Post("/api/login", [this](const httplib::Request& req, httplib::Response& res) {
        json resp;
        try {
            auto body = json::parse(req.body);
            std::string username = body.value("username", "");
            std::string password = body.value("password", "");

            if (username.empty()) {
                resp = {{"success", false}, {"type", "error"}, {"message", "Username is required."}};
                res.set_content(resp.dump(), "application/json");
                return;
            }

            // Create session
            auto session = std::make_unique<Session>();
            session->executor = std::make_unique<Executor>();
            session->parser   = std::make_unique<Parser>();
            session->token    = generateToken();
            session->createdAt = std::chrono::steady_clock::now();
            session->lastAccess = session->createdAt;

            // First try login
            std::string sql = "LOGIN " + username + " IDENTIFIED BY '" + password + "';";
            bool success = false;
            std::string output;
            {
                std::lock_guard<std::mutex> lock(executeMutex_);
                output = captureExecute(*session, sql);
            }
            std::string type = classifyOutput(output, success);

            // If login failed and no users exist, auto-create as first admin
            if (!success) {
                // Try creating the first user (works without login when no users exist)
                std::string createSql = "CREATE USER " + username + " IDENTIFIED BY '" + password + "' ROLE ADMIN;";
                std::string createOutput;
                {
                    std::lock_guard<std::mutex> lock(executeMutex_);
                    createOutput = captureExecute(*session, createSql);
                }
                bool createOk = false;
                classifyOutput(createOutput, createOk);

                if (createOk) {
                    // Now login with the newly created user
                    {
                        std::lock_guard<std::mutex> lock(executeMutex_);
                        output = captureExecute(*session, sql);
                    }
                    type = classifyOutput(output, success);
                }
            }

            if (success) {
                std::string token = session->token;
                {
                    std::lock_guard<std::mutex> lock(sessionsMutex_);
                    sessions_[token] = std::move(session);
                }
                resp = {
                    {"success", true},
                    {"type", type},
                    {"message", output},
                    {"token", token},
                    {"currentUser", sessions_[token]->executor->getCurrentUser()},
                    {"currentDb", sessions_[token]->executor->getCurrentDb()}
                };
            } else {
                resp = {{"success", false}, {"type", "error"}, {"message", output}};
            }
        } catch (const std::exception& e) {
            resp = {{"success", false}, {"type", "error"}, {"message", std::string("Invalid request: ") + e.what()}};
        }
        res.set_content(resp.dump(), "application/json");
    });

    // POST /api/logout
    server_->Post("/api/logout", [this](const httplib::Request& req, httplib::Response& res) {
        json resp;
        try {
            auto body = json::parse(req.body);
            std::string token = body.value("token", "");

            if (token.empty()) {
                resp = {{"success", false}, {"type", "error"}, {"message", "No session token provided."}};
                res.status = 401;
                res.set_content(resp.dump(), "application/json");
                return;
            }

            Session* session = getSession(token);
            if (!session) {
                resp = {{"success", false}, {"type", "error"}, {"message", "Invalid or expired session."}};
                res.status = 401;
                res.set_content(resp.dump(), "application/json");
                return;
            }

            {
                std::lock_guard<std::mutex> lock(executeMutex_);
                captureExecute(*session, "LOGOUT;");
            }
            removeSession(token);

            resp = {{"success", true}, {"type", "info"}, {"message", "Logged out."}};
        } catch (const std::exception& e) {
            resp = {{"success", false}, {"type", "error"}, {"message", std::string("Invalid request: ") + e.what()}};
        }
        res.set_content(resp.dump(), "application/json");
    });

    // POST /api/execute
    server_->Post("/api/execute", [this](const httplib::Request& req, httplib::Response& res) {
        json resp;
        try {
            auto body = json::parse(req.body);
            std::string token = body.value("token", "");
            std::string sql   = body.value("sql", "");

            if (token.empty()) {
                resp = {{"success", false}, {"type", "error"}, {"message", "No session token provided."}};
                res.status = 401;
                res.set_content(resp.dump(), "application/json");
                return;
            }
            if (sql.empty()) {
                resp = {{"success", false}, {"type", "error"}, {"message", "Missing field: sql"}};
                res.status = 400;
                res.set_content(resp.dump(), "application/json");
                return;
            }

            Session* session = getSession(token);
            if (!session) {
                resp = {{"success", false}, {"type", "error"}, {"message", "Invalid or expired session."}};
                res.status = 401;
                res.set_content(resp.dump(), "application/json");
                return;
            }

            bool success = false;
            std::string output;
            {
                std::lock_guard<std::mutex> lock(executeMutex_);
                output = captureExecute(*session, sql);
            }
            std::string type = classifyOutput(output, success);

            resp = {
                {"success", success},
                {"type", type},
                {"message", output},
                {"currentUser", session->executor->getCurrentUser()},
                {"currentDb", session->executor->getCurrentDb()}
            };
        } catch (const std::exception& e) {
            resp = {{"success", false}, {"type", "error"}, {"message", std::string("Invalid request: ") + e.what()}};
        }
        res.set_content(resp.dump(), "application/json");
    });

    // POST /api/status
    server_->Post("/api/status", [this](const httplib::Request& req, httplib::Response& res) {
        json resp;
        try {
            auto body = json::parse(req.body);
            std::string token = body.value("token", "");

            if (token.empty()) {
                resp = {{"success", false}, {"type", "error"}, {"message", "No session token provided."}};
                res.status = 401;
                res.set_content(resp.dump(), "application/json");
                return;
            }

            Session* session = getSession(token);
            if (!session) {
                resp = {{"success", false}, {"type", "error"}, {"message", "Invalid or expired session."}};
                res.status = 401;
                res.set_content(resp.dump(), "application/json");
                return;
            }

            resp = {
                {"success", true},
                {"type", "info"},
                {"message", ""},
                {"currentUser", session->executor->getCurrentUser()},
                {"currentDb", session->executor->getCurrentDb()}
            };
        } catch (const std::exception& e) {
            resp = {{"success", false}, {"type", "error"}, {"message", std::string("Invalid request: ") + e.what()}};
        }
        res.set_content(resp.dump(), "application/json");
    });
}

// ---- Constructor / Destructor ----

ApiServer::ApiServer(int port) : port_(port), running_(false) {
    server_ = std::make_unique<httplib::Server>();
    setupRoutes();
}

ApiServer::~ApiServer() {
    stop();
}

void ApiServer::start() {
    running_ = true;
    std::cout << "MiniDB web server started on http://localhost:" << port_ << "\n";
    std::cout << "Open your browser and navigate to the address above.\n";
    server_->listen("0.0.0.0", port_);
}

void ApiServer::stop() {
    if (running_) {
        running_ = false;
        server_->stop();
    }
}
