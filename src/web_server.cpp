// WIN32_LEAN_AND_MEAN prevents <windows.h> from pulling in the old winsock.h,
// allowing cpp-httplib to include <winsock2.h> first (required on Windows).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// cpp-httplib — download single-file header from:
// https://github.com/yhirose/cpp-httplib/releases
// and place at third_party/httplib/httplib.h
//
// Phase 1: add  #define CPPHTTPLIB_OPENSSL_SUPPORT  before this include
// and link openssl libs to enable HTTPS.
#define CPPHTTPLIB_THREAD_POOL_SIZE 1   // overridden at runtime via server.new_task_queue
#include <httplib.h>

#include "web_server.h"
#include "web_assets_default.h"

#include "openai_client.h"
#include "util.h"
#include <nlohmann/json.hpp>

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#pragma comment(lib, "bcrypt.lib")

using json = nlohmann::json;
namespace fs = std::filesystem;

// ──────────────────────────────────────────────────────────────────────────────
// Pimpl struct — owns the httplib::Server so this TU is the only one that
// includes httplib.h, keeping overall compile times manageable.
// ──────────────────────────────────────────────────────────────────────────────
struct WebServerImpl {
    httplib::Server server;
};

// ──────────────────────────────────────────────────────────────────────────────
// WebServerConfig persistence
// ──────────────────────────────────────────────────────────────────────────────
WebServerConfig WebServerConfig::LoadFromFile(const fs::path& path) {
    WebServerConfig cfg;
    if (!fs::exists(path)) return cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;
    json j;
    try { f >> j; } catch (...) { return cfg; }

    cfg.auto_start              = j.value("enabled", false);
    cfg.port                    = j.value("port", 8080);
    cfg.bind_address            = j.value("bind_address", "0.0.0.0");
    cfg.base_url                = j.value("base_url", "");
    cfg.web_root                = j.value("web_root", "");
    cfg.active_theme            = j.value("active_theme", "default");
    cfg.session_timeout_minutes = j.value("session_timeout_minutes", 60);
    cfg.thread_pool_size        = j.value("thread_pool_size", 4);
    cfg.max_upload_bytes        = j.value("max_upload_mb", 50ULL) * 1024ULL * 1024ULL;
    return cfg;
}

void WebServerConfig::SaveToFile(const fs::path& path) const {
    json j = {
        {"enabled",                  auto_start},
        {"port",                     port},
        {"bind_address",             bind_address},
        {"base_url",                 base_url},
        {"web_root",                 web_root},
        {"active_theme",             active_theme},
        {"session_timeout_minutes",  session_timeout_minutes},
        {"thread_pool_size",         thread_pool_size},
        {"max_upload_mb",            max_upload_bytes / (1024ULL * 1024ULL)},
        {"tls",                      {{"mode", "self_signed"},
                                      {"cert_file", ""},
                                      {"key_file", ""},
                                      {"pfx_file", ""},
                                      {"pfx_passphrase", ""}}}
    };
    const auto tmp = fs::path(path).replace_extension(".tmp");
    { std::ofstream f(tmp); f << j.dump(2); }
    fs::rename(tmp, path);
}

// ──────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ──────────────────────────────────────────────────────────────────────────────
WebServer::WebServer(AppStorage*       storage,
                     WebUserStore*     user_store,
                     WebServerConfig   config,
                     fs::path          app_root)
    : storage_(storage)
    , user_store_(user_store)
    , config_(std::move(config))
    , app_root_(std::move(app_root))
    , impl_(std::make_unique<WebServerImpl>())
{}

WebServer::~WebServer() {
    Stop();
}

// ──────────────────────────────────────────────────────────────────────────────
// Session helpers
// ──────────────────────────────────────────────────────────────────────────────
std::string WebServer::GenerateToken() {
    unsigned char buf[32] = {};
    BCryptGenRandom(nullptr, buf, sizeof(buf), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : buf) oss << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

std::string WebServer::CreateSession(const WebUser& user,
                                      const std::string& remote_addr) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    const auto token = GenerateToken();
    Session s;
    s.user_id               = user.id;
    s.username              = user.username;
    s.force_password_reset  = user.force_password_reset;
    s.remote_addr           = remote_addr;
    s.created_at            = std::chrono::system_clock::now();
    s.last_activity         = std::chrono::steady_clock::now();
    sessions_[token] = s;
    return token;
}

std::string WebServer::GetRemoteAddr(const void* req_ptr) {
    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    return req->remote_addr;
}

std::optional<WebServer::Session> WebServer::FindSession(const std::string& token) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return std::nullopt;
    const auto age_min = std::chrono::duration_cast<std::chrono::minutes>(
        std::chrono::steady_clock::now() - it->second.last_activity).count();
    if (age_min >= config_.session_timeout_minutes) return std::nullopt;
    return it->second;
}

bool WebServer::TouchSession(const std::string& token) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return false;
    const auto age_min = std::chrono::duration_cast<std::chrono::minutes>(
        std::chrono::steady_clock::now() - it->second.last_activity).count();
    if (age_min >= config_.session_timeout_minutes) {
        sessions_.erase(it);
        return false;
    }
    it->second.last_activity = std::chrono::steady_clock::now();
    return true;
}

void WebServer::DeleteSession(const std::string& token) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.erase(token);
}

void WebServer::PurgeExpiredSessions() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    const auto now = std::chrono::steady_clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        const auto age = std::chrono::duration_cast<std::chrono::minutes>(
            now - it->second.last_activity).count();
        it = (age >= config_.session_timeout_minutes) ? sessions_.erase(it) : ++it;
    }
}

int WebServer::ActiveSessions() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return static_cast<int>(sessions_.size());
}

std::vector<SessionInfo> WebServer::GetActiveSessions() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    const auto now_steady = std::chrono::steady_clock::now();
    std::vector<SessionInfo> result;
    result.reserve(sessions_.size());
    for (const auto& [token, s] : sessions_) {
        SessionInfo info;
        info.token_prefix = token.substr(0, 8);
        info.user_id      = s.user_id;
        info.username     = s.username;
        info.remote_addr  = s.remote_addr;
        info.created_at   = s.created_at;
        info.idle_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            now_steady - s.last_activity).count();
        result.push_back(std::move(info));
    }
    return result;
}

void WebServer::ForceLogoutUser(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        if (it->second.user_id == user_id)
            it = sessions_.erase(it);
        else
            ++it;
    }
}

void WebServer::ForceLogoutAll() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.clear();
}

void WebServer::SetAuditLogPath(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(audit_mutex_);
    audit_log_path_ = path;
}

// ──────────────────────────────────────────────────────────────────────────────
// Rate limiting
// ──────────────────────────────────────────────────────────────────────────────
bool WebServer::IsRateLimited(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(rate_mutex_);
    auto it = rate_entries_.find(ip);
    if (it == rate_entries_.end()) return false;
    const auto& e = it->second;
    if (e.failures < kMaxLoginFailures) return false;
    return std::chrono::steady_clock::now() < e.lockout_until;
}

void WebServer::RecordLoginFailure(const std::string& ip) {
    std::lock_guard<std::mutex> lock(rate_mutex_);
    auto& e = rate_entries_[ip];
    ++e.failures;
    if (e.failures >= kMaxLoginFailures)
        e.lockout_until = std::chrono::steady_clock::now()
                          + std::chrono::minutes(kLockoutMinutes);
}

void WebServer::RecordLoginSuccess(const std::string& ip) {
    std::lock_guard<std::mutex> lock(rate_mutex_);
    rate_entries_.erase(ip);
}

// ──────────────────────────────────────────────────────────────────────────────
// Audit log
// ──────────────────────────────────────────────────────────────────────────────
void WebServer::AppendAuditLog(const std::string& ip,
                                const std::string& event,
                                const std::string& detail) {
    std::lock_guard<std::mutex> lock(audit_mutex_);
    if (audit_log_path_.empty()) return;
    std::ofstream f(audit_log_path_, std::ios::app);
    if (!f) return;
    char ts[32] = {};
    {
        SYSTEMTIME st = {};
        GetSystemTime(&st);
        snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);
    }
    f << ts << " [" << ip << "] " << event;
    if (!detail.empty()) f << " " << detail;
    f << "\n";
}

// Returns the value of a named cookie from a Cookie: header, or "".
std::string WebServer::GetCookieValue(const std::string& cookie_header,
                                       const std::string& name) {
    const std::string needle = name + "=";
    size_t pos = 0;
    while (pos < cookie_header.size()) {
        // Skip whitespace
        while (pos < cookie_header.size() && (cookie_header[pos] == ' ' || cookie_header[pos] == '\t'))
            ++pos;
        if (cookie_header.compare(pos, needle.size(), needle) == 0) {
            pos += needle.size();
            size_t end = cookie_header.find(';', pos);
            return cookie_header.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        }
        // Skip to next cookie
        size_t semi = cookie_header.find(';', pos);
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return {};
}

// ──────────────────────────────────────────────────────────────────────────────
// JSON / error helpers (work on httplib::Request/Response via void* casts)
// ──────────────────────────────────────────────────────────────────────────────
void WebServer::SendJson(void* res_ptr, int status, const std::string& body) {
    auto* res = static_cast<httplib::Response*>(res_ptr);
    res->status = status;
    res->set_content(body, "application/json");
}

void WebServer::SendError(void* res_ptr, int status, const std::string& message) {
    json j = {{"error", message}};
    SendJson(res_ptr, status, j.dump());
}

std::optional<WebServer::Session>
WebServer::RequireAuth(const void* req_ptr, void* res_ptr) {
    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    const auto cookie_hdr = req->get_header_value("Cookie");
    const auto token = GetCookieValue(cookie_hdr, "session");
    if (token.empty()) {
        auto* res = static_cast<httplib::Response*>(res_ptr);
        // API route → 401; page route → 302 to /login
        const bool is_api = req->path.rfind("/api/", 0) == 0;
        if (is_api) {
            SendError(res_ptr, 401, "Not authenticated");
        } else {
            res->status = 302;
            res->set_header("Location", "/login");
        }
        return std::nullopt;
    }
    auto session = FindSession(token);
    if (!session) {
        auto* res = static_cast<httplib::Response*>(res_ptr);
        const bool is_api = req->path.rfind("/api/", 0) == 0;
        if (is_api) {
            SendError(res_ptr, 401, "Session expired");
        } else {
            res->status = 302;
            res->set_header("Location", "/login");
        }
        return std::nullopt;
    }
    TouchSession(token);
    return session;
}

// ──────────────────────────────────────────────────────────────────────────────
// Web-root resolution
// ──────────────────────────────────────────────────────────────────────────────
fs::path WebServer::ResolveWebRoot() const {
    if (!config_.web_root.empty()) {
        const fs::path p(config_.web_root);
        if (fs::is_directory(p)) return p;
    }
    // Fall back to <app_root>/www
    return app_root_ / "www";
}

fs::path WebServer::ResolveThemeRoot() const {
    return ResolveWebRoot() / "themes" / config_.active_theme;
}

std::string WebServer::InjectThemePath(std::string html, const std::string& theme_path) {
    const std::string placeholder = "{{THEME_PATH}}";
    size_t pos = 0;
    while ((pos = html.find(placeholder, pos)) != std::string::npos) {
        html.replace(pos, placeholder.size(), theme_path);
        pos += theme_path.size();
    }
    return html;
}

// ──────────────────────────────────────────────────────────────────────────────
// Static file serving
// ──────────────────────────────────────────────────────────────────────────────
std::string WebServer::MimeType(const std::string& ext) {
    if (ext == ".html" || ext == ".htm")  return "text/html; charset=utf-8";
    if (ext == ".css")   return "text/css; charset=utf-8";
    if (ext == ".js")    return "application/javascript; charset=utf-8";
    if (ext == ".json")  return "application/json";
    if (ext == ".svg")   return "image/svg+xml";
    if (ext == ".png")   return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".ico")   return "image/x-icon";
    if (ext == ".woff")  return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf")   return "font/ttf";
    if (ext == ".txt")   return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

bool WebServer::ServeFile(const fs::path& abs_path, void* res_ptr) {
    auto* res = static_cast<httplib::Response*>(res_ptr);
    if (!fs::is_regular_file(abs_path)) return false;

    std::ifstream f(abs_path, std::ios::binary);
    if (!f.is_open()) return false;

    std::ostringstream oss;
    oss << f.rdbuf();
    const std::string body = oss.str();

    const std::string ext = abs_path.extension().string();
    res->status = 200;
    res->set_content(body, MimeType(ext));
    // Light caching for assets; none for HTML
    if (ext != ".html" && ext != ".htm")
        res->set_header("Cache-Control", "max-age=3600");
    else
        res->set_header("Cache-Control", "no-store");
    return true;
}

void WebServer::HandleStaticOrPage(const void* req_ptr, void* res_ptr) {
    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    auto* res       = static_cast<httplib::Response*>(res_ptr);

    const fs::path web_root = ResolveWebRoot();
    std::string url_path = req->path;

    // Normalize root → index.html
    if (url_path == "/" || url_path.empty()) url_path = "/index.html";

    // Strip leading slash and resolve under web root
    if (!url_path.empty() && url_path[0] == '/') url_path = url_path.substr(1);
    const fs::path resolved = fs::weakly_canonical(web_root / url_path);

    // Path traversal guard: resolved must still be under web_root
    const fs::path canonical_root = fs::weakly_canonical(web_root);
    const auto resolved_str = resolved.string();
    const auto root_str     = canonical_root.string();
    if (resolved_str.rfind(root_str, 0) != 0) {
        SendError(res_ptr, 403, "Forbidden");
        return;
    }

    // For HTML pages, inject theme path
    const std::string ext = resolved.extension().string();
    if ((ext == ".html" || ext == ".htm") && fs::is_regular_file(resolved)) {
        std::ifstream f(resolved);
        std::ostringstream oss; oss << f.rdbuf();
        std::string content = InjectThemePath(oss.str(), "/themes/" + config_.active_theme);
        res->status = 200;
        res->set_content(content, "text/html; charset=utf-8");
        res->set_header("Cache-Control", "no-store");
        return;
    }

    if (!ServeFile(resolved, res_ptr)) {
        res->status = 404;
        res->set_content("Not found", "text/plain");
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Auth route handlers
// ──────────────────────────────────────────────────────────────────────────────
void WebServer::HandleLogin(const void* req_ptr, void* res_ptr) {
    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    auto* res       = static_cast<httplib::Response*>(res_ptr);

    const std::string ip = GetRemoteAddr(req_ptr);

    // Rate-limit check
    if (IsRateLimited(ip)) {
        AppendAuditLog(ip, "login_blocked", "(too many failures)");
        SendError(res_ptr, 429, "Too many failed attempts — try again later"); return;
    }

    json body_j;
    try { body_j = json::parse(req->body); } catch (...) {
        SendError(res_ptr, 400, "Invalid JSON"); return;
    }

    const std::string username = body_j.value("username", "");
    const std::string password = body_j.value("password", "");
    if (username.empty() || password.empty()) {
        SendError(res_ptr, 400, "Username and password required"); return;
    }

    auto user_opt = user_store_->FindUserByUsername(username);
    if (!user_opt || !user_opt->enabled) {
        RecordLoginFailure(ip);
        AppendAuditLog(ip, "login_failed", "user=" + username);
        SendError(res_ptr, 401, "Invalid credentials"); return;
    }
    if (!user_store_->VerifyPassword(*user_opt, password)) {
        RecordLoginFailure(ip);
        AppendAuditLog(ip, "login_failed", "user=" + username);
        SendError(res_ptr, 401, "Invalid credentials"); return;
    }

    RecordLoginSuccess(ip);
    user_store_->RecordLogin(user_opt->id);
    const std::string token = CreateSession(*user_opt, ip);
    AppendAuditLog(ip, "login_success", "user=" + username);

    // Set HttpOnly session cookie (add Secure when TLS is enabled)
    const std::string cookie = "session=" + token
        + "; Path=/; HttpOnly; SameSite=Strict";
    res->set_header("Set-Cookie", cookie);

    json resp = {
        {"ok",                    true},
        {"force_password_reset",  user_opt->force_password_reset},
        {"display_name",          user_opt->display_name},
    };
    SendJson(res_ptr, 200, resp.dump());
}

void WebServer::HandleLogout(const void* req_ptr, void* res_ptr) {
    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    auto* res       = static_cast<httplib::Response*>(res_ptr);

    const auto token = GetCookieValue(req->get_header_value("Cookie"), "session");
    if (!token.empty()) {
        // find username before deleting
        auto sess = FindSession(token);
        DeleteSession(token);
        AppendAuditLog(GetRemoteAddr(req_ptr), "logout",
                       sess ? "user=" + sess->username : "");
    }

    // Expire the cookie
    res->set_header("Set-Cookie",
        "session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    res->status = 302;
    res->set_header("Location", "/login");
}

void WebServer::HandleChangePassword(const void* req_ptr, void* res_ptr) {
    auto session = RequireAuth(req_ptr, res_ptr);
    if (!session) return;

    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    json body_j;
    try { body_j = json::parse(req->body); } catch (...) {
        SendError(res_ptr, 400, "Invalid JSON"); return;
    }

    const std::string current  = body_j.value("current_password", "");
    const std::string new_pass = body_j.value("new_password", "");
    if (new_pass.size() < 8) {
        SendError(res_ptr, 400, "Password must be at least 8 characters"); return;
    }

    auto user_opt = user_store_->FindUserById(session->user_id);
    if (!user_opt) { SendError(res_ptr, 404, "User not found"); return; }

    // If not a forced reset, verify the current password
    if (!session->force_password_reset) {
        if (!user_store_->VerifyPassword(*user_opt, current)) {
            SendError(res_ptr, 401, "Current password incorrect"); return;
        }
    }

    user_store_->SetPassword(session->user_id, new_pass);

    // Update the session to clear the force_password_reset flag
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        const auto token = GetCookieValue(
            static_cast<const httplib::Request*>(req_ptr)->get_header_value("Cookie"), "session");
        auto it = sessions_.find(token);
        if (it != sessions_.end()) it->second.force_password_reset = false;
    }

    SendJson(res_ptr, 200, R"({"ok":true})");
}

// ──────────────────────────────────────────────────────────────────────────────
// Project / chat / message API handlers
// ──────────────────────────────────────────────────────────────────────────────
void WebServer::HandleGetProjects(const void* req_ptr, void* res_ptr) {
    auto session = RequireAuth(req_ptr, res_ptr);
    if (!session) return;

    const auto accessible = user_store_->GetProjectIdsForUser(session->user_id);
    const auto all_projects = storage_->LoadProjects();

    json arr = json::array();
    for (const auto& proj : all_projects) {
        if (std::find(accessible.begin(), accessible.end(), proj.info.id) != accessible.end()) {
            arr.push_back({{"id", proj.info.id}, {"name", proj.info.name}});
        }
    }
    SendJson(res_ptr, 200, arr.dump());
}

void WebServer::HandleGetChats(const void* req_ptr, void* res_ptr) {
    auto session = RequireAuth(req_ptr, res_ptr);
    if (!session) return;

    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    const std::string project_id = req->path_params.at("id");

    // Verify access
    const auto accessible = user_store_->GetProjectIdsForUser(session->user_id);
    if (std::find(accessible.begin(), accessible.end(), project_id) == accessible.end()) {
        SendError(res_ptr, 403, "Access denied"); return;
    }

    const auto all_projects = storage_->LoadProjects();
    json arr = json::array();
    for (const auto& proj : all_projects) {
        if (proj.info.id != project_id) continue;
        for (const auto& chat : proj.chats) {
            // Only return this user's chats (name ends with " [username]")
            const std::string suffix = " [" + session->username + "]";
            if (chat.name.size() >= suffix.size() &&
                chat.name.compare(chat.name.size() - suffix.size(),
                                  suffix.size(), suffix) == 0) {
                arr.push_back({{"id", chat.id}, {"name", chat.name}});
            }
        }
        break;
    }
    SendJson(res_ptr, 200, arr.dump());
}

void WebServer::HandleCreateChat(const void* req_ptr, void* res_ptr) {
    auto session = RequireAuth(req_ptr, res_ptr);
    if (!session) return;

    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    const std::string project_id = req->path_params.at("id");

    const auto accessible = user_store_->GetProjectIdsForUser(session->user_id);
    if (std::find(accessible.begin(), accessible.end(), project_id) == accessible.end()) {
        SendError(res_ptr, 403, "Access denied"); return;
    }

    json body_j;
    try { body_j = json::parse(req->body); } catch (...) {
        SendError(res_ptr, 400, "Invalid JSON"); return;
    }

    std::string name = body_j.value("name", "New Chat");
    if (name.empty()) name = "New Chat";
    // Append [username] suffix unconditionally
    name += " [" + session->username + "]";

    const auto chat = storage_->CreateChat(project_id, name, "", "");
    AppendAuditLog(GetRemoteAddr(req_ptr), "chat_created",
                   "user=" + session->username + " project=" + project_id
                   + " chat=" + chat.id);
    json resp = {{"id", chat.id}, {"name", chat.name}};
    SendJson(res_ptr, 201, resp.dump());
}

void WebServer::HandleDeleteChat(const void* req_ptr, void* res_ptr) {
    auto session = RequireAuth(req_ptr, res_ptr);
    if (!session) return;

    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    const std::string chat_id = req->path_params.at("id");

    // Find the project that owns this chat; verify it belongs to this user
    const auto all_projects = storage_->LoadProjects();
    const std::string suffix = " [" + session->username + "]";
    for (const auto& proj : all_projects) {
        for (const auto& chat : proj.chats) {
            if (chat.id != chat_id) continue;
            // Ownership check
            if (chat.name.size() < suffix.size() ||
                chat.name.compare(chat.name.size() - suffix.size(),
                                  suffix.size(), suffix) != 0) {
                SendError(res_ptr, 403, "Cannot delete another user's chat"); return;
            }
            storage_->DeleteChat(proj.info.id, chat_id);
            AppendAuditLog(GetRemoteAddr(req_ptr), "chat_deleted",
                           "user=" + session->username + " chat=" + chat_id);
            SendJson(res_ptr, 200, R"({"ok":true})");
            return;
        }
    }
    SendError(res_ptr, 404, "Chat not found");
}

// ──────────────────────────────────────────────────────────────────────────────
// HandleRenameChat — PATCH /api/chats/:id
// Body: { "name": "new title" }
// ──────────────────────────────────────────────────────────────────────────────
void WebServer::HandleRenameChat(const void* req_ptr, void* res_ptr) {
    auto session = RequireAuth(req_ptr, res_ptr);
    if (!session) return;

    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    const std::string chat_id = req->path_params.at("id");

    json body_j;
    try { body_j = json::parse(req->body); } catch (...) {
        SendError(res_ptr, 400, "Invalid JSON"); return;
    }
    std::string new_name = body_j.value("name", "");
    if (new_name.empty()) { SendError(res_ptr, 400, "Name required"); return; }
    // Strip any [username] suffix the caller may have included, then re-append
    {
        const std::string suf = " [";
        const auto pos = new_name.rfind(suf);
        if (pos != std::string::npos) new_name = new_name.substr(0, pos);
    }
    new_name = new_name + " [" + session->username + "]";

    // Find owning project; verify ownership
    const auto all_projects = storage_->LoadProjects();
    for (const auto& proj : all_projects) {
        for (const auto& chat : proj.chats) {
            if (chat.id != chat_id) continue;
            // Verify this chat belongs to the requesting user
            const std::string suffix = " [" + session->username + "]";
            if (chat.name.size() < suffix.size() ||
                chat.name.compare(chat.name.size() - suffix.size(),
                                  suffix.size(), suffix) != 0) {
                SendError(res_ptr, 403, "Cannot rename another user's chat"); return;
            }
            storage_->RenameChat(proj.info.id, chat_id, new_name);
            AppendAuditLog(GetRemoteAddr(req_ptr), "chat_renamed",
                           "user=" + session->username + " chat=" + chat_id
                           + " -> " + new_name);
            json resp = {{"id", chat_id}, {"name", new_name}};
            SendJson(res_ptr, 200, resp.dump());
            return;
        }
    }
    SendError(res_ptr, 404, "Chat not found");
}

void WebServer::HandleGetMessages(const void* req_ptr, void* res_ptr) {
    auto session = RequireAuth(req_ptr, res_ptr);
    if (!session) return;

    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    const std::string chat_id = req->path_params.at("id");

    // Find owning project
    const auto all_projects = storage_->LoadProjects();
    for (const auto& proj : all_projects) {
        for (const auto& chat : proj.chats) {
            if (chat.id != chat_id) continue;
            const auto messages = storage_->LoadMessages(proj.info.id, chat_id);
            json arr = json::array();
            for (const auto& m : messages) {
                arr.push_back({{"role", m.role}, {"content", m.content},
                               {"created_at", m.created_at}});
            }
            SendJson(res_ptr, 200, arr.dump());
            return;
        }
    }
    SendError(res_ptr, 404, "Chat not found");
}

// ──────────────────────────────────────────────────────────────────────────────
// Attachment injection helper
// ──────────────────────────────────────────────────────────────────────────────
std::string WebServer::BuildUserContentWithAttachments(
    const std::string& project_id,
    const std::string& chat_id,
    const std::string& user_content,
    const std::vector<std::string>& attachments) const
{
    if (attachments.empty()) return user_content;

    const fs::path upload_dir = app_root_ / "uploads" / project_id / chat_id;

    std::string combined = user_content;

    for (const auto& filename : attachments) {
        // Security: reject any filename that contains a path separator or ".."
        if (filename.find('/') != std::string::npos ||
            filename.find('\\') != std::string::npos ||
            filename.find("..") != std::string::npos) {
            continue;
        }

        const fs::path file_path = upload_dir / filename;
        if (!fs::is_regular_file(file_path)) continue;

        // Determine how to read: text vs binary
        const std::string ext = file_path.extension().string();
        // Treat common text formats as UTF-8 text; everything else as base64
        static const std::vector<std::string> kTextExts = {
            ".txt", ".md", ".markdown", ".csv", ".json", ".xml", ".yaml", ".yml",
            ".html", ".htm", ".css", ".js", ".ts", ".py", ".cpp", ".c", ".h",
            ".java", ".rs", ".go", ".sh", ".bat", ".ps1", ".log", ".ini", ".cfg",
            ".toml", ".sql", ".r", ".m", ".rb", ".php", ".swift",
        };
        bool is_text = std::any_of(kTextExts.begin(), kTextExts.end(),
            [&](const std::string& e) {
                if (ext.size() != e.size()) return false;
                for (size_t i = 0; i < ext.size(); ++i)
                    if (std::tolower(static_cast<unsigned char>(ext[i])) !=
                        std::tolower(static_cast<unsigned char>(e[i]))) return false;
                return true;
            });

        combined += "\n\n---\nAttached file: " + filename + "\n";

        if (is_text) {
            std::ifstream ifs(file_path);
            if (!ifs) { combined += "[Error: could not read file]\n"; continue; }
            std::ostringstream oss;
            oss << ifs.rdbuf();
            std::string text = oss.str();
            // Truncate very large files to avoid blowing the context window
            constexpr size_t kMaxTextBytes = 128 * 1024;
            if (text.size() > kMaxTextBytes) {
                text.resize(kMaxTextBytes);
                text += "\n[... truncated at 128 KB ...]";
            }
            combined += text;
        } else {
            // Binary file: note it exists but don't embed raw bytes
            const auto file_size = fs::file_size(file_path);
            combined += "[Binary file, " + std::to_string(file_size)
                     + " bytes — content not embedded]\n";
        }
    }

    return combined;
}

// ──────────────────────────────────────────────────────────────────────────────
// Model call helper
// ──────────────────────────────────────────────────────────────────────────────
static std::string NowIso() {
    SYSTEMTIME st = {};
    GetSystemTime(&st);
    char buf[32] = {};
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    return buf;
}

WebServer::ModelCallResult
WebServer::CallModel(const std::string& project_id,
                     const std::string& chat_id,
                     const std::string& user_content,
                     const std::string& username,
                     const std::vector<std::string>& attachments) {
    ModelCallResult result;

    // Load providers
    const auto providers = storage_->LoadProviders();
    if (providers.empty()) {
        result.error = "No AI providers configured.";
        return result;
    }

    // Load project settings to get preferred provider/model and instructions
    const auto proj_settings = storage_->LoadProjectSettings(project_id);

    // Select provider and model
    ProviderConfig selected_provider;
    ModelConfig    selected_model;
    bool found = false;

    if (!proj_settings.preferred_provider_id.empty() &&
        !proj_settings.preferred_model_id.empty()) {
        for (const auto& p : providers) {
            if (p.id != proj_settings.preferred_provider_id) continue;
            for (const auto& m : p.models) {
                if (m.id == proj_settings.preferred_model_id) {
                    selected_provider = p;
                    selected_model    = m;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
    }
    if (!found && !providers.empty()) {
        selected_provider = providers.front();
        if (!selected_provider.models.empty())
            selected_model = selected_provider.models.front();
    }
    if (selected_model.id.empty()) {
        result.error = "No AI model configured.";
        return result;
    }

    // Build system prompt: project instructions with $<VarName> substitution
    std::string system_prompt;
    {
        std::string instr = proj_settings.project_instructions;
        for (const auto& pv : proj_settings.project_variables) {
            if (pv.name.empty()) continue;
            const std::string ph = "$<" + pv.name + ">";
            size_t pos = 0;
            while ((pos = instr.find(ph, pos)) != std::string::npos) {
                const size_t after = pos + ph.size();
                // Trailing-$ guard: skip $<Name>$ MCP binding tokens
                if (after < instr.size() && instr[after] == '$') { ++pos; continue; }
                instr.replace(pos, ph.size(), pv.value);
                pos += pv.value.size();
            }
        }
        // Also inject $<UserName>
        {
            const std::string ph = "$<UserName>";
            size_t pos = 0;
            while ((pos = instr.find(ph, pos)) != std::string::npos) {
                instr.replace(pos, ph.size(), username);
                pos += username.size();
            }
        }
        if (!instr.empty()) system_prompt = "Project Instructions:\n" + instr;
    }

    // Load existing message history
    auto messages = storage_->LoadMessages(project_id, chat_id);

    // Append the new user message (with any attached file content injected)
    MessageRecord user_msg;
    user_msg.role       = "user";
    user_msg.content    = BuildUserContentWithAttachments(
                              project_id, chat_id, user_content, attachments);
    user_msg.created_at = NowIso();
    messages.push_back(user_msg);

    // Build request
    ChatRequestOptions opts;
    opts.provider     = selected_provider;
    opts.model        = selected_model;
    opts.system_prompt = system_prompt;
    opts.temperature  = 0.2;
    opts.max_tokens   = 1024;
    opts.messages     = messages;

    // Call model (synchronous, no streaming for Phase 0)
    const auto call_result = OpenAIClient::CreateSimpleCompletion(opts);

    if (!call_result.success) {
        result.error = call_result.error.empty() ? "Model call failed" : call_result.error;
        // Still save the user message so the chat shows it
        storage_->SaveMessages(project_id, chat_id, messages);
        return result;
    }

    // Append assistant response
    MessageRecord asst_msg;
    asst_msg.role       = "assistant";
    asst_msg.content    = call_result.assistant_text;
    asst_msg.created_at = NowIso();
    messages.push_back(asst_msg);

    // Persist
    storage_->SaveMessages(project_id, chat_id, messages);

    result.success        = true;
    result.assistant_text = call_result.assistant_text;
    return result;
}

void WebServer::HandleSendMessage(const void* req_ptr, void* res_ptr) {
    auto session = RequireAuth(req_ptr, res_ptr);
    if (!session) return;

    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    const std::string chat_id = req->path_params.at("id");

    json body_j;
    try { body_j = json::parse(req->body); } catch (...) {
        SendError(res_ptr, 400, "Invalid JSON"); return;
    }
    const std::string content = body_j.value("content", "");
    if (content.empty()) {
        SendError(res_ptr, 400, "Message content required"); return;
    }

    // Find owning project and verify user access
    const auto accessible  = user_store_->GetProjectIdsForUser(session->user_id);
    const auto all_projects = storage_->LoadProjects();
    std::string project_id;
    for (const auto& proj : all_projects) {
        for (const auto& chat : proj.chats) {
            if (chat.id == chat_id) { project_id = proj.info.id; break; }
        }
        if (!project_id.empty()) break;
    }
    if (project_id.empty()) { SendError(res_ptr, 404, "Chat not found"); return; }
    if (std::find(accessible.begin(), accessible.end(), project_id) == accessible.end()) {
        SendError(res_ptr, 403, "Access denied"); return;
    }

    // Call model (may block for several seconds)
    const auto model_result = CallModel(project_id, chat_id, content, session->username);

    if (!model_result.success) {
        json err = {{"error", model_result.error}};
        SendJson(res_ptr, 502, err.dump());
        return;
    }

    json resp = {
        {"role",    "assistant"},
        {"content", model_result.assistant_text},
    };
    SendJson(res_ptr, 200, resp.dump());
}

// ──────────────────────────────────────────────────────────────────────────────
// StreamModel — streaming variant of CallModel.
// Saves the user message first, streams assistant tokens via on_delta, then
// saves the completed assistant message.  Returns an error string or "".
// ──────────────────────────────────────────────────────────────────────────────
std::string WebServer::StreamModel(const std::string& project_id,
                                   const std::string& chat_id,
                                   const std::string& user_content,
                                   const std::string& username,
                                   const std::function<bool(const std::string&)>& on_delta,
                                   const std::vector<std::string>& attachments)
{
    // Load providers
    const auto providers = storage_->LoadProviders();
    if (providers.empty()) return "No AI providers configured.";

    const auto proj_settings = storage_->LoadProjectSettings(project_id);

    // Select provider/model (same logic as CallModel)
    ProviderConfig selected_provider;
    ModelConfig    selected_model;
    bool found = false;
    if (!proj_settings.preferred_provider_id.empty() &&
        !proj_settings.preferred_model_id.empty()) {
        for (const auto& p : providers) {
            if (p.id != proj_settings.preferred_provider_id) continue;
            for (const auto& m : p.models) {
                if (m.id == proj_settings.preferred_model_id) {
                    selected_provider = p; selected_model = m; found = true; break;
                }
            }
            if (found) break;
        }
    }
    if (!found && !providers.empty()) {
        selected_provider = providers.front();
        if (!selected_provider.models.empty()) selected_model = selected_provider.models.front();
    }
    if (selected_model.id.empty()) return "No AI model configured.";

    // Build system prompt with variable substitution
    std::string system_prompt;
    {
        std::string instr = proj_settings.project_instructions;
        for (const auto& pv : proj_settings.project_variables) {
            if (pv.name.empty()) continue;
            const std::string ph = "$<" + pv.name + ">";
            size_t pos = 0;
            while ((pos = instr.find(ph, pos)) != std::string::npos) {
                const size_t after = pos + ph.size();
                if (after < instr.size() && instr[after] == '$') { ++pos; continue; }
                instr.replace(pos, ph.size(), pv.value);
                pos += pv.value.size();
            }
        }
        {
            const std::string ph = "$<UserName>";
            size_t pos = 0;
            while ((pos = instr.find(ph, pos)) != std::string::npos) {
                instr.replace(pos, ph.size(), username);
                pos += username.size();
            }
        }
        if (!instr.empty()) system_prompt = "Project Instructions:\n" + instr;
    }

    // Load history and append user message (with any attached file content injected)
    auto messages = storage_->LoadMessages(project_id, chat_id);
    MessageRecord user_msg;
    user_msg.role       = "user";
    user_msg.content    = BuildUserContentWithAttachments(
                              project_id, chat_id, user_content, attachments);
    user_msg.created_at = NowIso();
    messages.push_back(user_msg);

    // Persist user message immediately so it's visible if the stream aborts
    storage_->SaveMessages(project_id, chat_id, messages);

    // Build request (streaming = true)
    ChatRequestOptions opts;
    opts.provider      = selected_provider;
    opts.model         = selected_model;
    opts.system_prompt = system_prompt;
    opts.temperature   = 0.2;
    opts.max_tokens    = 4096;  // allow longer outputs when streaming
    opts.messages      = messages;

    // Accumulate full text so we can save it at the end
    std::string accumulated;
    bool aborted = false;

    const auto stream_result = OpenAIClient::StreamChat(opts,
        [&](const std::string& delta) {
            accumulated += delta;
            // on_delta returns false to signal the client disconnected
            if (!on_delta(delta)) {
                aborted = true;
            }
        });

    if (!stream_result.success && !aborted) {
        // Nothing to save; return the error
        return stream_result.error.empty() ? "Streaming model call failed." : stream_result.error;
    }

    // Save completed assistant message (even if aborted mid-stream, save what we got)
    if (!accumulated.empty()) {
        MessageRecord asst_msg;
        asst_msg.role       = "assistant";
        asst_msg.content    = accumulated;
        asst_msg.created_at = NowIso();
        messages.push_back(asst_msg);
        storage_->SaveMessages(project_id, chat_id, messages);
    }

    return {};  // success
}

// ──────────────────────────────────────────────────────────────────────────────
// HandleStreamMessage — POST /api/chats/:id/messages/stream
//
// Opens a true Server-Sent Events (SSE) response using httplib's chunked
// content provider.  The model runs in a background thread; each token chunk
// is enqueued into a shared buffer and immediately flushed to the client:
//
//   data: {"delta":"<text>"}\n\n   — one per token chunk
//   data: {"done":true}\n\n        — final event on success
//   data: {"error":"<msg>"}\n\n    — final event on failure
// ──────────────────────────────────────────────────────────────────────────────
void WebServer::HandleStreamMessage(const void* req_ptr, void* res_ptr) {
    auto session = RequireAuth(req_ptr, res_ptr);
    if (!session) return;

    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    auto*       res = static_cast<httplib::Response*>(res_ptr);

    const std::string chat_id = req->path_params.at("id");

    json body_j;
    try { body_j = json::parse(req->body); } catch (...) {
        SendError(res_ptr, 400, "Invalid JSON"); return;
    }
    const std::string content = body_j.value("content", "");
    if (content.empty()) { SendError(res_ptr, 400, "Message content required"); return; }

    // Optional list of already-uploaded filenames to inject into the message context
    std::vector<std::string> attachments;
    if (body_j.contains("attachments") && body_j["attachments"].is_array()) {
        for (const auto& a : body_j["attachments"]) {
            if (a.is_string()) attachments.push_back(a.get<std::string>());
        }
    }

    // Verify access
    const auto accessible   = user_store_->GetProjectIdsForUser(session->user_id);
    const auto all_projects = storage_->LoadProjects();
    std::string project_id;
    for (const auto& proj : all_projects) {
        for (const auto& chat : proj.chats) {
            if (chat.id == chat_id) { project_id = proj.info.id; break; }
        }
        if (!project_id.empty()) break;
    }
    if (project_id.empty()) { SendError(res_ptr, 404, "Chat not found"); return; }
    if (std::find(accessible.begin(), accessible.end(), project_id) == accessible.end()) {
        SendError(res_ptr, 403, "Access denied"); return;
    }

    // ── Shared state between producer (model thread) and consumer (HTTP thread) ──
    struct StreamPipe {
        std::mutex              mtx;
        std::condition_variable cv;
        std::string             pending;      // queued SSE text to flush
        bool                    done = false; // producer finished
        bool                    abort= false; // consumer disconnected
    };
    auto pipe = std::make_shared<StreamPipe>();

    auto encode_sse = [](const std::string& json_str) -> std::string {
        return "data: " + json_str + "\n\n";
    };

    const std::string proj_id  = project_id;
    const std::string sess_user = session->username;

    // ── Producer thread — runs the model, enqueues SSE events ─────────────────
    std::thread producer([this, pipe, proj_id, chat_id, content, sess_user,
                          encode_sse, attachments]() {
        const std::string err = StreamModel(
            proj_id, chat_id, content, sess_user,
            [&](const std::string& delta) -> bool {
                {
                    std::lock_guard<std::mutex> lk(pipe->mtx);
                    if (pipe->abort) return false;
                    json ev = {{"delta", delta}};
                    pipe->pending += encode_sse(ev.dump());
                }
                pipe->cv.notify_one();
                return true;
            },
            attachments);

        // Enqueue final event
        {
            std::lock_guard<std::mutex> lk(pipe->mtx);
            if (!err.empty()) {
                json ev = {{"error", err}};
                pipe->pending += encode_sse(ev.dump());
            } else {
                pipe->pending += encode_sse(R"({"done":true})");
            }
            pipe->done = true;
        }
        pipe->cv.notify_one();
    });
    producer.detach();  // HTTP content provider below drives lifetime

    // ── Consumer: httplib chunked content provider ─────────────────────────────
    res->set_header("Cache-Control", "no-cache");
    res->set_header("X-Accel-Buffering", "no");

    res->set_chunked_content_provider(
        "text/event-stream; charset=utf-8",
        [pipe](size_t /*offset*/, httplib::DataSink& sink) -> bool {
            std::unique_lock<std::mutex> lk(pipe->mtx);
            // Wait until there's data or the producer is done
            pipe->cv.wait_for(lk, std::chrono::milliseconds(200),
                [&] { return !pipe->pending.empty() || pipe->done; });

            if (!pipe->pending.empty()) {
                std::string chunk;
                std::swap(chunk, pipe->pending);
                lk.unlock();
                if (!sink.write(chunk.c_str(), chunk.size())) {
                    // Client disconnected
                    std::lock_guard<std::mutex> lg(pipe->mtx);
                    pipe->abort = true;
                    return false;
                }
                return true;
            }

            if (pipe->done) {
                sink.done();
                return false;   // close the response
            }

            return true;  // keep-alive poll
        });
}

// ──────────────────────────────────────────────────────────────────────────────
// HandleUpload — POST /api/chats/:id/upload
//
// Accepts a multipart/form-data upload with a "file" field.
// Saves the file to the project's output folder (or a temp folder) and returns
// a JSON object with the saved filename and size.
//
// The uploaded content is currently stored as a blob in the chat's storage
// folder so it can be referenced in future messages.
// ──────────────────────────────────────────────────────────────────────────────
void WebServer::HandleUpload(const void* req_ptr, void* res_ptr) {
    auto session = RequireAuth(req_ptr, res_ptr);
    if (!session) return;

    const auto* req = static_cast<const httplib::Request*>(req_ptr);
    const std::string chat_id = req->path_params.at("id");

    // Find owning project
    const auto accessible   = user_store_->GetProjectIdsForUser(session->user_id);
    const auto all_projects = storage_->LoadProjects();
    std::string project_id;
    for (const auto& proj : all_projects) {
        for (const auto& chat : proj.chats) {
            if (chat.id == chat_id) { project_id = proj.info.id; break; }
        }
        if (!project_id.empty()) break;
    }
    if (project_id.empty()) { SendError(res_ptr, 404, "Chat not found"); return; }
    if (std::find(accessible.begin(), accessible.end(), project_id) == accessible.end()) {
        SendError(res_ptr, 403, "Access denied"); return;
    }

    // Find "file" field in multipart
    auto it = req->files.find("file");
    if (it == req->files.end()) {
        SendError(res_ptr, 400, "No 'file' field in upload"); return;
    }
    const auto& file_info = it->second;

    // Enforce upload size limit
    if (file_info.content.size() > config_.max_upload_bytes) {
        SendError(res_ptr, 413, "File too large"); return;
    }

    // Sanitize filename: strip directory components and keep alphanumeric + common safe chars
    std::string safe_name = file_info.filename;
    {
        // Remove path separators
        const auto pos = safe_name.find_last_of("/\\");
        if (pos != std::string::npos) safe_name = safe_name.substr(pos + 1);
        // Replace anything that isn't alphanumeric, '.', '-', '_', ' '
        for (auto& c : safe_name)
            if (!std::isalnum(static_cast<unsigned char>(c)) &&
                c != '.' && c != '-' && c != '_' && c != ' ')
                c = '_';
        if (safe_name.empty()) safe_name = "upload";
    }

    // Destination: <app_root>/uploads/<project_id>/<chat_id>/<safe_name>
    const fs::path upload_dir = app_root_ / "uploads" / project_id / chat_id;
    std::error_code ec;
    fs::create_directories(upload_dir, ec);
    if (ec) { SendError(res_ptr, 500, "Could not create upload directory"); return; }

    // Avoid overwriting: if the file exists, append a counter
    fs::path dest = upload_dir / safe_name;
    if (fs::exists(dest)) {
        const std::string stem = dest.stem().string();
        const std::string ext  = dest.extension().string();
        int counter = 1;
        do {
            dest = upload_dir / (stem + "_" + std::to_string(counter++) + ext);
        } while (fs::exists(dest) && counter < 1000);
    }

    // Write file
    std::ofstream ofs(dest, std::ios::binary);
    if (!ofs) { SendError(res_ptr, 500, "Failed to write upload"); return; }
    ofs.write(file_info.content.data(), static_cast<std::streamsize>(file_info.content.size()));
    ofs.close();

    AppendAuditLog(GetRemoteAddr(req_ptr), "upload",
                   session->username + " -> " + dest.filename().string());

    json resp = {
        {"filename",   dest.filename().string()},
        {"size",       file_info.content.size()},
        {"chat_id",    chat_id},
        {"project_id", project_id},
    };
    SendJson(res_ptr, 200, resp.dump());
}

// ──────────────────────────────────────────────────────────────────────────────
// Route registration
// ──────────────────────────────────────────────────────────────────────────────
void WebServer::RegisterRoutes() {
    auto& srv = impl_->server;

    // Security headers on every response
    srv.set_default_headers({
        {"X-Frame-Options",        "DENY"},
        {"X-Content-Type-Options", "nosniff"},
    });

    // ── Auth ──────────────────────────────────────────────────────────────
    srv.Post("/login", [this](const httplib::Request& req, httplib::Response& res) {
        HandleLogin(&req, &res);
    });
    srv.Get("/logout", [this](const httplib::Request& req, httplib::Response& res) {
        HandleLogout(&req, &res);
    });
    srv.Post("/api/change-password", [this](const httplib::Request& req, httplib::Response& res) {
        HandleChangePassword(&req, &res);
    });

    // ── API ───────────────────────────────────────────────────────────────
    srv.Get("/api/me", [this](const httplib::Request& req, httplib::Response& res) {
        auto session = RequireAuth(&req, &res);
        if (!session) return;
        auto user_opt = user_store_->FindUserById(session->user_id);
        if (!user_opt) { SendError(&res, 404, "User not found"); return; }
        json resp = {
            {"username",     user_opt->username},
            {"display_name", user_opt->display_name.empty()
                             ? user_opt->username : user_opt->display_name},
        };
        SendJson(&res, 200, resp.dump());
    });
    srv.Get("/api/projects", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetProjects(&req, &res);
    });
    srv.Get(R"(/api/projects/([^/]+)/chats)", [this](const httplib::Request& req, httplib::Response& res) {
        // httplib captures go into req.matches; re-expose as path_params-style
        const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1].str();
        HandleGetChats(&req, &res);
    });
    srv.Post(R"(/api/projects/([^/]+)/chats)", [this](const httplib::Request& req, httplib::Response& res) {
        const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1].str();
        HandleCreateChat(&req, &res);
    });
    srv.Delete(R"(/api/chats/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1].str();
        HandleDeleteChat(&req, &res);
    });
    srv.Patch(R"(/api/chats/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1].str();
        HandleRenameChat(&req, &res);
    });
    srv.Get(R"(/api/chats/([^/]+)/messages)", [this](const httplib::Request& req, httplib::Response& res) {
        const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1].str();
        HandleGetMessages(&req, &res);
    });
    srv.Post(R"(/api/chats/([^/]+)/messages)", [this](const httplib::Request& req, httplib::Response& res) {
        const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1].str();
        HandleSendMessage(&req, &res);
    });
    // SSE streaming variant — client sends same body but receives token-by-token
    srv.Post(R"(/api/chats/([^/]+)/messages/stream)", [this](const httplib::Request& req, httplib::Response& res) {
        const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1].str();
        HandleStreamMessage(&req, &res);
    });
    // File upload
    srv.Post(R"(/api/chats/([^/]+)/upload)", [this](const httplib::Request& req, httplib::Response& res) {
        const_cast<httplib::Request&>(req).path_params["id"] = req.matches[1].str();
        HandleUpload(&req, &res);
    });

    // ── Static files & HTML pages ─────────────────────────────────────────
    // login.html served without auth check
    srv.Get("/login", [this](const httplib::Request& req, httplib::Response& res) {
        const auto f = ResolveWebRoot() / "login.html";
        if (fs::is_regular_file(f)) {
            std::ifstream fi(f); std::ostringstream oss; oss << fi.rdbuf();
            const std::string content = InjectThemePath(oss.str(),
                "/themes/" + config_.active_theme);
            res.status = 200;
            res.set_content(content, "text/html; charset=utf-8");
            res.set_header("Cache-Control", "no-store");
        } else {
            ServeFile(ResolveWebRoot() / "login.html", &res);
        }
    });
    srv.Get("/change-password", [this](const httplib::Request& req, httplib::Response& res) {
        const auto f = ResolveWebRoot() / "change-password.html";
        if (fs::is_regular_file(f)) {
            std::ifstream fi(f); std::ostringstream oss; oss << fi.rdbuf();
            const std::string content = InjectThemePath(oss.str(),
                "/themes/" + config_.active_theme);
            res.status = 200;
            res.set_content(content, "text/html; charset=utf-8");
            res.set_header("Cache-Control", "no-store");
        } else {
            ServeFile(ResolveWebRoot() / "change-password.html", &res);
        }
    });

    // Catch-all for static assets and the SPA root
    srv.Get(R"(.*)", [this](const httplib::Request& req, httplib::Response& res) {
        HandleStaticOrPage(&req, &res);
    });
}

// ──────────────────────────────────────────────────────────────────────────────
// Default web-asset bootstrapping
// Writes default files to web_root only if they are missing.  Never overwrites.
// ──────────────────────────────────────────────────────────────────────────────
void WebServer::EnsureDefaultWebAssets() const {
    struct Asset { const char* rel_path; const char* content; };
    static const Asset kAssets[] = {
        { "index.html",                    DefaultWebAssets::kIndexHtml          },
        { "login.html",                    DefaultWebAssets::kLoginHtml          },
        { "change-password.html",          DefaultWebAssets::kChangePasswordHtml },
        { "css/base.css",                  DefaultWebAssets::kBaseCss            },
        { "js/app.js",                     DefaultWebAssets::kAppJs              },
        { "themes/default/style.css",      DefaultWebAssets::kThemeDefaultCss    },
        { "themes/default/theme.json",     DefaultWebAssets::kThemeDefaultJson   },
    };

    const fs::path root = ResolveWebRoot();
    for (const auto& a : kAssets) {
        const fs::path dest = root / a.rel_path;
        if (fs::exists(dest)) continue;              // never overwrite existing files
        fs::create_directories(dest.parent_path());  // ensure dirs exist
        std::ofstream f(dest);
        if (f) f << a.content;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Start / Stop
// ──────────────────────────────────────────────────────────────────────────────
bool WebServer::Start() {
    if (running_.load()) return true;

    // Bootstrap any missing default web assets before binding the port
    EnsureDefaultWebAssets();

    // Recreate the impl (allows restart after Stop)
    impl_ = std::make_unique<WebServerImpl>();
    RegisterRoutes();

    impl_->server.new_task_queue = [this]() -> httplib::TaskQueue* {
        return new httplib::ThreadPool(
            static_cast<size_t>(std::max(1, config_.thread_pool_size)));
    };

    // Bind first to detect port conflicts before launching thread
    if (!impl_->server.bind_to_port(config_.bind_address.c_str(), config_.port)) {
        return false;
    }

    running_.store(true);
    server_thread_ = std::thread([this]() {
        impl_->server.listen_after_bind();
        running_.store(false);
    });

    return true;
}

void WebServer::Stop() {
    if (!running_.load()) return;
    impl_->server.stop();
    if (server_thread_.joinable()) server_thread_.join();
    running_.store(false);
    PurgeExpiredSessions();
}

void WebServer::Reconfigure(const WebServerConfig& new_config) {
    const bool was_running = IsRunning();
    if (was_running) Stop();
    config_ = new_config;
    if (was_running) Start();
}
