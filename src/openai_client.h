#pragma once

#include "types.h"

#include <optional>
#include <functional>
#include <string>
#include <vector>

struct ChatRequestOptions {
    ProviderConfig provider;
    ModelConfig model;
    std::string system_prompt;
    double temperature = 0.2;
    int max_tokens = 1024;
    std::vector<MessageRecord> messages;
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
};

struct ChatExecutionResult {
    bool success = false;
    std::string full_text;
    std::string error;
};

struct ChatCompletionResult {
    bool success = false;
    std::string assistant_text;
    std::vector<ChatToolCall> tool_calls;
    std::string finish_reason;
    std::string raw_message_json;
    std::string error;
    MessageRecord message;  // Full message record with role and content
};

class OpenAIClient {
public:
    static TestConnectionResult TestConnection(const ProviderConfig& provider, const ModelConfig& model);
    static ChatExecutionResult StreamChat(const ChatRequestOptions& request, const std::function<void(const std::string&)>& on_delta);
    static ChatCompletionResult CreateToolAwareCompletion(const ChatRequestOptions& request, const std::vector<ChatToolDefinition>& tools);
    static ChatCompletionResult StreamToolAwareCompletion(const ChatRequestOptions& request, const std::vector<ChatToolDefinition>& tools, const std::function<void(const std::string&)>& on_delta);
    // Non-streaming completion for compression model calls
    static ChatCompletionResult CreateSimpleCompletion(const ChatRequestOptions& request);

    // Provider cache for compression model calls (L2/L3 of HSC)
    static void SetProviderCache(const std::vector<ProviderConfig>& providers);
    static std::optional<ProviderConfig> LookupProvider(const std::string& provider_id);
};
