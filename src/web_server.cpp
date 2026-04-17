// WIN32_LEAN_AND_MEAN prevents <windows.h> from pulling in the old winsock.h,
// allowing cpp-httplib to include <winsock2.h> first (required on Windows).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// cpp-httplib — download single-file header from:
// https://github.com/yhirose/cpp-httplib/releases
// and place at third_party/httplib/httplib.h
//
// HTTPS: define CPPHTTPLIB_OPENSSL_SUPPORT (done automatically by build.bat when
// third_party/openssl/ is present) and link OpenSSL libs.
#define CPPHTTPLIB_THREAD_POOL_SIZE 1   // overridden at runtime via server.new_task_queue
#include <httplib.h>

// OpenSSL direct API — only included when TLS support is compiled in.
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#endif

#include "web_server.h"
#include "web_assets_default.h"

#include "mcp_manager.h"
#include "openai_client.h"
#include "rag_tool_bridge.h"
#include "util.h"
#include <nlohmann/json.hpp>

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;
namespace fs = std::filesystem;

static std::string NowIso();

namespace {

bool HeaderContains(std::string value, const std::string& needle) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value.find(needle) != std::string::npos;
}

bool IsFormUrlEncoded(const httplib::Request& req) {
    return HeaderContains(req.get_header_value("Content-Type"),
                          "application/x-www-form-urlencoded");
}

int HexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

std::string DecodeFormComponent(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = HexNibble(value[i + 1]);
            const int lo = HexNibble(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                out.push_back(value[i]);
            }
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

json ParseFormUrlEncoded(const std::string& body) {
    json result = json::object();
    size_t pos = 0;
    while (pos <= body.size()) {
        const size_t amp = body.find('&', pos);
        const std::string part = body.substr(
            pos, amp == std::string::npos ? std::string::npos : amp - pos);
        const size_t eq = part.find('=');
        const std::string key = DecodeFormComponent(
            eq == std::string::npos ? part : part.substr(0, eq));
        const std::string value = eq == std::string::npos
            ? std::string()
            : DecodeFormComponent(part.substr(eq + 1));

        if (!key.empty()) result[key] = value;
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return result;
}

std::optional<json> ParseJsonOrFormBody(const httplib::Request& req) {
    if (!IsFormUrlEncoded(req)) {
        try {
            return json::parse(req.body);
        } catch (...) {
            // Fall through and try form decoding for browser fallback submits.
        }
    }

    if (IsFormUrlEncoded(req) || req.body.find('=') != std::string::npos) {
        json form = ParseFormUrlEncoded(req.body);
        if (!form.empty()) return form;
    }

    return std::nullopt;
}

void SendRedirect(httplib::Response* res, const std::string& location) {
    res->status = 302;
    res->set_header("Location", location);
}

std::vector<ChatToolDefinition> BuildMcpToolDefinitions(
    const std::vector<McpExposedTool>& exposed_tools) {
    std::vector<ChatToolDefinition> definitions;
    definitions.reserve(exposed_tools.size());
    for (const auto& exposed : exposed_tools) {
        ChatToolDefinition definition;
        definition.name = exposed.alias;
        definition.description = exposed.description.empty()
            ? ("MCP tool from server \"" + exposed.server_name + "\" named \"" +
               exposed.tool_name + "\".")
            : (exposed.description + " (MCP server: " + exposed.server_name +
               ", tool: " + exposed.tool_name + ")");
        definition.parameters_json = exposed.input_schema_json;
        definitions.push_back(std::move(definition));
    }
    return definitions;
}

void EnsureProjectMcpConnections(McpManager* mcp_manager,
                                 const std::string& project_id) {
    if (!mcp_manager || project_id.empty()) return;

    for (const auto& config : mcp_manager->configs()) {
        if (!config.enabled ||
            !mcp_manager->IsServerSelectedForProject(project_id, config.id)) {
            continue;
        }

        std::string ignored_error;
        mcp_manager->ConnectServer(config.id, project_id, &ignored_error);
    }
}

std::string BuildMcpProjectContext(McpManager* mcp_manager,
                                   AppStorage* storage,
                                   const std::string& project_id) {
    if (!mcp_manager || project_id.empty()) return {};

    const auto bindings = mcp_manager->GetProjectBindings(project_id);
    if (bindings.empty()) return {};

    std::unordered_map<std::string, std::string> values;
    for (const auto& binding : bindings) {
        for (const auto& variable : binding.variables) {
            const std::string name = Trim(variable.name);
            const std::string value = Trim(variable.value);
            if (!name.empty() && !value.empty()) values[name] = value;
        }
    }
    if (storage) {
        const auto project_settings = storage->LoadProjectSettings(project_id);
        for (const auto& variable : project_settings.project_variables) {
            const std::string name = Trim(variable.name);
            const std::string value = Trim(variable.value);
            if (!name.empty() && !value.empty()) values[name] = value;
        }
    }
    if (values.empty()) return {};

    struct ContextVariable {
        std::string name;
        std::string value;
        std::string description;
    };

    std::vector<ContextVariable> context_values;
    std::vector<std::string> emitted_names;
    const auto add_variable = [&](const McpServerVariable& variable) {
        if (!variable.inject_into_context || variable.name.empty()) return;
        if (std::find(emitted_names.begin(), emitted_names.end(), variable.name)
                != emitted_names.end()) {
            return;
        }

        const auto value_it = values.find(variable.name);
        if (value_it == values.end() || Trim(value_it->second).empty()) return;

        ContextVariable context_variable;
        context_variable.name = variable.name;
        context_variable.value = value_it->second;
        context_variable.description = Trim(variable.description);
        context_values.push_back(std::move(context_variable));
        emitted_names.push_back(variable.name);
    };

    for (const auto& variable : mcp_manager->global_variables()) {
        add_variable(variable);
    }

    const auto& configs = mcp_manager->configs();
    for (const auto& binding : bindings) {
        const auto config_it = std::find_if(configs.begin(), configs.end(),
            [&](const McpServerConfig& config) {
                return config.id == binding.server_id;
            });
        if (config_it == configs.end()) continue;
        for (const auto& variable : config_it->variables) {
            add_variable(variable);
        }
    }

    if (context_values.empty()) return {};

    std::ostringstream stream;
    stream << "MCP Project Context:\n";
    stream << "These project variable values are current for this chat. Use them "
              "when choosing paths, working directories, command locations, and "
              "MCP tool arguments.\n";
    for (const auto& variable : context_values) {
        stream << "- " << variable.name << ": " << variable.value;
        if (!variable.description.empty()) {
            stream << " (description: " << variable.description << ")";
        }
        stream << "\n";
    }
    return stream.str();
}

std::string BuildWebFormattingContext() {
    return
        "Web Chat Formatting Capabilities:\n"
        "This response is being rendered in the web chat. Mermaid and Vega-Lite "
        "libraries are available for visual output.\n"
        "- Use fenced code blocks tagged `mermaid` for flowcharts, sequence diagrams, "
        "state diagrams, class diagrams, timelines, and similar diagrams.\n"
        "- Use fenced code blocks tagged `vega-lite` with JSON for charts and data graphics. "
        "Keep Vega-Lite specs self-contained with inline data when possible.\n"
        "- Prefer these formats when a diagram, graph, or flow chart would make "
        "the answer clearer.";
}

void AppendAssistantToolRequest(std::vector<MessageRecord>& messages,
                                const ChatCompletionResult& completion) {
    MessageRecord assistant_msg;
    assistant_msg.role = "assistant";
    assistant_msg.content = completion.assistant_text;
    assistant_msg.created_at = NowIso();

    if (!completion.raw_message_json.empty()) {
        try {
            const auto message_json = json::parse(completion.raw_message_json);
            if (message_json.contains("tool_calls")) {
                assistant_msg.tool_calls_json = message_json["tool_calls"].dump();
            }
        } catch (...) {}
    }

    if (assistant_msg.tool_calls_json.empty()) {
        json tool_calls_json = json::array();
        for (const auto& tool_call : completion.tool_calls) {
            tool_calls_json.push_back({
                {"id", tool_call.id},
                {"type", "function"},
                {"function", {
                    {"name", tool_call.name},
                    {"arguments", tool_call.arguments_json},
                }},
            });
        }
        assistant_msg.tool_calls_json = tool_calls_json.dump();
    }

    messages.push_back(std::move(assistant_msg));
}

McpToolCallResult InvalidToolArgumentsResult(const ChatToolCall& tool_call) {
    McpToolCallResult result;
    result.success = false;
    result.is_tool_error = true;
    std::ostringstream message;
    message << "Tool call was not executed because the model returned invalid "
            << "JSON arguments for \"" << tool_call.name << "\". Tool arguments "
            << "must be a valid JSON object.";
    if (!tool_call.arguments_error.empty()) {
        message << "\nParser error: " << tool_call.arguments_error;
    }
    if (!tool_call.original_arguments_json.empty()) {
        message << "\nOriginal arguments: " << tool_call.original_arguments_json;
    }
    result.content_text = message.str();
    result.raw_result_json = json{
        {"error", result.content_text},
        {"invalid_arguments", tool_call.original_arguments_json},
    }.dump(2);
    return result;
}

McpToolCallResult ToolUnavailableResult(const ChatToolCall& tool_call) {
    McpToolCallResult result;
    result.success = false;
    result.is_tool_error = true;
    result.content_text = "Tool is not available for this project or server: " + tool_call.name;
    result.raw_result_json = json{
        {"error", result.content_text},
        {"tool", tool_call.name},
    }.dump(2);
    return result;
}

void AppendToolResultMessage(std::vector<MessageRecord>& messages,
                             const ChatToolCall& tool_call,
                             const McpToolCallResult& result) {
    MessageRecord tool_msg;
    tool_msg.role = "tool";
    tool_msg.name = tool_call.name;
    tool_msg.tool_call_id = tool_call.id;
    tool_msg.content = result.content_text;
    tool_msg.created_at = NowIso();
    messages.push_back(std::move(tool_msg));
}

} // namespace

// ──────────────────────────────────────────────────────────────────────────────
// OpenSSL initialization — must happen before any OpenSSL calls.
// The Shining Light OpenSSL 3.x builds read an openssl.cnf that tries to load
// the FIPS provider, which causes a fatal error if FIPS isn't built.
// Setting OPENSSL_CONF to "nul" prevents the config file from being loaded.
// ──────────────────────────────────────────────────────────────────────────────
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
namespace {
    struct OpenSSLInit {
        OpenSSLInit() {
            _putenv_s("OPENSSL_CONF", "nul");
            Logger::Info("OpenSSL", "OPENSSL_CONF set to nul");
            OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr);
            Logger::Info("OpenSSL", "OPENSSL_init_crypto called");
        }
    };
    static OpenSSLInit openssl_init;
}
#endif

// ──────────────────────────────────────────────────────────────────────────────
// Pimpl struct — owns the httplib::Server (or SSLServer) so this TU is the
// only one that includes httplib.h, keeping overall compile times manageable.
//
// The main server is heap-allocated so we can create either httplib::Server
// (HTTP) or httplib::SSLServer (HTTPS) based on the runtime config.
// ──────────────────────────────────────────────────────────────────────────────
struct WebServerImpl {
    std::unique_ptr<httplib::Server> server;      // plain HTTP or SSL
    std::unique_ptr<httplib::Server> redirect_srv; // HTTP→HTTPS redirect (optional)

    httplib::Server& srv() { return *server; }
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
    cfg.http_redirect_port      = j.value("http_redirect_port", 0);

    // TLS subtree
    if (j.contains("tls") && j["tls"].is_object()) {
        const auto& t = j["tls"];
        cfg.tls_mode            = t.value("mode", "");
        cfg.tls_cert_file       = t.value("cert_file", "");
        cfg.tls_key_file        = t.value("key_file", "");
        cfg.tls_pfx_file        = t.value("pfx_file", "");
        cfg.tls_pfx_passphrase  = t.value("pfx_passphrase", "");
    }
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
        {"http_redirect_port",       http_redirect_port},
        {"tls", {
            {"mode",           tls_mode},
            {"cert_file",      tls_cert_file},
            {"key_file",       tls_key_file},
            {"pfx_file",       tls_pfx_file},
            {"pfx_passphrase", tls_pfx_passphrase},
        }},
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
                     fs::path          app_root,
                     McpManager*       mcp_manager,
                     RagService*       rag_service)
    : storage_(storage)
    , user_store_(user_store)
    , mcp_manager_(mcp_manager)
    , rag_service_(rag_service)
    , config_(std::move(config))
    , app_root_(std::move(app_root))
    , impl_(std::make_unique<WebServerImpl>())
{}

WebServer::~WebServer() {
    Stop();
}

void WebServer::SetContentChangedCallback(ContentChangedCallback callback) {
    content_changed_callback_ = std::move(callback);
}

void WebServer::NotifyContentChanged() const {
    if (content_changed_callback_) {
        content_changed_callback_();
    }
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

bool WebServer::IsMessageRateLimited(const std::string& ip) {
    std::lock_guard<std::mutex> lock(msg_rate_mutex_);
    auto now = std::chrono::steady_clock::now();
    const auto window = std::chrono::minutes(1);

    auto it = message_rate_entries_.find(ip);
    if (it == message_rate_entries_.end()) return false;

    // Remove timestamps outside the 60-second window
    auto& ts = it->second.timestamps;
    ts.erase(
        std::remove_if(ts.begin(), ts.end(),
            [&](const std::chrono::steady_clock::time_point& t) { return (now - t) > window; }),
        ts.end());

    return static_cast<int>(ts.size()) >= kMaxMessagesPerMinute;
}

void WebServer::RecordMessageSent(const std::string& ip) {
    std::lock_guard<std::mutex> lock(msg_rate_mutex_);
    auto now = std::chrono::steady_clock::now();
    const auto window = std::chrono::minutes(1);

    auto& entry = message_rate_entries_[ip];
    // Remove old timestamps
    entry.timestamps.erase(
        std::remove_if(entry.timestamps.begin(), entry.timestamps.end(),
            [&](const auto& t) { return (now - t) > window; }),
        entry.timestamps.end());

    entry.timestamps.push_back(now);
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

    // If user needs password reset, block routes except reset and identity checks.
    if (session->needs_password_reset) {
        const std::string path = req->path;
        const bool is_change_password =
            (path == "/change-password" || path == "/api/change-password"
             || path == "/api/me");
        if (!is_change_password) {
            auto* res = static_cast<httplib::Response*>(res_ptr);
            res->status = 302;
            res->set_header("Location", "/change-password");
            return std::nullopt;
        }
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
        // Vendor library CDN fallback: if a vendor file is missing locally
        // (download still in progress or failed), redirect to the canonical CDN
        // URL so the page still works.
        static const std::unordered_map<std::string, std::string> kVendorCdn = {
            { "js/vendor/highlight.min.js",
              "https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js" },
            { "css/vendor/vs2015.min.css",
              "https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/vs2015.min.css" },
            { "js/vendor/marked.min.js",
              "https://cdnjs.cloudflare.com/ajax/libs/marked/11.2.0/marked.min.js" },
            { "js/vendor/purify.min.js",
              "https://cdnjs.cloudflare.com/ajax/libs/dompurify/3.1.5/purify.min.js" },
            { "js/vendor/mermaid.min.js",
              "https://cdnjs.cloudflare.com/ajax/libs/mermaid/10.9.1/mermaid.min.js" },
            { "js/vendor/vega.min.js",
              "https://cdnjs.cloudflare.com/ajax/libs/vega/5.28.0/vega.min.js" },
            { "js/vendor/vega-lite.min.js",
              "https://cdnjs.cloudflare.com/ajax/libs/vega-lite/5.18.1/vega-lite.min.js" },
            { "js/vendor/vega-embed.min.js",
              "https://cdnjs.cloudflare.com/ajax/libs/vega-embed/6.25.0/vega-embed.min.js" },
        };
        const auto it = kVendorCdn.find(url_path);
        if (it != kVendorCdn.end()) {
            res->status = 302;
            res->set_header("Location", it->second);
            res->set_header("Cache-Control", "no-store");
        } else {
            res->status = 404;
            res->set_content("Not found", "text/plain");
        }
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

    const bool form_post = IsFormUrlEncoded(*req);
    const auto body_opt = ParseJsonOrFormBody(*req);
    if (!body_opt) {
        SendError(res_ptr, 400, "Invalid login request"); return;
    }
    const json& body_j = *body_opt;

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

    // If user needs password reset, update session and route to change-password.
    if (user_opt->force_password_reset) {
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(token);
            if (it != sessions_.end()) it->second.needs_password_reset = true;
        }
        const std::string cookie = "session=" + token
            + "; Path=/; HttpOnly; SameSite=Strict";
        res->set_header("Set-Cookie", cookie);
        if (form_post) {
            SendRedirect(res, "/change-password");
        } else {
            json resp = {
                {"ok",                   true},
                {"force_password_reset", true},
                {"display_name",         user_opt->display_name},
            };
            SendJson(res_ptr, 200, resp.dump());
        }
        return;
    }

    // Set HttpOnly session cookie (add Secure when TLS is enabled)
    const std::string cookie = "session=" + token
        + "; Path=/; HttpOnly; SameSite=Strict";
    res->set_header("Set-Cookie", cookie);

    if (form_post) {
        SendRedirect(res, "/");
        return;
    }

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
    const bool form_post = IsFormUrlEncoded(*req);
    const auto body_opt = ParseJsonOrFormBody(*req);
    if (!body_opt) {
        SendError(res_ptr, 400, "Invalid password change request"); return;
    }
    const json& body_j = *body_opt;

    const std::string current  = body_j.value("current_password",
                                  body_j.value("current-password", ""));
    const std::string new_pass = body_j.value("new_password",
                                  body_j.value("new-password", ""));
    // Password policy: ≥10 chars, at least one uppercase, one lowercase, one digit
    if (new_pass.size() < 10) {
        SendError(res_ptr, 400, "Password must be at least 10 characters"); return;
    }
    {
        bool has_upper = false, has_lower = false, has_digit = false;
        for (char c : new_pass) {
            if (std::isupper(static_cast<unsigned char>(c))) has_upper = true;
            else if (std::islower(static_cast<unsigned char>(c))) has_lower = true;
            else if (std::isdigit(static_cast<unsigned char>(c))) has_digit = true;
        }
        if (!has_upper || !has_lower || !has_digit) {
            SendError(res_ptr, 400,
                "Password must include at least one uppercase letter, one lowercase letter, and one digit");
            return;
        }
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

    // Update the session to clear both force_password_reset and needs_password_reset flags
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        const auto token = GetCookieValue(
            static_cast<const httplib::Request*>(req_ptr)->get_header_value("Cookie"), "session");
        auto it = sessions_.find(token);
        if (it != sessions_.end()) {
            it->second.force_password_reset = false;
            it->second.needs_password_reset = false;
        }
    }

    if (form_post) {
        SendRedirect(static_cast<httplib::Response*>(res_ptr), "/");
    } else {
        SendJson(res_ptr, 200, R"({"ok":true})");
    }
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
    NotifyContentChanged();
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
            NotifyContentChanged();
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
            NotifyContentChanged();
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
                if (m.role == "tool") continue;
                if (m.role == "assistant" &&
                    (m.content.empty() || !m.tool_calls_json.empty())) {
                    continue;
                }
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
        // Inject $<UserProjectFolder> if per-user folder is enabled for this project
        const std::string user_folder = user_store_->GetUserProjectFolder(project_id, username);
        if (!user_folder.empty()) {
            fs::create_directories(fs::path(user_folder));
            const std::string ph = "$<UserProjectFolder>";
            size_t pos = 0;
            while ((pos = instr.find(ph, pos)) != std::string::npos) {
                instr.replace(pos, ph.size(), user_folder);
                pos += user_folder.size();
            }
        }
        if (!instr.empty()) system_prompt = "Project Instructions:\n" + instr;
    }
    if (mcp_manager_) {
        EnsureProjectMcpConnections(mcp_manager_, project_id);
        const std::string mcp_context =
            BuildMcpProjectContext(mcp_manager_, storage_, project_id);
        if (!mcp_context.empty()) {
            if (!system_prompt.empty()) system_prompt += "\n\n";
            system_prompt += mcp_context;
        }
    }
    if (rag_service_) {
        const std::string rag_context =
            rag_tools::BuildRagProjectContext(rag_service_, project_id);
        if (!rag_context.empty()) {
            if (!system_prompt.empty()) system_prompt += "\n\n";
            system_prompt += rag_context;
        }
    }
    {
        const std::string web_formatting_context = BuildWebFormattingContext();
        if (!web_formatting_context.empty()) {
            if (!system_prompt.empty()) system_prompt += "\n\n";
            system_prompt += web_formatting_context;
        }
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

    const auto exposed_tools = (mcp_manager_ && selected_model.supports_tools)
        ? mcp_manager_->GetExposedToolsForProject(project_id)
        : std::vector<McpExposedTool>{};
    auto tool_definitions = BuildMcpToolDefinitions(exposed_tools);
    const auto rag_tool_set = (rag_service_ && selected_model.supports_tools)
        ? rag_tools::BuildRagToolSet(rag_service_, project_id)
        : rag_tools::RagToolSet{};
    tool_definitions.insert(
        tool_definitions.end(),
        rag_tool_set.definitions.begin(),
        rag_tool_set.definitions.end());

    if (!tool_definitions.empty()) {
        constexpr int kMaxToolRounds = 8;
        bool success = false;

        for (int round = 0; round < kMaxToolRounds; ++round) {
            ChatRequestOptions loop_opts = opts;
            loop_opts.messages = messages;

            const auto completion =
                OpenAIClient::CreateToolAwareCompletion(loop_opts, tool_definitions);
            if (!completion.success) {
                result.error = completion.error.empty()
                    ? "Model call failed"
                    : completion.error;
                storage_->SaveMessages(project_id, chat_id, messages);
                NotifyContentChanged();
                return result;
            }

            if (!completion.tool_calls.empty()) {
                AppendAssistantToolRequest(messages, completion);
                for (const auto& tool_call : completion.tool_calls) {
                    McpToolCallResult tool_result;
                    if (!tool_call.arguments_valid) {
                        tool_result = InvalidToolArgumentsResult(tool_call);
                    } else if (rag_tools::IsRagToolName(tool_call.name, &rag_tool_set.routes)) {
                        tool_result = rag_tools::CallRagTool(
                            rag_service_,
                            mcp_manager_,
                            project_id,
                            tool_call.name,
                            tool_call.arguments_json,
                            &rag_tool_set.routes);
                    } else if (mcp_manager_) {
                        tool_result = mcp_manager_->CallExposedTool(
                            project_id, tool_call.name, tool_call.arguments_json);
                    } else {
                        tool_result = ToolUnavailableResult(tool_call);
                    }
                    AppendToolResultMessage(messages, tool_call, tool_result);
                }
                continue;
            }

            if (!completion.assistant_text.empty()) {
                MessageRecord asst_msg;
                asst_msg.role       = "assistant";
                asst_msg.content    = completion.assistant_text;
                asst_msg.created_at = NowIso();
                messages.push_back(asst_msg);
                result.assistant_text = completion.assistant_text;
            }
            success = true;
            break;
        }

        if (!success) {
            ChatRequestOptions final_opts = opts;
            final_opts.messages = messages;
            if (!final_opts.system_prompt.empty()) final_opts.system_prompt += "\n\n";
            final_opts.system_prompt +=
                "The MCP tool-call round limit has been reached. Do not call or "
                "request any more tools. Use the tool results already present in "
                "the conversation to write the final answer. If the requested work "
                "succeeded, say so and summarize the important output. If it did "
                "not fully succeed, explain the last observed state and what remains.";

            const auto final_completion =
                OpenAIClient::CreateSimpleCompletion(final_opts);
            if (final_completion.success && !final_completion.assistant_text.empty()) {
                MessageRecord asst_msg;
                asst_msg.role       = "assistant";
                asst_msg.content    = final_completion.assistant_text;
                asst_msg.created_at = NowIso();
                messages.push_back(asst_msg);
                storage_->SaveMessages(project_id, chat_id, messages);
                NotifyContentChanged();
                result.success = true;
                result.assistant_text = final_completion.assistant_text;
                return result;
            }

            result.error = final_completion.error.empty()
                ? "The model exceeded the MCP tool-call loop limit."
                : "The model exceeded the MCP tool-call loop limit, and the final summary failed: " +
                      final_completion.error;
            storage_->SaveMessages(project_id, chat_id, messages);
            NotifyContentChanged();
            return result;
        }

        storage_->SaveMessages(project_id, chat_id, messages);
        NotifyContentChanged();
        result.success = true;
        return result;
    }

    // Call model without tools when none are connected for the project.
    const auto call_result = OpenAIClient::CreateSimpleCompletion(opts);

    if (!call_result.success) {
        result.error = call_result.error.empty() ? "Model call failed" : call_result.error;
        // Still save the user message so the chat shows it
        storage_->SaveMessages(project_id, chat_id, messages);
        NotifyContentChanged();
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
    NotifyContentChanged();

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

    // Rate-limit check
    const std::string ip = GetRemoteAddr(req_ptr);
    if (IsMessageRateLimited(ip)) {
        static_cast<httplib::Response*>(res_ptr)->set_header("Retry-After", "60");
        SendError(res_ptr, 429, "Too many messages — please wait before sending again"); return;
    }
    RecordMessageSent(ip);

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
        // Inject $<UserProjectFolder> if per-user folder is enabled for this project
        const std::string user_folder = user_store_->GetUserProjectFolder(project_id, username);
        if (!user_folder.empty()) {
            fs::create_directories(fs::path(user_folder));
            const std::string ph = "$<UserProjectFolder>";
            size_t pos = 0;
            while ((pos = instr.find(ph, pos)) != std::string::npos) {
                instr.replace(pos, ph.size(), user_folder);
                pos += user_folder.size();
            }
        }
        if (!instr.empty()) system_prompt = "Project Instructions:\n" + instr;
    }
    if (mcp_manager_) {
        EnsureProjectMcpConnections(mcp_manager_, project_id);
        const std::string mcp_context =
            BuildMcpProjectContext(mcp_manager_, storage_, project_id);
        if (!mcp_context.empty()) {
            if (!system_prompt.empty()) system_prompt += "\n\n";
            system_prompt += mcp_context;
        }
    }
    if (rag_service_) {
        const std::string rag_context =
            rag_tools::BuildRagProjectContext(rag_service_, project_id);
        if (!rag_context.empty()) {
            if (!system_prompt.empty()) system_prompt += "\n\n";
            system_prompt += rag_context;
        }
    }
    {
        const std::string web_formatting_context = BuildWebFormattingContext();
        if (!web_formatting_context.empty()) {
            if (!system_prompt.empty()) system_prompt += "\n\n";
            system_prompt += web_formatting_context;
        }
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
    NotifyContentChanged();

    // Build request (streaming = true)
    ChatRequestOptions opts;
    opts.provider      = selected_provider;
    opts.model         = selected_model;
    opts.system_prompt = system_prompt;
    opts.temperature   = 0.2;
    opts.max_tokens    = 4096;  // allow longer outputs when streaming
    opts.messages      = messages;

    const auto exposed_tools = (mcp_manager_ && selected_model.supports_tools)
        ? mcp_manager_->GetExposedToolsForProject(project_id)
        : std::vector<McpExposedTool>{};
    auto tool_definitions = BuildMcpToolDefinitions(exposed_tools);
    const auto rag_tool_set = (rag_service_ && selected_model.supports_tools)
        ? rag_tools::BuildRagToolSet(rag_service_, project_id)
        : rag_tools::RagToolSet{};
    tool_definitions.insert(
        tool_definitions.end(),
        rag_tool_set.definitions.begin(),
        rag_tool_set.definitions.end());

    if (!tool_definitions.empty()) {
        constexpr int kMaxToolRounds = 8;
        std::string accumulated;
        bool aborted = false;
        bool success = false;

        for (int round = 0; round < kMaxToolRounds; ++round) {
            ChatRequestOptions loop_opts = opts;
            loop_opts.messages = messages;

            const auto completion = OpenAIClient::StreamToolAwareCompletion(
                loop_opts, tool_definitions,
                [&](const std::string& delta) {
                    accumulated += delta;
                    if (!on_delta(delta)) {
                        aborted = true;
                    }
                });

            if (!completion.success && !aborted) {
                return completion.error.empty()
                    ? "Streaming model call failed."
                    : completion.error;
            }

            if (aborted) {
                if (!accumulated.empty()) {
                    MessageRecord asst_msg;
                    asst_msg.role = "assistant";
                    asst_msg.content = accumulated;
                    asst_msg.created_at = NowIso();
                    messages.push_back(asst_msg);
                    storage_->SaveMessages(project_id, chat_id, messages);
                    NotifyContentChanged();
                }
                return {};
            }

            if (!completion.tool_calls.empty()) {
                AppendAssistantToolRequest(messages, completion);
                for (const auto& tool_call : completion.tool_calls) {
                    McpToolCallResult tool_result;
                    if (!tool_call.arguments_valid) {
                        tool_result = InvalidToolArgumentsResult(tool_call);
                    } else if (rag_tools::IsRagToolName(tool_call.name, &rag_tool_set.routes)) {
                        tool_result = rag_tools::CallRagTool(
                            rag_service_,
                            mcp_manager_,
                            project_id,
                            tool_call.name,
                            tool_call.arguments_json,
                            &rag_tool_set.routes);
                    } else if (mcp_manager_) {
                        tool_result = mcp_manager_->CallExposedTool(
                            project_id, tool_call.name, tool_call.arguments_json);
                    } else {
                        tool_result = ToolUnavailableResult(tool_call);
                    }
                    AppendToolResultMessage(messages, tool_call, tool_result);
                }
                storage_->SaveMessages(project_id, chat_id, messages);
                NotifyContentChanged();
                continue;
            }

            if (!completion.assistant_text.empty()) {
                MessageRecord asst_msg;
                asst_msg.role = "assistant";
                asst_msg.content = completion.assistant_text;
                asst_msg.created_at = NowIso();
                messages.push_back(asst_msg);
                storage_->SaveMessages(project_id, chat_id, messages);
                NotifyContentChanged();
            }
            success = true;
            break;
        }

        if (!success) {
            ChatRequestOptions final_opts = opts;
            final_opts.messages = messages;
            if (!final_opts.system_prompt.empty()) final_opts.system_prompt += "\n\n";
            final_opts.system_prompt +=
                "The MCP tool-call round limit has been reached. Do not call or "
                "request any more tools. Use the tool results already present in "
                "the conversation to write the final answer. If the requested work "
                "succeeded, say so and summarize the important output. If it did "
                "not fully succeed, explain the last observed state and what remains.";

            std::string final_text;
            bool final_aborted = false;
            const auto final_result = OpenAIClient::StreamChat(
                final_opts,
                [&](const std::string& delta) {
                    final_text += delta;
                    if (!on_delta(delta)) {
                        final_aborted = true;
                    }
                });

            if (!final_result.success && !final_aborted) {
                return final_result.error.empty()
                    ? "The model exceeded the MCP tool-call loop limit."
                    : "The model exceeded the MCP tool-call loop limit, and the final summary failed: " +
                          final_result.error;
            }

            if (!final_text.empty()) {
                MessageRecord asst_msg;
                asst_msg.role = "assistant";
                asst_msg.content = final_text;
                asst_msg.created_at = NowIso();
                messages.push_back(asst_msg);
                storage_->SaveMessages(project_id, chat_id, messages);
                NotifyContentChanged();
            }
            return {};
        }
        return {};
    }

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
        NotifyContentChanged();
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

    // Rate-limit check
    const std::string ip = GetRemoteAddr(req_ptr);
    if (IsMessageRateLimited(ip)) {
        static_cast<httplib::Response*>(res_ptr)->set_header("Retry-After", "60");
        SendError(res_ptr, 429, "Too many messages — please wait before sending again"); return;
    }
    RecordMessageSent(ip);

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

    // Find "file" field in multipart (httplib stores files in req->form.files)
    if (!req->form.has_file("file")) {
        SendError(res_ptr, 400, "No 'file' field in upload"); return;
    }
    const auto file_info = req->form.get_file("file");  // returns FormData by value

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
    auto& srv = impl_->srv();

    // Security headers on every response
    // Note: HSTS only applies when TLS is enabled; CSP uses 'unsafe-inline' for
    // styles because highlight.js themes inject inline style attributes.
    httplib::Headers headers = {
        {"X-Frame-Options",        "DENY"},
        {"X-Content-Type-Options", "nosniff"},
        {"Referrer-Policy",        "strict-origin-when-cross-origin"},
        {"Content-Security-Policy",
         "default-src 'self'; "
         "script-src 'self' 'unsafe-eval'; "
         "style-src 'self' 'unsafe-inline'; "
         "img-src 'self' data:; "
         "connect-src 'self'; "
         "font-src 'self'; "
         "frame-ancestors 'none'"},
    };
    if (!config_.tls_mode.empty()) {
        headers.emplace("Strict-Transport-Security",
                      "max-age=31536000; includeSubDomains");
    }
    srv.set_default_headers(std::move(headers));

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
            {"force_password_reset", session->force_password_reset
                                     || session->needs_password_reset
                                     || user_opt->force_password_reset},
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
    // DELETE /api/chats/:id/stream — abort in-progress streaming (sent by stop button)
    srv.Delete(R"(/api/chats/([^/]+)/stream)", [](const httplib::Request& req, httplib::Response& res) {
        // The actual abort is handled via AbortController on the client side.
        // This endpoint exists to allow the server to track that a cancellation occurred
        // and for future use (e.g., logging, metrics).
        res.status = 200;
        res.set_content("{}", "application/json");
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
        { "js/login.js",                   DefaultWebAssets::kLoginJs            },
        { "js/change-password.js",         DefaultWebAssets::kChangePasswordJs   },
        { "js/app.js",                     DefaultWebAssets::kAppJs              },
        { "themes/default/style.css",      DefaultWebAssets::kThemeDefaultCss    },
        { "themes/default/theme.json",     DefaultWebAssets::kThemeDefaultJson   },
        { "themes/dark/style.css",         DefaultWebAssets::kThemeDarkCss       },
        { "themes/dark/theme.json",        DefaultWebAssets::kThemeDarkJson      },
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
// EnsureVendorLibs
//
// Downloads the front-end vendor libraries from cdnjs if the cached
// copies are absent from the web root.  Runs in a detached background thread
// so Start() returns immediately.  On any failure (no internet, etc.) the
// missing files stay absent and the CDN-redirect fallback in
// HandleStaticOrPage kicks in at serve time.
// ──────────────────────────────────────────────────────────────────────────────
namespace {

// Synchronous WinHTTP GET to a file.  Returns true on success.
static bool WinHttpDownloadToFile(const wchar_t* url, const fs::path& dest) {
    URL_COMPONENTSW comp = {};
    comp.dwStructSize       = sizeof(comp);
    wchar_t host[512]       = {};
    wchar_t url_path[2048]  = {};
    comp.lpszHostName       = host;       comp.dwHostNameLength  = 511;
    comp.lpszUrlPath        = url_path;   comp.dwUrlPathLength   = 2047;

    if (!WinHttpCrackUrl(url, 0, 0, &comp)) return false;

    struct HI {
        HINTERNET h = nullptr;
        ~HI() { if (h) WinHttpCloseHandle(h); }
    } sess, conn, req;

    sess.h = WinHttpOpen(L"AgentVendorFetch/1.0",
                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess.h) return false;

    // Timeouts: 15 s connect, 60 s receive
    DWORD t15 = 15000, t60 = 60000;
    WinHttpSetOption(sess.h, WINHTTP_OPTION_CONNECT_TIMEOUT,    &t15, sizeof t15);
    WinHttpSetOption(sess.h, WINHTTP_OPTION_RECEIVE_TIMEOUT,    &t60, sizeof t60);
    WinHttpSetOption(sess.h, WINHTTP_OPTION_SEND_TIMEOUT,       &t15, sizeof t15);
    WinHttpSetOption(sess.h, WINHTTP_OPTION_RESOLVE_TIMEOUT,    &t15, sizeof t15);

    conn.h = WinHttpConnect(sess.h, host, comp.nPort, 0);
    if (!conn.h) return false;

    const bool is_https = (comp.nScheme == INTERNET_SCHEME_HTTPS);
    req.h = WinHttpOpenRequest(conn.h, L"GET", url_path,
                               nullptr,
                               WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                               is_https ? WINHTTP_FLAG_SECURE : 0);
    if (!req.h) return false;

    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return false;
    if (!WinHttpReceiveResponse(req.h, nullptr)) return false;

    // Verify HTTP 200
    DWORD status = 0, sz = sizeof(DWORD);
    WinHttpQueryHeaders(req.h,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) return false;

    // Read body
    std::vector<char> body;
    body.reserve(512 * 1024);
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req.h, &avail) && avail > 0) {
        const size_t off = body.size();
        body.resize(off + avail);
        DWORD read = 0;
        if (!WinHttpReadData(req.h, body.data() + off, avail, &read)) break;
        body.resize(off + read);
        avail = 0;
    }
    if (body.empty()) return false;

    // Write to file (create directories first)
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (ec) return false;

    std::ofstream ofs(dest, std::ios::binary);
    if (!ofs) return false;
    ofs.write(body.data(), static_cast<std::streamsize>(body.size()));
    return ofs.good();
}

} // anonymous namespace

void WebServer::EnsureVendorLibs() const {
    struct VendorLib { const wchar_t* url; const char* rel_path; };
    static const VendorLib kLibs[] = {
        { L"https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js",
          "js/vendor/highlight.min.js" },
        { L"https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/vs2015.min.css",
          "css/vendor/vs2015.min.css" },
        { L"https://cdnjs.cloudflare.com/ajax/libs/marked/11.2.0/marked.min.js",
          "js/vendor/marked.min.js" },
        { L"https://cdnjs.cloudflare.com/ajax/libs/dompurify/3.1.5/purify.min.js",
          "js/vendor/purify.min.js" },
        { L"https://cdnjs.cloudflare.com/ajax/libs/mermaid/10.9.1/mermaid.min.js",
          "js/vendor/mermaid.min.js" },
        { L"https://cdnjs.cloudflare.com/ajax/libs/vega/5.28.0/vega.min.js",
          "js/vendor/vega.min.js" },
        { L"https://cdnjs.cloudflare.com/ajax/libs/vega-lite/5.18.1/vega-lite.min.js",
          "js/vendor/vega-lite.min.js" },
        { L"https://cdnjs.cloudflare.com/ajax/libs/vega-embed/6.25.0/vega-embed.min.js",
          "js/vendor/vega-embed.min.js" },
    };

    // Check if any files are missing before spawning a thread
    const fs::path root = ResolveWebRoot();
    bool any_missing = false;
    for (const auto& lib : kLibs) {
        if (!fs::exists(root / lib.rel_path)) { any_missing = true; break; }
    }
    if (!any_missing) return;

    // Snapshot the paths we need so the lambda owns everything
    struct Entry { std::wstring url; fs::path dest; };
    std::vector<Entry> todo;
    for (const auto& lib : kLibs) {
        const fs::path dest = root / lib.rel_path;
        if (!fs::exists(dest)) todo.push_back({ lib.url, dest });
    }

    // Download in background so Start() is not delayed
    std::thread([todo = std::move(todo)]() {
        for (const auto& e : todo) {
            WinHttpDownloadToFile(e.url.c_str(), e.dest);
            // Silent on failure — CDN redirect fallback handles it at serve time
        }
    }).detach();
}

// ──────────────────────────────────────────────────────────────────────────────
// TLS helpers
// ──────────────────────────────────────────────────────────────────────────────
fs::path WebServer::TlsCertsDir() const {
    return app_root_ / "certs";
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
// Generate a self-signed RSA-2048 / SHA-256 certificate valid for 3 years.
// Uses the modern EVP_PKEY_CTX API (OpenSSL 3.x compatible).
static bool GenerateSelfSignedCert(const fs::path& cert_path, const fs::path& key_path)
{
    Logger::Info("OpenSSL", "GenerateSelfSignedCert: starting");
    // ── Generate RSA-2048 key ────────────────────────────────────────────────
    EVP_PKEY* pkey = nullptr;
    {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        if (!ctx) {
            Logger::Error("OpenSSL", "GenerateSelfSignedCert: EVP_PKEY_CTX_new_id failed");
            return false;
        }
        if (EVP_PKEY_keygen_init(ctx) <= 0 ||
            EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0 ||
            EVP_PKEY_keygen(ctx, &pkey) <= 0)
        {
            Logger::Error("OpenSSL", "GenerateSelfSignedCert: EVP_PKEY_keygen failed");
            EVP_PKEY_CTX_free(ctx);
            return false;
        }
        EVP_PKEY_CTX_free(ctx);
    }
    Logger::Info("OpenSSL", "GenerateSelfSignedCert: key generated");

    // ── Build X.509 certificate ──────────────────────────────────────────────
    X509* x509 = X509_new();
    if (!x509) {
        Logger::Error("OpenSSL", "GenerateSelfSignedCert: X509_new failed");
        EVP_PKEY_free(pkey);
        return false;
    }
    Logger::Info("OpenSSL", "GenerateSelfSignedCert: X509 created");

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 60LL * 60 * 24 * 365 * 3); // 3 years

    X509_set_pubkey(x509, pkey);

    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "O",  MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("Agent"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
    X509_set_issuer_name(x509, name);   // self-signed

    if (X509_sign(x509, pkey, EVP_sha256()) == 0) {
        Logger::Error("OpenSSL", "GenerateSelfSignedCert: X509_sign failed");
        X509_free(x509); EVP_PKEY_free(pkey); return false;
    }
    Logger::Info("OpenSSL", "GenerateSelfSignedCert: certificate signed");

    // ── Write PEM files ──────────────────────────────────────────────────────
    std::error_code ec;
    fs::create_directories(cert_path.parent_path(), ec);
    Logger::Info("OpenSSL", "GenerateSelfSignedCert: writing to " + key_path.string());
    Logger::Flush();

    bool ok = true;
    {
        FILE* f = nullptr;
        errno_t err = _wfopen_s(&f, key_path.wstring().c_str(), L"w");
        Logger::Info("OpenSSL", "GenerateSelfSignedCert: _wfopen_s returned err=" + std::to_string(err));
        Logger::Flush();
        if (err == 0 && f) {
            Logger::Info("OpenSSL", "GenerateSelfSignedCert: file opened, about to write private key");
            Logger::Flush();
            ok = (PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr) == 1);
            fclose(f);
            if (!ok) Logger::Error("OpenSSL", "GenerateSelfSignedCert: PEM_write_PrivateKey failed");
        } else {
            ok = false;
            Logger::Error("OpenSSL", "GenerateSelfSignedCert: could not open key file for writing, err=" + std::to_string(err));
        }
    }
    Logger::Flush();
    if (ok) {
        FILE* f = nullptr;
        Logger::Info("OpenSSL", "GenerateSelfSignedCert: writing cert to " + cert_path.string());
        Logger::Flush();
        errno_t err = _wfopen_s(&f, cert_path.wstring().c_str(), L"w");
        Logger::Info("OpenSSL", "GenerateSelfSignedCert: cert file open err=" + std::to_string(err));
        Logger::Flush();
        if (err == 0 && f) {
            ok = (PEM_write_X509(f, x509) == 1);
            fclose(f);
            if (!ok) Logger::Error("OpenSSL", "GenerateSelfSignedCert: PEM_write_X509 failed");
        } else {
            ok = false;
            Logger::Error("OpenSSL", "GenerateSelfSignedCert: could not open cert file for writing");
        }
    }

    X509_free(x509);
    EVP_PKEY_free(pkey);

    if (ok) {
        Logger::Info("OpenSSL", "GenerateSelfSignedCert: complete — cert=" +
            cert_path.string() + " key=" + key_path.string());
    } else {
        Logger::Error("OpenSSL", "GenerateSelfSignedCert: FAILED");
    }
    Logger::Flush();
    return ok;
}
#endif  // CPPHTTPLIB_OPENSSL_SUPPORT

// ──────────────────────────────────────────────────────────────────────────────
// ResolveTlsCertAndKey
// Determines the certificate and private-key PEM paths to use based on
// config_.tls_mode.  Generates a self-signed cert when requested and not yet
// present.  Returns false if TLS cannot be configured.
// ──────────────────────────────────────────────────────────────────────────────
bool WebServer::ResolveTlsCertAndKey(std::string& out_cert,
                                      std::string& out_key) const
{
    if (config_.tls_mode.empty()) return false;

    if (config_.tls_mode == "self_signed") {
        const fs::path cert_dir  = TlsCertsDir();
        const fs::path cert_path = cert_dir / "cert.pem";
        const fs::path key_path  = cert_dir / "key.pem";

        if (!fs::exists(cert_path) || !fs::exists(key_path)) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            Logger::Info("WebServer", "Generating self-signed certificate…");
            if (!GenerateSelfSignedCert(cert_path, key_path)) {
                Logger::Error("WebServer", "Self-signed cert generation failed.");
                return false;
            }
#else
            Logger::Error("WebServer",
                "tls_mode=self_signed but OpenSSL is not compiled in.");
            return false;
#endif
        }
        out_cert = cert_path.string();
        out_key  = key_path.string();
        return true;
    }

    if (config_.tls_mode == "pem") {
        if (!fs::exists(config_.tls_cert_file) ||
            !fs::exists(config_.tls_key_file)) {
            Logger::Error("WebServer",
                "tls_mode=pem: cert or key file not found.");
            return false;
        }
        out_cert = config_.tls_cert_file;
        out_key  = config_.tls_key_file;
        return true;
    }

    // "pfx" / unknown modes
    Logger::Error("WebServer",
        "Unsupported tls_mode: " + config_.tls_mode);
    return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// GetCertExpiryDays
// Returns days until the configured certificate expires, or -1 if unknown.
// ──────────────────────────────────────────────────────────────────────────────
int WebServer::GetCertExpiryDays() const
{
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    std::string cert_path, key_path;
    if (!ResolveTlsCertAndKey(cert_path, key_path)) return -1;

    FILE* f = nullptr;
    if (_wfopen_s(&f, fs::path(cert_path).wstring().c_str(), L"r") != 0 || !f)
        return -1;

    X509* x509 = PEM_read_X509(f, nullptr, nullptr, nullptr);
    fclose(f);
    if (!x509) return -1;

    int days = 0, secs = 0;
    ASN1_TIME_diff(&days, &secs, nullptr, X509_get0_notAfter(x509));
    X509_free(x509);
    return days;
#else
    return -1;
#endif
}

// ──────────────────────────────────────────────────────────────────────────────
// Start
// Creates the httplib server (SSL or plain), registers routes, and launches
// the listener thread(s).  Returns true if the server started successfully.
// Safe to call while already running — returns true immediately.
// ──────────────────────────────────────────────────────────────────────────────
bool WebServer::Start()
{
    if (running_.load()) return true;

    // A listener can exit by itself (for example, a bind failure on a worker
    // thread) while leaving std::thread joinable. Join any previous listener
    // before assigning a new thread, or std::thread will call std::terminate.
    if (server_thread_.joinable() || redirect_thread_.joinable()) {
        Stop();
    }

    impl_ = std::make_unique<WebServerImpl>();

    // ── Build httplib::Server instance ───────────────────────────────────────
    std::string cert_path, key_path;
    const bool  has_tls = ResolveTlsCertAndKey(cert_path, key_path);

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (has_tls) {
        Logger::Info("WebServer",
            "Starting HTTPS server on port " + std::to_string(config_.port) +
            " (cert=" + cert_path + ")");
        impl_->server =
            std::make_unique<httplib::SSLServer>(cert_path.c_str(), key_path.c_str());
    } else {
        Logger::Info("WebServer",
            "Starting HTTP server on port " + std::to_string(config_.port));
        impl_->server = std::make_unique<httplib::Server>();
    }
#else
    if (has_tls) {
        Logger::Error("WebServer",
            "TLS requested but OpenSSL is not compiled in — falling back to HTTP.");
    }
    Logger::Info("WebServer",
        "Starting HTTP server on port " + std::to_string(config_.port));
    impl_->server = std::make_unique<httplib::Server>();
#endif

    if (!impl_->server || !impl_->server->is_valid()) {
        Logger::Error("WebServer", "Failed to create httplib server.");
        return false;
    }

    // Thread-pool size
    const int pool = std::max(1, config_.thread_pool_size);
    impl_->server->new_task_queue = [pool]() {
        return new httplib::ThreadPool(pool);
    };

    // Routes + default assets
    RegisterRoutes();
    EnsureDefaultWebAssets();
    EnsureVendorLibs();

    // ── Listener thread ───────────────────────────────────────────────────────
    const std::string bind_addr = config_.bind_address;
    const int         port      = config_.port;

    if (!impl_->server->bind_to_port(bind_addr.c_str(), port)) {
        Logger::Error("WebServer",
            "Failed to bind listener on " + bind_addr + ":" +
            std::to_string(port));
        return false;
    }

    running_.store(true);

    server_thread_ = std::thread([this, bind_addr, port]() {
        (void)bind_addr;
        (void)port;
        impl_->server->listen_after_bind();
        running_.store(false);
        Logger::Info("WebServer", "Listener thread exited.");
    });

    // ── Optional HTTP→HTTPS redirect server ──────────────────────────────────
    if (has_tls && config_.http_redirect_port > 0 &&
        config_.http_redirect_port != port) {
        impl_->redirect_srv = std::make_unique<httplib::Server>();

        // Build the base HTTPS URL for the Location header.
        std::string base = config_.base_url;
        if (base.empty()) {
            base = "https://" + bind_addr + ":" + std::to_string(port);
        }

        impl_->redirect_srv->Get(".*",
            [base](const httplib::Request& req, httplib::Response& res) {
                res.status = 301;
                res.set_header("Location", base + req.path);
            });

        const int rport = config_.http_redirect_port;
        if (impl_->redirect_srv->bind_to_port(bind_addr.c_str(), rport)) {
            redirect_thread_ = std::thread([this]() {
                impl_->redirect_srv->listen_after_bind();
                Logger::Info("WebServer", "Redirect listener thread exited.");
            });
            Logger::Info("WebServer",
                "HTTP→HTTPS redirect running on port " + std::to_string(rport));
        } else {
            Logger::Error("WebServer",
                "Failed to bind redirect listener on " + bind_addr + ":" +
                std::to_string(rport));
        }
    }

    Logger::Info("WebServer",
        std::string("Server started — ") +
        (has_tls ? "https" : "http") + "://" +
        bind_addr + ":" + std::to_string(port));
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Stop
// Signals both listener threads to exit and waits for them to join.
// Safe to call when already stopped.
// ──────────────────────────────────────────────────────────────────────────────
void WebServer::Stop()
{
    // Signal httplib to stop accepting new connections.
    if (impl_) {
        if (impl_->server)      impl_->server->stop();
        if (impl_->redirect_srv) impl_->redirect_srv->stop();
    }

    // Join threads — critical to avoid std::terminate() in the destructor.
    if (server_thread_.joinable())   server_thread_.join();
    if (redirect_thread_.joinable()) redirect_thread_.join();

    running_.store(false);
    Logger::Info("WebServer", "Server stopped.");
}

// ──────────────────────────────────────────────────────────────────────────────
// Reconfigure
// Stops the server (if running), replaces the config and httplib impl, then
// restarts if it was previously running.
// ──────────────────────────────────────────────────────────────────────────────
void WebServer::Reconfigure(const WebServerConfig& new_config)
{
    const bool was_running = running_.load();
    Stop();

    config_ = new_config;
    impl_   = std::make_unique<WebServerImpl>();  // fresh impl — clears old routes

    if (was_running) Start();
}
