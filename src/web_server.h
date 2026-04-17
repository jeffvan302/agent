#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// WebServer — embedded HTTP server for multi-user web chat access.
//
// Phase 0: HTTP only (no TLS).  HTTPS / TLS via OpenSSL will be added in
// Phase 1 by defining CPPHTTPLIB_OPENSSL_SUPPORT and linking openssl libs.
//
// The server uses cpp-httplib (header-only).  That header is large, so it is
// included only in web_server.cpp via a pimpl struct (WebServerImpl) to keep
// compile times acceptable for the rest of the translation units.
//
// Dependency: third_party/httplib/httplib.h must exist.  Download the latest
// single-file release from https://github.com/yhirose/cpp-httplib/releases
// and drop it at that path before building.
// ──────────────────────────────────────────────────────────────────────────────

#include "storage.h"
#include "web_user_store.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class McpManager;
class RagService;

// ──────────────────────────────────────────────────────────────────────────────
// Configuration — loaded from / saved to web_settings.json
// ──────────────────────────────────────────────────────────────────────────────
struct WebServerConfig {
    bool     auto_start          = false;
    int      port                = 8080;    // HTTP default; use 8443 for HTTPS
    std::string bind_address     = "0.0.0.0";
    std::string base_url;                   // e.g. "https://192.168.1.10:8443"
    std::string web_root;                   // absolute path; empty = auto-detect www/
    std::string active_theme     = "default";
    int      session_timeout_minutes = 60;
    int      thread_pool_size    = 4;
    size_t   max_upload_bytes    = 50ULL * 1024 * 1024;  // 50 MB

    // ── TLS / HTTPS ───────────────────────────────────────────────────────────
    // tls_mode: ""            = plain HTTP (default)
    //           "self_signed" = auto-generate cert/key under <app_root>/certs/
    //           "pem"         = load tls_cert_file + tls_key_file (PEM)
    //           "pfx"         = load tls_pfx_file with tls_pfx_passphrase
    std::string tls_mode;
    std::string tls_cert_file;          // PEM certificate path (mode "pem")
    std::string tls_key_file;           // PEM private key path (mode "pem")
    std::string tls_pfx_file;           // PKCS#12 bundle path  (mode "pfx")
    std::string tls_pfx_passphrase;     // PFX passphrase       (mode "pfx")

    // Optional plain-HTTP port that 301-redirects every request to HTTPS.
    // 0 = disabled.
    int      http_redirect_port = 0;

    static WebServerConfig LoadFromFile(const std::filesystem::path& path);
    void SaveToFile(const std::filesystem::path& path) const;
};

// ──────────────────────────────────────────────────────────────────────────────
// SessionInfo — returned by GetActiveSessions() for the Admin Config dialog.
// ──────────────────────────────────────────────────────────────────────────────
struct SessionInfo {
    std::string token_prefix;   // first 8 hex chars only (for display)
    std::string user_id;
    std::string username;
    std::string remote_addr;
    std::chrono::system_clock::time_point created_at;
    long long   idle_seconds = 0;
};

// ──────────────────────────────────────────────────────────────────────────────
// WebServer
// ──────────────────────────────────────────────────────────────────────────────
struct WebServerImpl;   // defined in web_server.cpp; owns the httplib::Server

class WebServer {
public:
    using ContentChangedCallback = std::function<void()>;

    WebServer(AppStorage* storage,
              WebUserStore* user_store,
              WebServerConfig config,
              std::filesystem::path app_root,
              McpManager* mcp_manager = nullptr,
              RagService* rag_service = nullptr);
    ~WebServer();

    // Non-copyable, non-movable (owns threads + mutex)
    WebServer(const WebServer&)            = delete;
    WebServer& operator=(const WebServer&) = delete;

    bool Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }
    int  ActiveSessions() const;
    int  Port()           const { return config_.port; }

    // Update config and restart if running.
    void Reconfigure(const WebServerConfig& new_config);

    // ── Session management (for Admin Config dialog) ──────────────────────
    std::vector<SessionInfo> GetActiveSessions() const;
    void ForceLogoutUser(const std::string& user_id);
    void ForceLogoutAll();

    // ── Audit log ─────────────────────────────────────────────────────────
    void SetAuditLogPath(const std::filesystem::path& path);
    void SetContentChangedCallback(ContentChangedCallback callback);

    // ── TLS status (public — used by Web Config dialog) ───────────────────
    // Returns the directory used for auto-generated certs (<app_root>/certs/).
    std::filesystem::path TlsCertsDir() const;

    // Returns days until the configured certificate expires, or -1 if
    // unknown / not applicable (HTTP mode, cert unreadable, no OpenSSL).
    int GetCertExpiryDays() const;

private:
    // ── Session ──────────────────────────────────────────────────────────
    struct Session {
        std::string user_id;
        std::string username;
        bool        force_password_reset = false;
        bool        needs_password_reset = false;
        std::string remote_addr;
        std::chrono::system_clock::time_point created_at
            = std::chrono::system_clock::now();
        std::chrono::steady_clock::time_point last_activity;
    };

    std::string         CreateSession(const WebUser& user,
                                       const std::string& remote_addr);
    std::optional<Session> FindSession(const std::string& token) const;
    bool                TouchSession(const std::string& token);  // returns false if expired
    void                DeleteSession(const std::string& token);
    void                PurgeExpiredSessions();
    static std::string  GenerateToken();
    static std::string  GetCookieValue(const std::string& cookie_header,
                                        const std::string& name);
    static std::string  GetRemoteAddr(const void* req_ptr);

    // ── Route helpers ─────────────────────────────────────────────────────
    void RegisterRoutes();

    // Returns a Session if the request carries a valid, unexpired cookie.
    // On failure, writes a 401 or 302 to res and returns nullopt.
    std::optional<Session> RequireAuth(const void* req_ptr, void* res_ptr);

    static void SendJson (void* res_ptr, int status, const std::string& body);
    static void SendError(void* res_ptr, int status, const std::string& message);

    // ── Auth routes ───────────────────────────────────────────────────────
    void HandleLogin         (const void* req, void* res);
    void HandleLogout        (const void* req, void* res);
    void HandleChangePassword(const void* req, void* res);

    // ── API routes (require auth) ─────────────────────────────────────────
    void HandleGetProjects    (const void* req, void* res);
    void HandleGetChats       (const void* req, void* res);
    void HandleCreateChat     (const void* req, void* res);
    void HandleDeleteChat     (const void* req, void* res);
    void HandleGetMessages    (const void* req, void* res);
    void HandleSendMessage    (const void* req, void* res);
    void HandleStreamMessage  (const void* req, void* res);  // SSE streaming
    void HandleUpload         (const void* req, void* res);  // multipart file upload

    // ── Static file serving ───────────────────────────────────────────────
    void HandleStaticOrPage(const void* req, void* res);
    bool ServeFile         (const std::filesystem::path& abs_path, void* res);
    static std::string MimeType(const std::string& ext);

    // ── Utility ───────────────────────────────────────────────────────────
    std::filesystem::path ResolveWebRoot() const;
    std::filesystem::path ResolveThemeRoot() const;
    // Inject the active theme path into HTML template (replaces {{THEME_PATH}})
    static std::string InjectThemePath(std::string html, const std::string& theme_path);

    // ── Model call helpers ────────────────────────────────────────────────
    struct ModelCallResult {
        bool        success = false;
        std::string assistant_text;
        std::string error;
    };
    // Synchronous (non-streaming) completion — used by HandleSendMessage.
    ModelCallResult CallModel(const std::string& project_id,
                              const std::string& chat_id,
                              const std::string& user_content,
                              const std::string& username,
                              const std::vector<std::string>& attachments = {});

    // Streaming completion — calls on_delta for each token chunk as it arrives.
    // Saves user + assistant messages to storage on completion.
    // Returns error string (empty = success).
    std::string StreamModel(const std::string& project_id,
                            const std::string& chat_id,
                            const std::string& user_content,
                            const std::string& username,
                            const std::function<bool(const std::string&)>& on_delta,
                            const std::vector<std::string>& attachments = {});
                            // on_delta returns false to abort early

    // Build a PATCH /api/chats/:id rename handler
    void HandleRenameChat(const void* req, void* res);

    // Read attachment files and build augmented user content.
    // Returns user_content unchanged if attachments is empty.
    std::string BuildUserContentWithAttachments(
                            const std::string& project_id,
                            const std::string& chat_id,
                            const std::string& user_content,
                            const std::vector<std::string>& attachments) const;

    void NotifyContentChanged() const;

    // ── Rate limiting ─────────────────────────────────────────────────────
    // Tracks per-IP failed login attempts.  After kMaxLoginFailures failures
    // the IP is locked out for kLockoutMinutes minutes.
    static constexpr int kMaxLoginFailures = 5;
    static constexpr int kLockoutMinutes   = 15;

    struct RateLimitEntry {
        int failures = 0;
        std::chrono::steady_clock::time_point lockout_until{};
    };

    // Message submission rate limiting: sliding 60-second window.
    static constexpr int kMaxMessagesPerMinute = 60;
    struct MessageRateEntry {
        std::vector<std::chrono::steady_clock::time_point> timestamps;
    };

    bool IsRateLimited(const std::string& ip) const;
    void RecordLoginFailure(const std::string& ip);
    void RecordLoginSuccess(const std::string& ip);
    bool IsMessageRateLimited(const std::string& ip);
    void RecordMessageSent(const std::string& ip);

    mutable std::mutex rate_mutex_;
    std::unordered_map<std::string, RateLimitEntry> rate_entries_;
    mutable std::mutex msg_rate_mutex_;
    std::unordered_map<std::string, MessageRateEntry> message_rate_entries_;

    // ── Audit log ─────────────────────────────────────────────────────────
    void AppendAuditLog(const std::string& ip,
                        const std::string& event,
                        const std::string& detail = "");

    mutable std::mutex          audit_mutex_;
    std::filesystem::path       audit_log_path_;

    // ── Default web-asset creation ────────────────────────────────────────
    void EnsureDefaultWebAssets() const;

    // ── Vendor library caching ────────────────────────────────────────────
    // Downloads highlight.js / marked / DOMPurify / Mermaid / Vega from CDN into
    // <web_root>/js/vendor/ and <web_root>/css/vendor/ on first start.
    // Runs in a detached thread so it never blocks Start().
    // If files already exist or download fails, it is a silent no-op.
    void EnsureVendorLibs() const;

    // ── TLS helpers (private) ─────────────────────────────────────────────
    // Resolves the active cert + key paths for Start() based on tls_mode.
    // Returns false if the mode requires files that don't exist or are invalid.
    // On "self_signed" this generates the cert/key if absent.
    bool ResolveTlsCertAndKey(std::string& out_cert, std::string& out_key) const;


    // ── Members ──────────────────────────────────────────────────────────────
    AppStorage*                       storage_;
    WebUserStore*                     user_store_;
    McpManager*                       mcp_manager_;
    RagService*                       rag_service_;
    WebServerConfig                   config_;
    std::filesystem::path             app_root_;
    std::unique_ptr<WebServerImpl>    impl_;
    ContentChangedCallback            content_changed_callback_;

    std::atomic<bool>                 running_{false};
    std::thread                       server_thread_;
    std::thread                       redirect_thread_;

    mutable std::mutex                sessions_mutex_;
    std::unordered_map<std::string, Session> sessions_;
};
