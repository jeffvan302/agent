#pragma once

#include "types.h"
#include "storage.h"
#include "openai_client.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

class ContextCompressionService {
public:
    explicit ContextCompressionService(AppStorage* storage);
    ~ContextCompressionService();

    // Per-project config management
    std::vector<ContextCompressionConfig> LoadConfigs(const std::string& project_id) const;
    void SaveConfigs(const std::string& project_id, const std::vector<ContextCompressionConfig>& configs) const;
    std::optional<ContextCompressionConfig> GetConfig(const std::string& project_id, const std::string& config_id) const;

    // Global config management (shared across all projects)
    std::vector<ContextCompressionConfig> LoadGlobalConfigs() const;
    void SaveGlobalConfigs(const std::vector<ContextCompressionConfig>& configs) const;
    std::optional<ContextCompressionConfig> GetGlobalConfig(const std::string& config_id) const;

    // Per-chat state
    ChatCompressionState LoadChatState(const std::string& project_id, const std::string& chat_id) const;
    void SaveChatState(const std::string& project_id, const std::string& chat_id, const ChatCompressionState& state) const;

    // Compression decision - uses project's selected compression config
    bool ShouldCompress(const std::string& project_id, const std::string& chat_id, size_t total_messages) const;

    // Force compression on next send (used when context window is ~70% full)
    void MarkCompressionScheduled(const std::string& project_id, const std::string& chat_id);

    // Main compression entry point
    // Returns a block of text to inject into system_prompt (empty if no compression needed)
    std::string CompressConversation(
        const std::vector<MessageRecord>& messages,
        const std::string& project_id,
        const std::string& chat_id,
        const std::string& config_id,
        std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller,
        bool force_rebuild = false,
        const std::string& trigger_reason = "automatic");

    // Build a compressed context block from current state (for preview/diagnostics)
    std::string BuildCompressedContextBlock(
        const ContextCompressionConfig& config,
        const ChatCompressionState& state) const;

    // Token estimation for a single message
    static size_t EstimateMessageTokens(const MessageRecord& message);

private:
    struct ParallelCompressionResult {
        std::string layer2_summary;
        std::string layer3_state_json;
        bool layer2_success = false;
        bool layer3_success = false;
    };

    std::string CompressTruncateTop(
        const std::vector<MessageRecord>& messages,
        const ContextCompressionConfig& config,
        ChatCompressionState& state) const;

    std::string CompressHierarchical(
        const std::vector<MessageRecord>& messages,
        const ContextCompressionConfig& config,
        ChatCompressionState& state,
        std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller) const;

    std::string BuildTruncateTopBlock(
        const std::vector<MessageRecord>& messages,
        const ContextCompressionConfig& config,
        ChatCompressionState& state) const;

    std::string BuildHierarchicalContextBlock(
        const std::vector<MessageRecord>& messages,
        const ContextCompressionConfig& config,
        const ChatCompressionState& state) const;

    ParallelCompressionResult RunParallelCompression(
        const std::vector<MessageRecord>& new_turns,
        const ChatCompressionState& prior_state,
        const ContextCompressionConfig& config) const;

    // Layer implementations
    std::vector<MessageRecord> Layer1_Pin(
        const std::vector<MessageRecord>& messages,
        const Layer1Config& config) const;

    std::string Layer2_Summarize(
        const std::vector<MessageRecord>& prior_summary_context,
        const std::vector<MessageRecord>& new_turns,
        const std::string& prior_summary,
        const std::string& prior_state_json,
        const Layer2Config& config,
        std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller) const;

    std::string Layer3_ExtractState(
        const std::vector<MessageRecord>& new_turns,
        const std::string& prior_state_json,
        const Layer3Config& config,
        std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller) const;

    std::vector<MessageRecord> Layer4_RecencyWindow(
        const std::vector<MessageRecord>& messages,
        const Layer4Config& config,
        size_t budget_remaining) const;

    // Helpers
    std::string MessagesToText(const std::vector<MessageRecord>& messages) const;
    std::string GetNewTurnsSinceIndex(
        const std::vector<MessageRecord>& messages,
        size_t last_compression_index) const;
    std::string MakeCompressionId() const;
    std::string MakeTurnKey(const std::string& project_id, const std::string& chat_id) const;

    AppStorage* storage_ = nullptr;
    mutable std::mutex mutex_;
    mutable std::unordered_set<std::string> force_compression_chats_;  // "project_id:chat_id" keys
};
