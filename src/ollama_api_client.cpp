#include "ollama_api_client.h"
#include "openai_client.h"
#include "ollama_local_server.h"
#include "provider_profiles.h"
#include "util.h"
#include "types.h"

#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

namespace {

static std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

static bool IsOllamaCloudModelId(const std::string& model_id) {
    const std::string normalized = LowerAscii(Trim(model_id));
    constexpr std::string_view kCloudSuffix = ":cloud";
    return normalized.size() >= kCloudSuffix.size() &&
           normalized.compare(normalized.size() - kCloudSuffix.size(), kCloudSuffix.size(), kCloudSuffix) == 0;
}

/* ── WinHTTP helpers ──────────────────────────────────────────────── */

struct HandleDeleter {
    void operator()(HINTERNET h) const { if (h) WinHttpCloseHandle(h); }
};
using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HINTERNET>, HandleDeleter>;

std::string ReadEntireResponse(HINTERNET req) {
    std::string out;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        std::vector<char> buf(avail);
        DWORD read = 0;
        WinHttpReadData(req, buf.data(), avail, &read);
        out.append(buf.data(), read);
    }
    return out;
}

std::string FormatWinHttpError(const std::string& msg, DWORD code) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string detail;
    if (buf) { detail = WideToUtf8(buf); LocalFree(buf); }
    return msg + " (WinHTTP " + std::to_string(code) + ")" + (detail.empty() ? "" : ": " + detail);
}

static void SetLongTimeouts(HINTERNET req) {
    WinHttpSetTimeouts(req, 300000, 300000, 43200000, 43200000);
}

/* ── Build Ollama /api/chat JSON body ─────────────────────────────── */

json BuildOllamaApiBody(const ChatRequestOptions& request, bool stream, const std::vector<ChatToolDefinition>& tools) {
    json body;
    body["model"] = request.model.id;
    body["stream"] = stream;

    json messages = json::array();
    if (!request.system_prompt.empty()) {
        messages.push_back({{"role", "system"}, {"content", request.system_prompt}});
    }
    for (const auto& m : request.messages) {
        json msg{{"role", m.role}};
        if (!m.content.empty() || m.role != "assistant") msg["content"] = m.content;
        if (!m.tool_calls_json.empty()) { try { msg["tool_calls"] = json::parse(m.tool_calls_json); } catch (...) {} }
        messages.push_back(std::move(msg));
    }
    body["messages"] = messages;

    body["options"] = json::object();
    body["options"]["temperature"] = request.temperature;
    if (request.max_tokens > 0) body["options"]["num_predict"] = request.max_tokens;

    if (request.model.ollama_num_threads > 0) body["options"]["num_thread"] = request.model.ollama_num_threads;
    if (request.model.ollama_no_gpu) {
        body["options"]["num_gpu"] = 0;
    } else if (request.model.ollama_gpu_layers > 0) {
        body["options"]["num_gpu"] = request.model.ollama_gpu_layers;
    }
    if (request.model.ollama_context_length > 0) body["options"]["num_ctx"] = request.model.ollama_context_length;

    if (request.model.ollama_keep_alive_seconds > 0) body["keep_alive"] = request.model.ollama_keep_alive_seconds;

    if (!tools.empty()) {
        body["tools"] = json::array();
        for (const auto& t : tools) {
            json tool;
            tool["type"] = "function";
            tool["function"]["name"] = t.name;
            if (!t.description.empty()) tool["function"]["description"] = t.description;
            if (!t.parameters_json.empty()) { try { tool["function"]["parameters"] = json::parse(t.parameters_json); } catch (...) {} }
            body["tools"].push_back(std::move(tool));
        }
    }

    const std::string reasoning = LowerAscii(Trim(request.model.default_reasoning_effort));
    if (reasoning == "none") body["think"] = false;
    else if (!reasoning.empty()) body["think"] = reasoning == "xhigh" ? "high" : reasoning;

    return body;
}

/* ── Extract thinking text from raw Ollama content ──────────────── */

static std::pair<std::string, std::string> ExtractThinkingAndContent(const std::string& raw) {
    std::string thinking;
    std::string content;
    size_t i = 0;
    while (i < raw.size()) {
        size_t open = raw.find("\u003cthink\u003e", i);
        if (open == std::string::npos) {
            content += raw.substr(i);
            break;
        }
        content += raw.substr(i, open - i);
        size_t close = raw.find("\u003c/think\u003e", open + 7);
        if (close == std::string::npos) {
            thinking += raw.substr(open + 7);
            break;
        }
        thinking += raw.substr(open + 7, close - (open + 7));
        i = close + 8;
    }
    return {thinking, content};
}

/* ── Parse single JSON line from Ollama stream ───────────────────── */

struct OllamaStreamAccumulator {
    std::string raw_content;
    std::string response_text;
    std::string thinking_text;
    std::vector<ChatToolCall> tool_calls;
    bool done = false;
    std::string error;
};

bool ConsumeOllamaApiStreamLine(const std::string& line, OllamaStreamAccumulator& acc, const std::function<void(const std::string&)>& on_delta) {
    if (Trim(line).empty()) return true;
    try {
        const auto j = json::parse(line);
        if (j.contains("error") && j["error"].is_string()) {
            acc.error = j.value("error", "Ollama API error");
            return false;
        }
        if (j.contains("message") && j["message"].contains("content")) {
            std::string piece = j["message"].value("content", "");
            if (!piece.empty()) {
                acc.raw_content += piece;
                auto [think, content] = ExtractThinkingAndContent(acc.raw_content);
                acc.thinking_text = think;
                if (content.size() > acc.response_text.size()) {
                    std::string delta = content.substr(acc.response_text.size());
                    acc.response_text = content;
                    if (on_delta) on_delta(delta);
                } else if (content.size() < acc.response_text.size()) {
                    acc.response_text = content;
                }
            }
        }
        if (j.contains("message") && j["message"].contains("tool_calls")) {
            for (const auto& tc : j["message"]["tool_calls"]) {
                ChatToolCall call;
                if (tc.contains("function")) {
                    call.name = tc["function"].value("name", "");
                    if (tc["function"].contains("arguments")) {
                        if (tc["function"]["arguments"].is_string()) {
                            call.arguments_json = tc["function"]["arguments"].get<std::string>();
                        } else {
                            call.arguments_json = tc["function"]["arguments"].dump();
                        }
                    }
                }
                acc.tool_calls.push_back(std::move(call));
            }
        }
        if (j.contains("done") && j["done"].get<bool>()) acc.done = true;
        return true;
    } catch (...) {
        acc.error = "Failed to parse an Ollama stream line.";
        return false;
    }
}

/* ── HTTP Post /api/chat ─────────────────────────────────────────── */

bool PostOllamaApiChat(const std::string& base_url, const json& body, bool stream,
                       const std::function<void(const std::string&)>& on_delta,
                       OllamaStreamAccumulator& out_acc, std::string* error) {
    std::string url = base_url;
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += "/api/chat";

    std::string body_str = body.dump();

    URL_COMPONENTSW comp{};
    comp.dwStructSize = sizeof(comp);
    std::wstring wurl = Utf8ToWide(url);
    wchar_t host_buf[2048] = {};
    comp.lpszHostName = host_buf;
    comp.dwHostNameLength = std::size(host_buf);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &comp)) {
        if (error) *error = "Invalid Ollama base URL: " + url;
        return false;
    }
    std::wstring host(host_buf, comp.dwHostNameLength);
    INTERNET_PORT port = comp.nPort;
    std::wstring path = comp.dwUrlPathLength > 0 ? std::wstring(comp.lpszUrlPath, comp.dwUrlPathLength) : L"/api/chat";
    bool secure = comp.nScheme == INTERNET_SCHEME_HTTPS;

    UniqueHandle session(WinHttpOpen(L"AgentOllama/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { if (error) *error = FormatWinHttpError("WinHttpOpen failed", GetLastError()); return false; }
    UniqueHandle conn(WinHttpConnect(session.get(), host.c_str(), port, 0));
    if (!conn) { if (error) *error = FormatWinHttpError("WinHttpConnect failed", GetLastError()); return false; }
    UniqueHandle req(WinHttpOpenRequest(conn.get(), L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!req) { if (error) *error = FormatWinHttpError("WinHttpOpenRequest failed", GetLastError()); return false; }
    SetLongTimeouts(req.get());

    std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    if (!WinHttpSendRequest(req.get(), headers.c_str(), static_cast<DWORD>(headers.size()), const_cast<char*>(body_str.data()), static_cast<DWORD>(body_str.size()), static_cast<DWORD>(body_str.size()), 0)) {
        if (error) *error = FormatWinHttpError("WinHttpSendRequest failed", GetLastError());
        return false;
    }
    if (!WinHttpReceiveResponse(req.get(), nullptr)) {
        if (error) *error = FormatWinHttpError("WinHttpReceiveResponse failed", GetLastError());
        return false;
    }

    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        std::string err_body = ReadEntireResponse(req.get());
        std::string detail;
        try { detail = json::parse(err_body).value("error", err_body); } catch (...) { detail = err_body; }
        if (error) *error = "Ollama API error " + std::to_string(status) + ": " + detail;
        return false;
    }

    if (!stream) {
        std::string rsp = ReadEntireResponse(req.get());
        try {
            auto j = json::parse(rsp);
            if (j.contains("error") && j["error"].is_string()) { if (error) *error = j.value("error", "Ollama API error"); return false; }
            if (j.contains("message") && j["message"].contains("content")) out_acc.response_text = j["message"].value("content", "");
            if (j.contains("message") && j["message"].contains("tool_calls")) {
                for (const auto& tc : j["message"]["tool_calls"]) {
                    ChatToolCall call;
                    if (tc.contains("function")) {
                        call.name = tc["function"].value("name", "");
                        if (tc["function"].contains("arguments")) {
                            if (tc["function"]["arguments"].is_string()) {
                                call.arguments_json = tc["function"]["arguments"].get<std::string>();
                            } else {
                                call.arguments_json = tc["function"]["arguments"].dump();
                            }
                        }
                    }
                    out_acc.tool_calls.push_back(std::move(call));
                }
            }
            out_acc.done = true;
            return true;
        } catch (...) { if (error) *error = "Ollama returned non-JSON: " + rsp; return false; }
    }

    std::string buffer;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.get(), &avail)) { if (error) *error = FormatWinHttpError("WinHttpQueryDataAvailable failed", GetLastError()); return false; }
        if (avail == 0) break;
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(req.get(), buf.data(), avail, &read)) { if (error) *error = FormatWinHttpError("WinHttpReadData failed", GetLastError()); return false; }
        buffer.append(buf.data(), read);
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!ConsumeOllamaApiStreamLine(line, out_acc, on_delta)) { if (error) *error = out_acc.error; return false; }
            if (out_acc.done) return true;
        }
    }
    if (!buffer.empty()) {
        if (!ConsumeOllamaApiStreamLine(buffer, out_acc, on_delta)) { if (error) *error = out_acc.error; return false; }
    }
    out_acc.done = true;
    return true;
}

/* ── Simple GET helper for Ollama endpoints ──────────────────────── */

bool GetOllamaApi(const std::string& url, std::string* out_body, std::string* error) {
    std::string u = url;
    if (!u.empty() && u.back() == '/') u.pop_back();

    URL_COMPONENTSW comp{};
    comp.dwStructSize = sizeof(comp);
    std::wstring wurl = Utf8ToWide(u);
    wchar_t host_buf[2048] = {};
    comp.lpszHostName = host_buf;
    comp.dwHostNameLength = std::size(host_buf);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &comp)) {
        if (error) *error = "Invalid Ollama URL: " + u;
        return false;
    }
    std::wstring host(host_buf, comp.dwHostNameLength);
    INTERNET_PORT port = comp.nPort;
    std::wstring path = comp.dwUrlPathLength > 0 ? std::wstring(comp.lpszUrlPath, comp.dwUrlPathLength) : L"/";
    bool secure = comp.nScheme == INTERNET_SCHEME_HTTPS;

    UniqueHandle session(WinHttpOpen(L"AgentOllama/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { if (error) *error = FormatWinHttpError("WinHttpOpen failed", GetLastError()); return false; }
    UniqueHandle conn(WinHttpConnect(session.get(), host.c_str(), port, 0));
    if (!conn) { if (error) *error = FormatWinHttpError("WinHttpConnect failed", GetLastError()); return false; }
    UniqueHandle req(WinHttpOpenRequest(conn.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!req) { if (error) *error = FormatWinHttpError("WinHttpOpenRequest failed", GetLastError()); return false; }
    WinHttpSetTimeouts(req.get(), 5000, 5000, 10000, 10000);
    if (!WinHttpSendRequest(req.get(), nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req.get(), nullptr)) {
        if (error) *error = FormatWinHttpError("GET request failed", GetLastError());
        return false;
    }
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, WINHTTP_NO_HEADER_INDEX);
    std::string body = ReadEntireResponse(req.get());
    if (status < 200 || status >= 300) {
        if (error) *error = "Ollama GET error " + std::to_string(status) + ": " + body;
        return false;
    }
    if (out_body) *out_body = body;
    return true;
}

/* ── Simple POST helper for Ollama endpoints (non-streaming) ──────── */

bool PostOllamaApi(const std::string& url, const json& body, std::string* out_response, std::string* error) {
    std::string u = url;
    if (!u.empty() && u.back() == '/') u.pop_back();

    URL_COMPONENTSW comp{};
    comp.dwStructSize = sizeof(comp);
    std::wstring wurl = Utf8ToWide(u);
    wchar_t host_buf[2048] = {};
    comp.lpszHostName = host_buf;
    comp.dwHostNameLength = std::size(host_buf);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &comp)) {
        if (error) *error = "Invalid Ollama URL: " + u;
        return false;
    }
    std::wstring host(host_buf, comp.dwHostNameLength);
    INTERNET_PORT port = comp.nPort;
    std::wstring path = comp.dwUrlPathLength > 0 ? std::wstring(comp.lpszUrlPath, comp.dwUrlPathLength) : L"/";
    bool secure = comp.nScheme == INTERNET_SCHEME_HTTPS;

    std::string body_str = body.dump();
    UniqueHandle session(WinHttpOpen(L"AgentOllama/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { if (error) *error = FormatWinHttpError("WinHttpOpen failed", GetLastError()); return false; }
    UniqueHandle conn(WinHttpConnect(session.get(), host.c_str(), port, 0));
    if (!conn) { if (error) *error = FormatWinHttpError("WinHttpConnect failed", GetLastError()); return false; }
    UniqueHandle req(WinHttpOpenRequest(conn.get(), L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!req) { if (error) *error = FormatWinHttpError("WinHttpOpenRequest failed", GetLastError()); return false; }
    WinHttpSetTimeouts(req.get(), 5000, 5000, 10000, 10000);
    std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    if (!WinHttpSendRequest(req.get(), headers.c_str(), static_cast<DWORD>(headers.size()), const_cast<char*>(body_str.data()), static_cast<DWORD>(body_str.size()), static_cast<DWORD>(body_str.size()), 0)) {
        if (error) *error = FormatWinHttpError("POST request failed", GetLastError());
        return false;
    }
    if (!WinHttpReceiveResponse(req.get(), nullptr)) {
        if (error) *error = FormatWinHttpError("POST response failed", GetLastError());
        return false;
    }
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, WINHTTP_NO_HEADER_INDEX);
    std::string rsp = ReadEntireResponse(req.get());
    if (status < 200 || status >= 300) {
        if (error) *error = "Ollama POST error " + std::to_string(status) + ": " + rsp;
        return false;
    }
    if (out_response) *out_response = rsp;
    return true;
}

/* ── Helper to normalize tool call arguments ─────────────────────── */

struct ToolArgsNorm {
    bool valid = true;
    std::string normalized_json = "{}";
    std::string error;
};

ToolArgsNorm NormalizeToolArgs(const std::string& arguments_json) {
    ToolArgsNorm result;
    const std::string trimmed = Trim(arguments_json);
    if (trimmed.empty()) return result;
    try {
        const auto parsed = json::parse(trimmed);
        if (!parsed.is_object()) {
            result.valid = false;
            result.error = "Tool arguments must be a JSON object.";
            return result;
        }
        result.normalized_json = parsed.dump();
        return result;
    } catch (const std::exception& ex) {
        result.valid = false;
        result.error = ex.what();
        return result;
    } catch (...) {
        result.valid = false;
        result.error = "Tool arguments are not valid JSON.";
        return result;
    }
}

void NormalizeCallArgs(ChatToolCall& call) {
    call.original_arguments_json = call.arguments_json;
    const auto norm = NormalizeToolArgs(call.arguments_json);
    call.arguments_json = norm.normalized_json;
    call.arguments_valid = norm.valid;
    call.arguments_error = norm.error;
}

} // namespace

/* ── Public interface ──────────────────────────────────────────────── */

bool EnsureOllamaLocalServer(const ProviderConfig& provider, std::string* error) {
    return OllamaLocalServerManager::Instance().EnsureRunning(provider, error);
}

void ReportOllamaLocalActivity(const ProviderConfig& provider, const ModelConfig& model) {
    OllamaLocalServerManager::Instance().ReportActivity(provider, model);
}

std::string OllamaLocalBaseUrl(const ProviderConfig& provider) {
    return OllamaLocalServerManager::Instance().BaseUrlForProvider(provider);
}

void StopAllOllamaLocalServers() {
    OllamaLocalServerManager::Instance().StopAll();
}

static bool IsOllamaLocalProvider(const ProviderConfig& provider);

bool IsOllamaModelAvailable(const ProviderConfig& provider, const ModelConfig& model, std::string* error) {
    if (!IsOllamaLocalProvider(provider)) { if (error) *error = "Not an Ollama local provider."; return false; }
    if (!EnsureOllamaLocalServer(provider, error)) return false;
    // Ollama Cloud models are not pulled locally, so they are absent from /api/tags.
    if (IsOllamaCloudModelId(model.id)) return true;
    std::string body;
    if (!GetOllamaApi(OllamaLocalBaseUrl(provider) + "/api/tags", &body, error)) return false;
    try {
        auto j = json::parse(body);
        if (j.contains("models") && j["models"].is_array()) {
            for (const auto& m : j["models"]) {
                std::string name = m.value("name", "");
                if (name == model.id) return true;
                if (!name.empty() && name.find(model.id) == 0) return true;
            }
        }
    } catch (...) {}
    if (error) *error = "Model '" + model.id + "' is not available. Pull it first via the model editor.";
    return false;
}

bool PullOllamaModel(const ProviderConfig& provider, const std::string& model_id, const std::function<void(const std::string&)>& on_status, std::string* error) {
    if (!IsOllamaLocalProvider(provider)) { if (error) *error = "Not an Ollama local provider."; return false; }
    if (!EnsureOllamaLocalServer(provider, error)) return false;

    std::string url = OllamaLocalBaseUrl(provider);
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += "/api/pull";

    std::string body_str = json{{"name", model_id}, {"stream", true}}.dump();

    URL_COMPONENTSW comp{};
    comp.dwStructSize = sizeof(comp);
    std::wstring wurl = Utf8ToWide(url);
    wchar_t host_buf[2048] = {};
    comp.lpszHostName = host_buf;
    comp.dwHostNameLength = std::size(host_buf);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &comp)) {
        if (error) *error = "Invalid Ollama URL: " + url;
        return false;
    }
    std::wstring host(host_buf, comp.dwHostNameLength);
    INTERNET_PORT port = comp.nPort;
    bool secure = comp.nScheme == INTERNET_SCHEME_HTTPS;

    UniqueHandle session(WinHttpOpen(L"AgentOllamaPull/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { if (error) *error = FormatWinHttpError("WinHttpOpen failed", GetLastError()); return false; }
    UniqueHandle conn(WinHttpConnect(session.get(), host.c_str(), port, 0));
    if (!conn) { if (error) *error = FormatWinHttpError("WinHttpConnect failed", GetLastError()); return false; }
    UniqueHandle req(WinHttpOpenRequest(conn.get(), L"POST", L"/api/pull", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!req) { if (error) *error = FormatWinHttpError("WinHttpOpenRequest failed", GetLastError()); return false; }
    WinHttpSetTimeouts(req.get(), 30000, 30000, 43200000, 43200000);

    std::wstring headers = L"Content-Type: application/json\r\nAccept: application/x-ndjson\r\n";
    if (!WinHttpSendRequest(req.get(), headers.c_str(), static_cast<DWORD>(headers.size()), const_cast<char*>(body_str.data()), static_cast<DWORD>(body_str.size()), static_cast<DWORD>(body_str.size()), 0)) {
        if (error) *error = FormatWinHttpError("POST /api/pull failed", GetLastError());
        return false;
    }
    if (!WinHttpReceiveResponse(req.get(), nullptr)) {
        if (error) *error = FormatWinHttpError("POST /api/pull response failed", GetLastError());
        return false;
    }

    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        std::string err_body = ReadEntireResponse(req.get());
        if (error) *error = "Ollama pull error " + std::to_string(status) + ": " + err_body;
        return false;
    }

    std::string buffer;
    std::string last_status;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.get(), &avail)) break;
        if (avail == 0) break;
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(req.get(), buf.data(), avail, &read)) break;
        buffer.append(buf.data(), read);
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            try {
                auto j = json::parse(line);
                if (j.contains("error") && j["error"].is_string()) {
                    if (error) *error = j.value("error", "Pull failed");
                    return false;
                }
                if (j.contains("status") && j["status"].is_string()) {
                    last_status = j.value("status", "");
                    if (on_status) on_status(last_status);
                }
            } catch (...) {}
        }
    }
    return true;
}

static bool IsOllamaLocalProvider(const ProviderConfig& provider) {
    return NormalizeProviderType(provider.provider_type) == "ollama_local";
}

ChatCompletionResult RunOllamaLocalHttpCompletion(
    const ChatRequestOptions& request,
    const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/,
    const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {
    ChatCompletionResult result;
    if (!IsOllamaLocalProvider(request.provider)) { result.error = "Not an Ollama local provider."; return result; }
    if (!EnsureOllamaLocalServer(request.provider, &result.error)) return result;
    ReportOllamaLocalActivity(request.provider, request.model);

    OllamaStreamAccumulator acc;
    if (!PostOllamaApiChat(OllamaLocalBaseUrl(request.provider), BuildOllamaApiBody(request, false, {}), false, {}, acc, &result.error)) return result;
    result.success = true;
    result.assistant_text = acc.response_text;
    result.thinking_text = acc.thinking_text;
    result.message.role = "assistant";
    result.message.content = acc.response_text;
    result.finish_reason = "stop";
    if (!acc.tool_calls.empty()) { result.tool_calls = std::move(acc.tool_calls); result.finish_reason = "tool_calls"; }
    return result;
}

ChatExecutionResult RunOllamaLocalHttpChat(
    const ChatRequestOptions& request,
    const std::function<void(const std::string&)>& on_delta,
    const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/,
    const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {
    ChatExecutionResult result;
    if (!IsOllamaLocalProvider(request.provider)) { result.error = "Not an Ollama local provider."; return result; }
    if (!EnsureOllamaLocalServer(request.provider, &result.error)) return result;
    ReportOllamaLocalActivity(request.provider, request.model);

    OllamaStreamAccumulator acc;
    if (!PostOllamaApiChat(OllamaLocalBaseUrl(request.provider), BuildOllamaApiBody(request, true, {}), true, on_delta, acc, &result.error)) return result;
    if (!acc.done) { result.error = "Ollama stream ended without completion marker."; return result; }
    result.success = true;
    result.full_text = acc.response_text;
    result.thinking_text = acc.thinking_text;
    return result;
}

ChatCompletionResult RunOllamaLocalHttpToolPrompt(
    const ChatRequestOptions& request,
    const std::vector<ChatToolDefinition>& tools,
    const std::function<void(const std::string&)>& on_delta,
    const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/,
    const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {
    ChatCompletionResult result;
    if (!IsOllamaLocalProvider(request.provider)) { result.error = "Not an Ollama local provider."; return result; }
    if (!EnsureOllamaLocalServer(request.provider, &result.error)) return result;
    ReportOllamaLocalActivity(request.provider, request.model);

    OllamaStreamAccumulator acc;
    bool stream = !on_delta ? false : true;
    if (!PostOllamaApiChat(OllamaLocalBaseUrl(request.provider), BuildOllamaApiBody(request, stream, tools), stream, on_delta, acc, &result.error)) return result;
    result.success = true;
    result.assistant_text = acc.response_text;
    result.thinking_text = acc.thinking_text;
    result.message.role = "assistant";
    result.message.content = acc.response_text;
    result.finish_reason = "stop";
    if (!acc.tool_calls.empty()) { result.tool_calls = std::move(acc.tool_calls); result.finish_reason = "tool_calls"; }
    for (auto& tc : result.tool_calls) NormalizeCallArgs(tc);
    return result;
}
