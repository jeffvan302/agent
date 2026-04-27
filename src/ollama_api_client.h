#pragma once

#include <string>
#include <functional>
#include <vector>

struct ProviderConfig;
struct ModelConfig;
struct ChatRequestOptions;
struct ChatToolDefinition;
struct ChatCompletionResult;
struct ChatExecutionResult;
struct ProviderQueueStatus;

void StopAllOllamaLocalServers();

std::string OllamaLocalBaseUrl(const ProviderConfig& provider);

bool IsOllamaModelAvailable(const ProviderConfig& provider, const ModelConfig& model, std::string* error);

bool TestOllamaEmbeddingConnection(const ProviderConfig& provider, const ModelConfig& model, std::string* message);

bool PullOllamaModel(const ProviderConfig& provider, const std::string& model_id,
                     const std::function<void(const std::string&)>& on_status,
                     std::string* error);

ChatCompletionResult RunOllamaLocalHttpCompletion(
    const ChatRequestOptions& request,
    const std::function<void(const ProviderQueueStatus&)>& on_queue_status,
    const std::function<void(const std::string&, const std::string&)>& on_activity_status);

ChatExecutionResult RunOllamaLocalHttpChat(
    const ChatRequestOptions& request,
    const std::function<void(const std::string&)>& on_delta,
    const std::function<void(const ProviderQueueStatus&)>& on_queue_status,
    const std::function<void(const std::string&, const std::string&)>& on_activity_status);

ChatCompletionResult RunOllamaLocalHttpToolPrompt(
    const ChatRequestOptions& request,
    const std::vector<ChatToolDefinition>& tools,
    const std::function<void(const std::string&)>& on_delta,
    const std::function<void(const ProviderQueueStatus&)>& on_queue_status,
    const std::function<void(const std::string&, const std::string&)>& on_activity_status);
