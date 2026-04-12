#include "openai_client.h"

#include <windows.h>
#include <winhttp.h>

#include "util.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

using json = nlohmann::json;

namespace {
struct InternetHandleCloser {
    void operator()(void* handle) const {
        if (handle) {
            WinHttpCloseHandle(static_cast<HINTERNET>(handle));
        }
    }
};

using UniqueInternetHandle = std::unique_ptr<void, InternetHandleCloser>;

struct ParsedUrl {
    bool secure = false;
    INTERNET_PORT port = 0;
    std::wstring host;
    std::wstring path;
};

ParsedUrl CrackUrl(const std::string& url_utf8) {
    std::wstring url = Utf8ToWide(url_utf8);

    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);

    wchar_t host[2048] = {};
    wchar_t path[4096] = {};
    components.lpszHostName = host;
    components.dwHostNameLength = static_cast<DWORD>(std::size(host));
    components.lpszUrlPath = path;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(path));

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        throw std::runtime_error("Invalid URL: " + url_utf8);
    }

    ParsedUrl parsed;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parsed.port = components.nPort;
    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (parsed.path.empty()) {
        parsed.path = L"/";
    }
    return parsed;
}

std::string JoinChatCompletionsUrl(const std::string& base_url) {
    std::string trimmed = base_url;
    while (!trimmed.empty() && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return trimmed + "/chat/completions";
}

bool IsRetryableStatusCode(DWORD status_code) {
    switch (status_code) {
    case 408:
    case 409:
    case 425:
    case 429:
    case 500:
    case 502:
    case 503:
    case 504:
    case 529:
        return true;
    default:
        return false;
    }
}

int ComputeRetryDelayMs(int attempt_index, std::optional<int> retry_after_seconds) {
    if (retry_after_seconds && *retry_after_seconds > 0) {
        const int bounded = std::min(*retry_after_seconds, 30);
        return bounded * 1000;
    }

    const int capped_attempt = std::min(attempt_index, 3);
    return 1000 * (1 << capped_attempt);
}

std::optional<int> QueryRetryAfterSeconds(HINTERNET request) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Retry-After", WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return std::nullopt;
    }

    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Retry-After", buffer.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
        return std::nullopt;
    }

    std::wstring value = TrimWide(buffer.c_str());
    if (value.empty()) {
        return std::nullopt;
    }

    try {
        size_t consumed = 0;
        const int seconds = std::stoi(value, &consumed);
        if (consumed == value.size()) {
            return seconds;
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::string FormatWinHttpError(const std::string& prefix, DWORD error_code) {
    std::ostringstream stream;
    stream << prefix;
    if (error_code != ERROR_SUCCESS) {
        stream << " (WinHTTP error " << error_code << ")";
    }
    return stream.str();
}

std::string FormatHttpErrorMessage(DWORD status_code, const std::string& details, int attempts) {
    std::ostringstream message;
    message << "HTTP " << status_code;
    if (status_code == 529) {
        message << " (provider overloaded)";
    }
    if (attempts > 1) {
        message << " after " << attempts << " attempts";
    }
    if (!details.empty()) {
        message << ": " << details;
    }
    return message.str();
}

std::string ExtractContentString(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }

    if (value.is_array()) {
        std::string combined;
        for (const auto& item : value) {
            if (item.is_string()) {
                combined += item.get<std::string>();
            } else if (item.is_object()) {
                const std::string type = item.value("type", "");
                if (type == "text") {
                    combined += item.value("text", "");
                }
            }
        }
        return combined;
    }

    if (value.is_object()) {
        return value.value("text", "");
    }

    return {};
}

json BuildRequestBody(const ChatRequestOptions& request, bool stream, const std::vector<ChatToolDefinition>& tools = {}) {
    json body;
    body["model"] = request.model.id;
    body["temperature"] = request.temperature;
    body["max_tokens"] = request.max_tokens;
    body["stream"] = stream;
    body["messages"] = json::array();

    if (!request.system_prompt.empty()) {
        body["messages"].push_back({
            {"role", "system"},
            {"content", request.system_prompt},
        });
    }

    for (const auto& message : request.messages) {
        json payload{
            {"role", message.role},
        };
        if (!message.content.empty() || message.role != "assistant") {
            payload["content"] = message.content;
        }
        if (!message.name.empty()) {
            payload["name"] = message.name;
        }
        if (!message.tool_call_id.empty()) {
            payload["tool_call_id"] = message.tool_call_id;
        }
        if (!message.tool_calls_json.empty()) {
            try {
                payload["tool_calls"] = json::parse(message.tool_calls_json);
            } catch (...) {
            }
        }
        body["messages"].push_back(std::move(payload));
    }

    if (!tools.empty()) {
        body["tools"] = json::array();
        body["tool_choice"] = "auto";
        for (const auto& tool : tools) {
            json parameters = json{
                {"type", "object"},
                {"properties", json::object()},
            };
            if (!tool.parameters_json.empty()) {
                try {
                    parameters = json::parse(tool.parameters_json);
                } catch (...) {
                }
            }

            body["tools"].push_back({
                {"type", "function"},
                {"function", {
                    {"name", tool.name},
                    {"description", tool.description},
                    {"parameters", parameters},
                }},
            });
        }
    }

    return body;
}

std::vector<ChatToolCall> ExtractToolCalls(const json& message) {
    std::vector<ChatToolCall> tool_calls;
    if (!message.contains("tool_calls") || !message["tool_calls"].is_array()) {
        return tool_calls;
    }

    for (const auto& item : message["tool_calls"]) {
        if (!item.is_object()) {
            continue;
        }
        ChatToolCall tool_call;
        tool_call.id = item.value("id", "");
        if (item.contains("function") && item["function"].is_object()) {
            const auto& function = item["function"];
            tool_call.name = function.value("name", "");
            if (function.contains("arguments")) {
                if (function["arguments"].is_string()) {
                    tool_call.arguments_json = function["arguments"].get<std::string>();
                } else {
                    tool_call.arguments_json = function["arguments"].dump();
                }
            }
        }
        if (!tool_call.name.empty()) {
            tool_calls.push_back(std::move(tool_call));
        }
    }

    return tool_calls;
}

std::string ExtractErrorMessage(const std::string& body) {
    try {
        const auto payload = json::parse(body);
        if (payload.contains("error")) {
            const auto& error = payload["error"];
            if (error.is_string()) {
                return error.get<std::string>();
            }
            if (error.is_object()) {
                return error.value("message", body);
            }
        }
    } catch (...) {
    }
    return body;
}

std::string ReadEntireResponse(HINTERNET request) {
    std::string response;

    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            break;
        }
        if (available == 0) {
            break;
        }

        std::vector<char> bytes(static_cast<size_t>(available));
        DWORD read = 0;
        if (!WinHttpReadData(request, bytes.data(), available, &read)) {
            break;
        }
        response.append(bytes.data(), bytes.data() + read);
    }

    return response;
}

DWORD QueryStatusCode(HINTERNET request) {
    DWORD status_code = 0;
    DWORD size = sizeof(status_code);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size, WINHTTP_NO_HEADER_INDEX);
    return status_code;
}

ChatExecutionResult RunRequest(const ChatRequestOptions& request, bool stream, const std::function<void(const std::string&)>& on_delta) {
    const std::string url = JoinChatCompletionsUrl(request.provider.base_url);
    const ParsedUrl parsed = CrackUrl(url);
    const std::string body = BuildRequestBody(request, stream).dump();
    constexpr int kMaxAttempts = 4;
    std::string last_error;

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        ChatExecutionResult result;

        UniqueInternetHandle session(WinHttpOpen(L"AgentDesktop/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session) {
            last_error = FormatWinHttpError("Failed to open WinHTTP session.", GetLastError());
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            result.error = last_error;
            return result;
        }

        UniqueInternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session.get()), parsed.host.c_str(), parsed.port, 0));
        if (!connection) {
            last_error = FormatWinHttpError("Failed to connect to host.", GetLastError());
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            result.error = last_error;
            return result;
        }

        const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
        UniqueInternetHandle request_handle(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (!request_handle) {
            result.error = FormatWinHttpError("Failed to create WinHTTP request.", GetLastError());
            return result;
        }

        WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 15000, 15000, 30000, 30000);

        std::wstring headers = L"Content-Type: application/json\r\nAccept: ";
        headers += stream ? L"text/event-stream" : L"application/json";
        headers += L"\r\n";
        if (!request.provider.api_key.empty()) {
            headers += L"Authorization: Bearer ";
            headers += Utf8ToWide(request.provider.api_key);
            headers += L"\r\n";
        }

        if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()), headers.c_str(), static_cast<DWORD>(headers.size()), reinterpret_cast<LPVOID>(const_cast<char*>(body.data())), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
            last_error = FormatWinHttpError("Failed to send HTTP request.", GetLastError());
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            result.error = last_error;
            return result;
        }

        if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
            last_error = FormatWinHttpError("Failed to receive HTTP response.", GetLastError());
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            result.error = last_error;
            return result;
        }

        const DWORD status_code = QueryStatusCode(static_cast<HINTERNET>(request_handle.get()));
        if (status_code < 200 || status_code >= 300) {
            const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
            const std::string details = ExtractErrorMessage(error_body);
            last_error = FormatHttpErrorMessage(status_code, details, attempt);

            if (IsRetryableStatusCode(status_code) && attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, QueryRetryAfterSeconds(static_cast<HINTERNET>(request_handle.get())))));
                continue;
            }

            result.error = last_error;
            return result;
        }

        if (!stream) {
            const std::string response = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
            try {
                const auto payload = json::parse(response);
                result.success = true;
                if (payload.contains("choices") && payload["choices"].is_array() && !payload["choices"].empty()) {
                    const auto& choice = payload["choices"][0];
                    if (choice.contains("message")) {
                        result.full_text = ExtractContentString(choice["message"].value("content", json{}));
                    }
                }
                if (result.full_text.empty()) {
                    result.full_text = "Connection succeeded.";
                }
                return result;
            } catch (...) {
                result.error = "Received a non-JSON response from the provider.";
                return result;
            }
        }

        std::string response_buffer;

        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(static_cast<HINTERNET>(request_handle.get()), &available)) {
                result.error = FormatWinHttpError("Failed while streaming response.", GetLastError());
                return result;
            }
            if (available == 0) {
                break;
            }

            std::vector<char> bytes(static_cast<size_t>(available));
            DWORD read = 0;
            if (!WinHttpReadData(static_cast<HINTERNET>(request_handle.get()), bytes.data(), available, &read)) {
                result.error = FormatWinHttpError("Failed to read a streaming response chunk.", GetLastError());
                return result;
            }

            response_buffer.append(bytes.data(), bytes.data() + read);

            size_t line_end = std::string::npos;
            while ((line_end = response_buffer.find('\n')) != std::string::npos) {
                std::string line = response_buffer.substr(0, line_end);
                response_buffer.erase(0, line_end + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty() || line.rfind("data:", 0) != 0) {
                    continue;
                }

                std::string payload = Trim(line.substr(5));
                if (payload == "[DONE]") {
                    result.success = true;
                    return result;
                }

                try {
                    const auto chunk = json::parse(payload);
                    if (!chunk.contains("choices") || !chunk["choices"].is_array() || chunk["choices"].empty()) {
                        continue;
                    }

                    const auto& choice = chunk["choices"][0];
                    if (choice.contains("delta")) {
                        const auto& delta = choice["delta"];
                        if (delta.contains("content")) {
                            const std::string piece = ExtractContentString(delta["content"]);
                            if (!piece.empty()) {
                                result.full_text += piece;
                                on_delta(piece);
                            }
                        }
                    }
                } catch (...) {
                    result.error = "Failed to parse a streaming SSE chunk.";
                    return result;
                }
            }
        }

        result.success = true;
        return result;
    }

    ChatExecutionResult result;
    result.error = last_error.empty() ? "The request failed after multiple attempts." : last_error;
    return result;
}
}  // namespace

TestConnectionResult OpenAIClient::TestConnection(const ProviderConfig& provider, const ModelConfig& model) {
    TestConnectionResult result;
    try {
        ChatRequestOptions request;
        request.provider = provider;
        request.model = model;
        request.temperature = 0.0;
        request.max_tokens = 8;
        request.messages.push_back(MessageRecord{"user", "Reply with the single word pong.", CurrentTimestampUtc()});

        const ChatExecutionResult response = RunRequest(request, false, [](const std::string&) {});
        result.success = response.success;
        result.message = response.success ? response.full_text : response.error;
    } catch (const std::exception& ex) {
        result.success = false;
        result.message = ex.what();
    } catch (...) {
        result.success = false;
        result.message = "Unexpected error while testing the connection.";
    }
    return result;
}

ChatExecutionResult OpenAIClient::StreamChat(const ChatRequestOptions& request, const std::function<void(const std::string&)>& on_delta) {
    try {
        return RunRequest(request, true, on_delta);
    } catch (const std::exception& ex) {
        ChatExecutionResult result;
        result.error = ex.what();
        return result;
    } catch (...) {
        ChatExecutionResult result;
        result.error = "Unexpected error while sending the chat request.";
        return result;
    }
}

ChatCompletionResult OpenAIClient::CreateToolAwareCompletion(const ChatRequestOptions& request, const std::vector<ChatToolDefinition>& tools) {
    ChatCompletionResult result;

    try {
        const std::string url = JoinChatCompletionsUrl(request.provider.base_url);
        const ParsedUrl parsed = CrackUrl(url);
        const std::string body = BuildRequestBody(request, false, tools).dump();
        constexpr int kMaxAttempts = 4;
        std::string last_error;

        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            UniqueInternetHandle session(WinHttpOpen(L"AgentDesktop/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session) {
                last_error = FormatWinHttpError("Failed to open WinHTTP session.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            UniqueInternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session.get()), parsed.host.c_str(), parsed.port, 0));
            if (!connection) {
                last_error = FormatWinHttpError("Failed to connect to host.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
            UniqueInternetHandle request_handle(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
            if (!request_handle) {
                result.error = FormatWinHttpError("Failed to create WinHTTP request.", GetLastError());
                return result;
            }

            WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 15000, 15000, 30000, 30000);

            std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
            if (!request.provider.api_key.empty()) {
                headers += L"Authorization: Bearer ";
                headers += Utf8ToWide(request.provider.api_key);
                headers += L"\r\n";
            }

            if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()), headers.c_str(), static_cast<DWORD>(headers.size()), reinterpret_cast<LPVOID>(const_cast<char*>(body.data())), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
                last_error = FormatWinHttpError("Failed to send HTTP request.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
                last_error = FormatWinHttpError("Failed to receive HTTP response.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const DWORD status_code = QueryStatusCode(static_cast<HINTERNET>(request_handle.get()));
            if (status_code < 200 || status_code >= 300) {
                const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
                const std::string details = ExtractErrorMessage(error_body);
                last_error = FormatHttpErrorMessage(status_code, details, attempt);
                if (IsRetryableStatusCode(status_code) && attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, QueryRetryAfterSeconds(static_cast<HINTERNET>(request_handle.get())))));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const std::string response = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
            try {
                const auto payload = json::parse(response);
                result.success = true;
                if (payload.contains("choices") && payload["choices"].is_array() && !payload["choices"].empty()) {
                    const auto& choice = payload["choices"][0];
                    result.finish_reason = choice.value("finish_reason", "");
                    if (choice.contains("message")) {
                        result.raw_message_json = choice["message"].dump(2);
                        result.assistant_text = ExtractContentString(choice["message"].value("content", json{}));
                        result.tool_calls = ExtractToolCalls(choice["message"]);
                    }
                }
                return result;
            } catch (...) {
                result.error = "Received a non-JSON response from the provider.";
                return result;
            }
        }

        result.error = last_error.empty() ? "The request failed after multiple attempts." : last_error;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    } catch (...) {
        result.error = "Unexpected error while sending the tool-aware chat request.";
        return result;
    }
}

ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request, const std::vector<ChatToolDefinition>& tools, const std::function<void(const std::string&)>& on_delta) {
    ChatCompletionResult result;

    try {
        const std::string url = JoinChatCompletionsUrl(request.provider.base_url);
        const ParsedUrl parsed = CrackUrl(url);
        const std::string body = BuildRequestBody(request, true, tools).dump();
        constexpr int kMaxAttempts = 4;
        std::string last_error;

        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            UniqueInternetHandle session(WinHttpOpen(L"AgentDesktop/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session) {
                last_error = FormatWinHttpError("Failed to open WinHTTP session.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            UniqueInternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session.get()), parsed.host.c_str(), parsed.port, 0));
            if (!connection) {
                last_error = FormatWinHttpError("Failed to connect to host.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
            UniqueInternetHandle request_handle(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
            if (!request_handle) {
                result.error = FormatWinHttpError("Failed to create WinHTTP request.", GetLastError());
                return result;
            }

            WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 15000, 15000, 30000, 30000);

            std::wstring headers = L"Content-Type: application/json\r\nAccept: text/event-stream\r\n";
            if (!request.provider.api_key.empty()) {
                headers += L"Authorization: Bearer ";
                headers += Utf8ToWide(request.provider.api_key);
                headers += L"\r\n";
            }

            if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()), headers.c_str(), static_cast<DWORD>(headers.size()), reinterpret_cast<LPVOID>(const_cast<char*>(body.data())), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
                last_error = FormatWinHttpError("Failed to send HTTP request.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
                last_error = FormatWinHttpError("Failed to receive HTTP response.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const DWORD status_code = QueryStatusCode(static_cast<HINTERNET>(request_handle.get()));
            if (status_code < 200 || status_code >= 300) {
                const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
                const std::string details = ExtractErrorMessage(error_body);
                last_error = FormatHttpErrorMessage(status_code, details, attempt);
                if (IsRetryableStatusCode(status_code) && attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, QueryRetryAfterSeconds(static_cast<HINTERNET>(request_handle.get())))));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            std::string response_buffer;
            std::vector<ChatToolCall> streamed_tool_calls;

            while (true) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(static_cast<HINTERNET>(request_handle.get()), &available)) {
                    result.error = FormatWinHttpError("Failed while streaming tool-aware response.", GetLastError());
                    return result;
                }
                if (available == 0) {
                    break;
                }

                std::vector<char> bytes(static_cast<size_t>(available));
                DWORD read = 0;
                if (!WinHttpReadData(static_cast<HINTERNET>(request_handle.get()), bytes.data(), available, &read)) {
                    result.error = FormatWinHttpError("Failed to read a streaming tool-aware response chunk.", GetLastError());
                    return result;
                }

                response_buffer.append(bytes.data(), bytes.data() + read);

                size_t line_end = std::string::npos;
                while ((line_end = response_buffer.find('\n')) != std::string::npos) {
                    std::string line = response_buffer.substr(0, line_end);
                    response_buffer.erase(0, line_end + 1);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (line.empty() || line.rfind("data:", 0) != 0) {
                        continue;
                    }

                    std::string payload = Trim(line.substr(5));
                    if (payload == "[DONE]") {
                        result.success = true;
                        result.tool_calls.clear();
                        for (const auto& tool_call : streamed_tool_calls) {
                            if (!tool_call.name.empty()) {
                                result.tool_calls.push_back(tool_call);
                            }
                        }

                        json raw_message{
                            {"role", "assistant"},
                        };
                        if (!result.assistant_text.empty()) {
                            raw_message["content"] = result.assistant_text;
                        }
                        if (!result.tool_calls.empty()) {
                            raw_message["tool_calls"] = json::array();
                            for (const auto& tool_call : result.tool_calls) {
                                raw_message["tool_calls"].push_back({
                                    {"id", tool_call.id},
                                    {"type", "function"},
                                    {"function", {
                                        {"name", tool_call.name},
                                        {"arguments", tool_call.arguments_json},
                                    }},
                                });
                            }
                        }
                        result.raw_message_json = raw_message.dump(2);
                        return result;
                    }

                    try {
                        const auto chunk = json::parse(payload);
                        if (!chunk.contains("choices") || !chunk["choices"].is_array() || chunk["choices"].empty()) {
                            continue;
                        }

                        const auto& choice = chunk["choices"][0];
                        if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                            result.finish_reason = choice["finish_reason"].get<std::string>();
                        }
                        if (!choice.contains("delta") || !choice["delta"].is_object()) {
                            continue;
                        }

                        const auto& delta = choice["delta"];
                        if (delta.contains("content")) {
                            const std::string piece = ExtractContentString(delta["content"]);
                            if (!piece.empty()) {
                                result.assistant_text += piece;
                                on_delta(piece);
                            }
                        }

                        if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                            for (const auto& item : delta["tool_calls"]) {
                                if (!item.is_object()) {
                                    continue;
                                }

                                const int index = item.value("index", static_cast<int>(streamed_tool_calls.size()));
                                if (index < 0) {
                                    continue;
                                }
                                if (static_cast<size_t>(index) >= streamed_tool_calls.size()) {
                                    streamed_tool_calls.resize(static_cast<size_t>(index) + 1);
                                }

                                ChatToolCall& tool_call = streamed_tool_calls[static_cast<size_t>(index)];
                                if (item.contains("id") && item["id"].is_string()) {
                                    tool_call.id = item["id"].get<std::string>();
                                }
                                if (item.contains("function") && item["function"].is_object()) {
                                    const auto& function = item["function"];
                                    if (function.contains("name") && function["name"].is_string()) {
                                        tool_call.name += function["name"].get<std::string>();
                                    }
                                    if (function.contains("arguments") && function["arguments"].is_string()) {
                                        tool_call.arguments_json += function["arguments"].get<std::string>();
                                    }
                                }
                            }
                        }
                    } catch (...) {
                        result.error = "Failed to parse a streaming tool-aware SSE chunk.";
                        return result;
                    }
                }
            }

            result.success = true;
            result.tool_calls.clear();
            for (const auto& tool_call : streamed_tool_calls) {
                if (!tool_call.name.empty()) {
                    result.tool_calls.push_back(tool_call);
                }
            }
            return result;
        }

        result.error = last_error.empty() ? "The request failed after multiple attempts." : last_error;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    } catch (...) {
        result.error = "Unexpected error while streaming the tool-aware chat request.";
        return result;
    }
}
