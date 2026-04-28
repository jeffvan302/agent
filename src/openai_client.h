#pragma once

#include "types.h"

#include <optional>
#include <functional>
#include <string>
#include <vector>

class AppStorage;

struct ChatRequestOptions {
    ProviderConfig provider;
    ModelConfig model;
    std::string system_prompt;
    double temperature = 0.2;
    int max_tokens = 1024;
    std::vector<MessageRecord> messages;
    int retry_budget_seconds_override = 0;
    int retry_interval_seconds_override = 0;
};

struct ChatToolDefinition {
    std::string name;
    std::string description;
    std::string parameters_json;
};

struct ChatToolCall {
    std::string id;
    std::string name;
    std::string arguments_json;
    bool arguments_valid = true;
    std::string original_arguments_json;
    std::string arguments_error;
};

struct TestConnectionResult {
    bool success = false;
    std::string message;
    std::string details_log;
};

struct ProviderQueueStatus {
    std::string provider_id;
    std::string provider_name;
    std::string state; // queued, active
    int queue_position = 0;
    int queue_depth = 0;
    int active_requests = 0;
    int max_active_requests = 0; // 0 means no limit
    int max_queue_size = 0;      // 0 means no limit
};

struct ChatExecutionResult {
    bool success = false;
    std::string full_text;
    std::string error;
    std::string thinking_text;
};

struct ChatCompletionResult {
    bool success = false;
    std::string assistant_text;
    std::vector<ChatToolCall> tool_calls;
    std::string finish_reason;
    std::string raw_message_json;
    std::string error;
    MessageRecord message;  // Full message record with role and content
    std::string thinking_text;
};

class OpenAIClient {
public:
    static TestConnectionResult TestConnection(const ProviderConfig& provider, const ModelConfig& model);
    static ChatExecutionResult StreamChat(const ChatRequestOptions& request,
                                          const std::function<void(const std::string&)>& on_delta,
                                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status = {},
                                          const std::function<void(const std::string&, const std::string&)>& on_activity_status = {});
    static ChatCompletionResult CreateToolAwareCompletion(const ChatRequestOptions& request,
                                                          const std::vector<ChatToolDefinition>& tools,
                                                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status = {},
                                                          const std::function<void(const std::string&, const std::string&)>& on_activity_status = {});
    static ChatCompletionResult StreamToolAwareCompletion(const ChatRequestOptions& request,
                                                          const std::vector<ChatToolDefinition>& tools,
                                                          const std::function<void(const std::string&)>& on_delta,
                                                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status = {},
                                                          const std::function<void(const std::string&, const std::string&)>& on_activity_status = {});
    // Non-streaming completion for compression model calls
    static ChatCompletionResult CreateSimpleCompletion(const ChatRequestOptions& request,
                                                       const std::function<void(const ProviderQueueStatus&)>& on_queue_status = {},
                                                       const std::function<void(const std::string&, const std::string&)>& on_activity_status = {});

    // Provider cache for compression model calls (L2/L3 of HSC)
    static void SetProviderCache(const std::vector<ProviderConfig>& providers);
    static std::optional<ProviderConfig> LookupProvider(const std::string& provider_id);
    static void SetStorage(AppStorage* storage);
};
