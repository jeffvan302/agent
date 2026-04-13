#include "context_compression.h"

#include "openai_client.h"
#include "util.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_set>

using json = nlohmann::json;

namespace {
std::string TrimStr(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) {
        ++start;
    }
    auto end = s.end();
    while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(start, end);
}

size_t CountTokensSimple(const std::string& text) {
    // Rough token estimation: ~4 chars per token for English
    if (text.empty()) return 0;
    return std::max<size_t>(1, text.size() / 4);
}

std::string GenerateId(const std::string& prefix) {
    std::string id = prefix + "_";
    const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 12; ++i) {
        id += std::string(1, chars[rand() % (sizeof(chars) - 1)]);
    }
    return id;
}

std::string CompressionStrategyToString(ContextCompressionStrategy strategy) {
    switch (strategy) {
    case ContextCompressionStrategy::TruncateTop:
        return "truncate_top";
    case ContextCompressionStrategy::HierarchicalStructured:
        return "hierarchical_structured";
    case ContextCompressionStrategy::None:
    default:
        return "none";
    }
}
}  // namespace

// ============================================================================
// Layer 1: Verbatim Pinning
// ============================================================================

bool MessageMatchesPinPattern(const MessageRecord& message, bool match_code, bool match_urls,
                            bool match_numbers, bool match_explicit_instructions, bool match_user_flagged) {
    const std::string& content = message.content;
    if (match_code && content.find("```") != std::string::npos) {
        return true;
    }
    if (match_urls) {
        if (content.find("http://") != std::string::npos ||
            content.find("https://") != std::string::npos) {
            return true;
        }
        // File paths
        std::regex path_pattern(R"([A-Za-z]:[\\\/][^\s]+|/[A-Za-z0-9_\-\.\/]+)");
        if (std::regex_search(content, path_pattern)) {
            return true;
        }
    }
    if (match_numbers) {
        // 4+ digit numbers, version numbers, dates
        std::regex num_pattern(R"(\b\d{4,}[\d\.\-]*\b)");
        if (std::regex_search(content, num_pattern)) {
            return true;
        }
    }
    if (match_explicit_instructions && message.role == "user") {
        std::regex instruction_pattern(
            R"(\b(must|should|need to|required|requirement|do not|don't|never|always|make sure|ensure|please)\b)",
            std::regex_constants::icase);
        if (std::regex_search(content, instruction_pattern)) {
            return true;
        }
    }
    if (match_user_flagged) {
        std::regex flag_pattern(R"(\[PIN\]|\b(remember this|important|don't forget|do not forget|keep this)\b)",
            std::regex_constants::icase);
        if (std::regex_search(content, flag_pattern)) {
            return true;
        }
    }
    return false;
}

std::vector<MessageRecord> ContextCompressionService::Layer1_Pin(
    const std::vector<MessageRecord>& messages,
    const Layer1Config& config) const {

    if (!config.enabled) {
        return {};
    }

    std::vector<MessageRecord> pinned;
    const size_t max_pins = static_cast<size_t>(config.max_pins);

    // Always pin the first user message
    if (config.pin_first_message && !messages.empty() && max_pins > 0) {
        for (size_t i = 0; i < messages.size(); ++i) {
            if (messages[i].role == "user") {
                pinned.push_back(messages[i]);
                break;
            }
        }
    }

    // Scan all messages for pattern matches
    for (size_t i = 0; i < messages.size() && pinned.size() < max_pins; ++i) {
        const auto& msg = messages[i];
        if (MessageMatchesPinPattern(msg,
            config.pin_code_blocks, config.pin_urls,
            config.pin_numbers, config.pin_explicit_instructions, config.pin_user_flagged)) {
            // Avoid duplicates
            bool already_pinned = false;
            for (const auto& p : pinned) {
                if (p.content == msg.content && p.role == msg.role) {
                    already_pinned = true;
                    break;
                }
            }
            if (!already_pinned) {
                pinned.push_back(msg);
            }
        }
    }

    return pinned;
}

// ============================================================================
// Layer 2: Model-Generated Running Summary (Regenerative)
// ============================================================================

std::string ContextCompressionService::Layer2_Summarize(
    const std::vector<MessageRecord>& prior_summary_context,
    const std::vector<MessageRecord>& new_turns,
    const std::string& prior_summary,
    const std::string& prior_state_json,
    const Layer2Config& config,
    std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller) const {
    (void)prior_summary_context;

    if (!config.enabled || config.model_id.empty() || config.model_provider_id.empty()) {
        return prior_summary;
    }

    std::ostringstream prompt;
    prompt << "You are compressing a conversation for future context.\n\n";

    if (prior_summary.empty()) {
        prompt << "PREVIOUS SUMMARY: (No previous summary — this is the first compression.)\n\n";
    } else {
        prompt << "PREVIOUS SUMMARY:\n" << prior_summary << "\n\n";
    }

    prompt << "NEW TURNS SINCE LAST SUMMARY:\n";
    if (new_turns.empty()) {
        prompt << "(No new turns)\n";
    } else {
        for (const auto& turn : new_turns) {
            prompt << "[" << turn.role << "]: " << turn.content << "\n\n";
        }
    }

    prompt << "CURRENT STRUCTURED STATE (for reference, do not duplicate):\n";
    if (prior_state_json.empty()) {
        prompt << "{}\n";
    } else {
        prompt << prior_state_json << "\n";
    }

    prompt << "\nGenerate a concise narrative summary that captures:\n";
    prompt << "1. The user's original goal and any evolution of that goal\n";
    prompt << "2. Key decisions made and their reasoning\n";
    prompt << "3. Approaches attempted, including failures and why they failed\n";
    prompt << "4. Current status and what the next step should be\n";
    prompt << "5. Any constraints, preferences, or requirements the user has stated\n\n";
    prompt << "Rules:\n";
    prompt << "- Do NOT include specific code, URLs, or exact numbers (those are preserved elsewhere)\n";
    prompt << "- Do NOT summarize-from-summary: treat the previous summary as context, but prioritize\n";
    prompt << "  accuracy from the new turns if there are any contradictions\n";
    prompt << "- Keep the summary under " << config.max_tokens << " tokens\n";
    prompt << "- Write in third person past/present tense (\"The user asked for...\", \"The current approach is...\")\n";
    prompt << "- Flag any ambiguity or unresolved questions explicitly\n";

    ChatRequestOptions opts;
    opts.model.id = config.model_id;
    opts.messages.push_back(MessageRecord{});
    opts.messages.back().role = "user";
    opts.messages.back().content = prompt.str();
    opts.max_tokens = config.max_tokens;
    opts.temperature = 0.3;

    auto result = model_caller(opts);
    if (result && result->success && !result->message.content.empty()) {
        return TrimStr(result->message.content);
    }

    return prior_summary;
}

// ============================================================================
// Layer 3: Structured State Extraction
// ============================================================================

std::string ContextCompressionService::Layer3_ExtractState(
    const std::vector<MessageRecord>& new_turns,
    const std::string& prior_state_json,
    const Layer3Config& config,
    std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller) const {

    if (!config.enabled || config.model_id.empty() || config.model_provider_id.empty()) {
        return prior_state_json;
    }

    std::ostringstream prompt;
    prompt << "You are updating a structured state object for a conversation.\n\n";

    prompt << "CURRENT STATE:\n";
    if (prior_state_json.empty()) {
        prompt << "{}\n";
    } else {
        prompt << prior_state_json << "\n";
    }

    prompt << "\nNEW TURNS:\n";
    if (new_turns.empty()) {
        prompt << "(No new turns)\n";
    } else {
        for (size_t i = 0; i < new_turns.size(); ++i) {
            prompt << "[Turn " << i << "][" << new_turns[i].role << "]: " << new_turns[i].content << "\n";
        }
    }

    prompt << "\nUpdate the state object based on the new turns. Rules:\n";
    prompt << "- Only ADD or MODIFY fields that the new turns provide evidence for.\n";
    prompt << "- Do NOT remove existing entries unless the user explicitly contradicts or revokes them.\n";
    prompt << "- For decisions and failed_approaches, include the turn index for traceability.\n";
    prompt << "- Keep all values concise — single sentences, not paragraphs.\n";
    prompt << "- Return ONLY valid JSON matching the schema. No commentary.\n\n";

    prompt << "Schema:\n";
    prompt << R"({
  "primary_goal": "string",
  "constraints": ["string"],
  "preferences": ["string"],
  "decisions": [{"what": "string", "why": "string", "turn": int}],
  "failed_approaches": [{"approach": "string", "reason_abandoned": "string", "turn": int}],
  "open_questions": ["string"],
  "entities": {"key": "value"},
  "current_phase": "string",
  "user_flagged_notes": ["string"]
})";

    ChatRequestOptions opts;
    opts.model.id = config.model_id;
    opts.messages.push_back(MessageRecord{});
    opts.messages.back().role = "user";
    opts.messages.back().content = prompt.str();
    opts.max_tokens = config.max_tokens;
    opts.temperature = 0.2;

    auto result = model_caller(opts);
    if (result && result->success && !result->message.content.empty()) {
        // Try to extract JSON from the response
        std::string content = result->message.content;
        // Find JSON bounds
        size_t start = content.find('{');
        size_t end = content.rfind('}');
        if (start != std::string::npos && end != std::string::npos && end > start) {
            return content.substr(start, end - start + 1);
        }
        return content;
    }

    return prior_state_json;
}

// ============================================================================
// Layer 4: Recency Window
// ============================================================================

std::vector<MessageRecord> ContextCompressionService::Layer4_RecencyWindow(
    const std::vector<MessageRecord>& messages,
    const Layer4Config& config,
    size_t budget_remaining) const {

    if (!config.enabled) {
        return {};
    }

    std::vector<MessageRecord> window;
    size_t token_count = 0;
    size_t min_turns = static_cast<size_t>(config.min_recent_turns);

    // Walk backward from most recent message
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        size_t msg_tokens = EstimateMessageTokens(*it);
        if (token_count + msg_tokens > budget_remaining && window.size() >= min_turns) {
            break;
        }
        window.insert(window.begin(), *it);
        token_count += msg_tokens;
    }

    return window;
}

std::string ContextCompressionService::BuildTruncateTopBlock(
    const std::vector<MessageRecord>& messages,
    const ContextCompressionConfig& config,
    ChatCompressionState& state) const {

    int keep = config.truncate_top_keep_messages;
    if (keep <= 0) keep = 20;

    std::ostringstream block;
    block << "=== RECENT CONTEXT (Rolling Window) ===\n\n";

    const size_t start = messages.size() > static_cast<size_t>(keep)
        ? messages.size() - static_cast<size_t>(keep)
        : 0;
    for (size_t i = start; i < messages.size(); ++i) {
        block << "[" << messages[i].role << "]: " << messages[i].content << "\n\n";
    }
    if (messages.empty()) {
        block << "(No messages in this chat yet)\n\n";
    }

    state.last_compression_message_index = messages.size();
    block << "=== END RECENT CONTEXT ===\n";
    return block.str();
}

std::string ContextCompressionService::BuildHierarchicalContextBlock(
    const std::vector<MessageRecord>& messages,
    const ContextCompressionConfig& config,
    const ChatCompressionState& state) const {

    const auto& pinned = state.layer1_pinned_messages;

    size_t pinned_tokens = 0;
    for (const auto& p : pinned) {
        pinned_tokens += EstimateMessageTokens(p);
    }
    size_t summary_tokens = CountTokensSimple(state.layer2_previous_summary);
    size_t state_tokens = CountTokensSimple(state.layer3_previous_state_json);
    size_t overhead = 200;
    size_t used = pinned_tokens + summary_tokens + state_tokens + overhead;
    size_t remaining = used >= 4000 ? 1000 : 4000 - used;

    std::vector<MessageRecord> recency = Layer4_RecencyWindow(messages, config.layers.layer4, remaining);

    std::ostringstream block;
    block << "=== COMPRESSED CONTEXT ===\n\n";

    block << "## Conversation State (Layer 3)\n";
    if (!state.layer3_previous_state_json.empty()) {
        block << state.layer3_previous_state_json << "\n\n";
    } else {
        block << "{}\n\n";
    }

    block << "## Conversation Summary (Layer 2)\n";
    block << (state.layer2_previous_summary.empty() ? "(No summary yet)" : state.layer2_previous_summary) << "\n\n";

    block << "## Pinned Messages - Verbatim (Layer 1)\n";
    if (!pinned.empty()) {
        for (const auto& p : pinned) {
            block << "[" << p.role << "]: " << p.content << "\n\n";
        }
    } else {
        block << "(No pinned messages)\n\n";
    }

    block << "## Recent Conversation - Verbatim (Layer 4)\n";
    if (!recency.empty()) {
        for (const auto& r : recency) {
            block << "[" << r.role << "]: " << r.content << "\n\n";
        }
    } else {
        block << "(No recent turns in window)\n\n";
    }

    block << "=== END COMPRESSED CONTEXT ===\n";
    return block.str();
}

// ============================================================================
// Token Estimation
// ============================================================================

size_t ContextCompressionService::EstimateMessageTokens(const MessageRecord& message) {
    size_t tokens = 8;  // Chat message framing overhead
    tokens += CountTokensSimple(message.role);
    tokens += CountTokensSimple(message.content);
    tokens += CountTokensSimple(message.name);
    tokens += CountTokensSimple(message.tool_call_id);
    tokens += CountTokensSimple(message.tool_calls_json);
    return tokens;
}

// ============================================================================
// Helpers
// ============================================================================

std::string ContextCompressionService::MessagesToText(const std::vector<MessageRecord>& messages) const {
    std::ostringstream oss;
    for (const auto& msg : messages) {
        oss << "[" << msg.role << "]: " << msg.content << "\n\n";
    }
    return oss.str();
}

std::string ContextCompressionService::GetNewTurnsSinceIndex(
    const std::vector<MessageRecord>& messages,
    size_t last_compression_index) const {
    std::ostringstream oss;
    for (size_t i = last_compression_index; i < messages.size(); ++i) {
        oss << "[" << messages[i].role << "]: " << messages[i].content << "\n\n";
    }
    return oss.str();
}

std::string ContextCompressionService::MakeCompressionId() const {
    return GenerateId("cc");
}

std::string ContextCompressionService::MakeTurnKey(const std::string& project_id, const std::string& chat_id) const {
    return project_id + ":" + chat_id;
}

// ============================================================================
// Parallel Compression (L2 and L3 run simultaneously)
// ============================================================================

ContextCompressionService::ParallelCompressionResult ContextCompressionService::RunParallelCompression(
    const std::vector<MessageRecord>& new_turns,
    const ChatCompressionState& prior_state,
    const ContextCompressionConfig& config) const {

    ParallelCompressionResult result;
    std::mutex result_mutex;

    auto layer2_config = config.layers.layer2;
    auto layer3_config = config.layers.layer3;

    // L2 thread
    std::thread l2_thread([&]() {
        if (!layer2_config.enabled || layer2_config.model_id.empty()) {
            std::lock_guard<std::mutex> lock(result_mutex);
            result.layer2_summary = prior_state.layer2_previous_summary;
            result.layer2_success = true;
            return;
        }

        // Build prior summary context: include the prior summary plus some recent messages for grounding
        std::vector<MessageRecord> summary_context;
        MessageRecord ctx;
        if (!prior_state.layer2_previous_summary.empty()) {
            ctx.role = "system";
            ctx.content = "[Prior Summary]\n" + prior_state.layer2_previous_summary;
            summary_context.push_back(ctx);
        }

        auto summary = Layer2_Summarize(summary_context, new_turns,
            prior_state.layer2_previous_summary, prior_state.layer3_previous_state_json,
            layer2_config,
            [](const ChatRequestOptions&) -> std::optional<ChatCompletionResult> { return std::nullopt; });

        // Note: actual model call needs to happen via model_caller passed to CompressConversation
        // This is a placeholder - the actual parallel execution happens in CompressHierarchical
        std::lock_guard<std::mutex> lock(result_mutex);
        result.layer2_summary = summary;
        result.layer2_success = true;
    });

    // L3 thread
    std::thread l3_thread([&]() {
        if (!layer3_config.enabled || layer3_config.model_id.empty()) {
            std::lock_guard<std::mutex> lock(result_mutex);
            result.layer3_state_json = prior_state.layer3_previous_state_json;
            result.layer3_success = true;
            return;
        }

        auto state = Layer3_ExtractState(new_turns, prior_state.layer3_previous_state_json,
            layer3_config,
            [](const ChatRequestOptions&) -> std::optional<ChatCompletionResult> { return std::nullopt; });

        std::lock_guard<std::mutex> lock(result_mutex);
        result.layer3_state_json = state;
        result.layer3_success = true;
    });

    l2_thread.join();
    l3_thread.join();

    return result;
}

// ============================================================================
// Truncate Top Strategy
// ============================================================================

std::string ContextCompressionService::CompressTruncateTop(
    const std::vector<MessageRecord>& messages,
    const ContextCompressionConfig& config,
    ChatCompressionState& state) const {

    int keep = config.truncate_top_keep_messages;
    if (keep <= 0) keep = 20;

    if (messages.size() <= static_cast<size_t>(keep)) {
        // Not enough messages to truncate
        state.last_compression_message_index = messages.size();
        return "";
    }
    return BuildTruncateTopBlock(messages, config, state);
}

// ============================================================================
// Hierarchical Structured Compression Strategy
// ============================================================================

std::string ContextCompressionService::CompressHierarchical(
    const std::vector<MessageRecord>& messages,
    const ContextCompressionConfig& config,
    ChatCompressionState& state,
    std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller) const {

    size_t last_idx = state.last_compression_message_index;
    if (last_idx >= messages.size()) {
        return "";
    }

    // Get new turns since last compression
    std::vector<MessageRecord> new_turns;
    for (size_t i = last_idx; i < messages.size(); ++i) {
        new_turns.push_back(messages[i]);
    }

    if (new_turns.empty()) {
        return "";
    }

    // Layer 1: Verbatim Pinning (on full conversation for context)
    std::vector<MessageRecord> pinned = Layer1_Pin(messages, config.layers.layer1);

    // Parallel L2 and L3 using model calls
    ParallelCompressionResult parallel_result;
    std::mutex result_mutex;

    auto layer2_config = config.layers.layer2;
    auto layer3_config = config.layers.layer3;

    // Thread 1: L2 Summary
    std::thread l2_thread([&]() {
        if (!layer2_config.enabled || layer2_config.model_id.empty()) {
            std::lock_guard<std::mutex> lock(result_mutex);
            parallel_result.layer2_summary = state.layer2_previous_summary;
            parallel_result.layer2_success = true;
            return;
        }

        std::vector<MessageRecord> summary_context;
        if (!state.layer2_previous_summary.empty()) {
            MessageRecord ctx;
            ctx.role = "system";
            ctx.content = "[Prior Summary]\n" + state.layer2_previous_summary;
            summary_context.push_back(ctx);
        }

        ChatRequestOptions opts;
        opts.model.id = layer2_config.model_id;
        if (auto provider = OpenAIClient::LookupProvider(layer2_config.model_provider_id)) {
            opts.provider = *provider;
        }
        opts.messages.push_back(MessageRecord{});
        opts.messages.back().role = "user";

        std::ostringstream prompt;
        prompt << "You are compressing a conversation for future context.\n\n";
        if (state.layer2_previous_summary.empty()) {
            prompt << "PREVIOUS SUMMARY: (No previous summary — this is the first compression.)\n\n";
        } else {
            prompt << "PREVIOUS SUMMARY:\n" << state.layer2_previous_summary << "\n\n";
        }
        prompt << "NEW TURNS SINCE LAST SUMMARY:\n";
        for (const auto& t : new_turns) {
            prompt << "[" << t.role << "]: " << t.content << "\n\n";
        }
        prompt << "CURRENT STRUCTURED STATE (for reference):\n";
        prompt << (state.layer3_previous_state_json.empty() ? "{}" : state.layer3_previous_state_json) << "\n\n";
        prompt << "Generate a concise narrative summary (under " << layer2_config.max_tokens << " tokens) capturing:\n";
        prompt << "1. User's goal and any evolution\n";
        prompt << "2. Key decisions and reasoning\n";
        prompt << "3. Approaches tried and failures\n";
        prompt << "4. Current status and next steps\n";
        prompt << "5. Stated constraints and preferences\n\n";
        prompt << "Rules:\n";
        prompt << "- Do NOT include specific code, URLs, or exact numbers\n";
        prompt << "- Write in third person past/present tense\n";
        prompt << "- Flag ambiguities explicitly\n";

        opts.messages.back().content = prompt.str();
        opts.max_tokens = layer2_config.max_tokens;
        opts.temperature = 0.3;

        auto result = model_caller(opts);
        std::lock_guard<std::mutex> lock(result_mutex);
        if (result && result->success) {
            parallel_result.layer2_summary = TrimStr(result->message.content);
            parallel_result.layer2_success = true;
        } else {
            parallel_result.layer2_summary = state.layer2_previous_summary;
            parallel_result.layer2_success = false;
        }
    });

    // Thread 2: L3 State Extraction
    std::thread l3_thread([&]() {
        if (!layer3_config.enabled || layer3_config.model_id.empty()) {
            std::lock_guard<std::mutex> lock(result_mutex);
            parallel_result.layer3_state_json = state.layer3_previous_state_json;
            parallel_result.layer3_success = true;
            return;
        }

        ChatRequestOptions opts;
        opts.model.id = layer3_config.model_id;
        if (auto provider = OpenAIClient::LookupProvider(layer3_config.model_provider_id)) {
            opts.provider = *provider;
        }
        opts.messages.push_back(MessageRecord{});
        opts.messages.back().role = "user";

        std::ostringstream prompt;
        prompt << "You are updating a structured state object for a conversation.\n\n";
        prompt << "CURRENT STATE:\n";
        prompt << (state.layer3_previous_state_json.empty() ? "{}" : state.layer3_previous_state_json) << "\n\n";
        prompt << "NEW TURNS:\n";
        for (size_t i = 0; i < new_turns.size(); ++i) {
            prompt << "[Turn " << i << "][" << new_turns[i].role << "]: " << new_turns[i].content << "\n";
        }
        prompt << "\nUpdate the state object based on the new turns. Rules:\n";
        prompt << "- Only ADD or MODIFY fields that new turns provide evidence for\n";
        prompt << "- Do NOT remove existing entries unless user explicitly revokes them\n";
        prompt << "- For decisions and failed_approaches, include turn index\n";
        prompt << "- Keep values concise — single sentences\n";
        prompt << "- Return ONLY valid JSON. No commentary.\n\n";
        prompt << R"(Schema: {"primary_goal": "", "constraints": [], "preferences": [], "decisions": [{"what": "", "why": "", "turn": 0}], "failed_approaches": [{"approach": "", "reason_abandoned": "", "turn": 0}], "open_questions": [], "entities": {}, "current_phase": "", "user_flagged_notes": []})";

        opts.messages.back().content = prompt.str();
        opts.max_tokens = layer3_config.max_tokens;
        opts.temperature = 0.2;

        auto result = model_caller(opts);
        std::lock_guard<std::mutex> lock(result_mutex);
        if (result && result->success) {
            std::string content = result->message.content;
            size_t start = content.find('{');
            size_t end = content.rfind('}');
            if (start != std::string::npos && end != std::string::npos && end > start) {
                parallel_result.layer3_state_json = content.substr(start, end - start + 1);
            }
            parallel_result.layer3_success = true;
        } else {
            parallel_result.layer3_state_json = state.layer3_previous_state_json;
            parallel_result.layer3_success = false;
        }
    });

    l2_thread.join();
    l3_thread.join();

    // Update state with new L2/L3 results
    if (parallel_result.layer2_success) {
        state.layer2_previous_summary = parallel_result.layer2_summary;
    }
    if (parallel_result.layer3_success) {
        state.layer3_previous_state_json = parallel_result.layer3_state_json;
    }

    // Update state
    state.layer1_pinned_messages = pinned;
    state.last_compression_message_index = messages.size();
    return BuildHierarchicalContextBlock(messages, config, state);
}

// ============================================================================
// Public API
// ============================================================================

ContextCompressionService::ContextCompressionService(AppStorage* storage) : storage_(storage) {}

ContextCompressionService::~ContextCompressionService() {}

std::vector<ContextCompressionConfig> ContextCompressionService::LoadConfigs(const std::string& project_id) const {
    return storage_->LoadProjectSettings(project_id).compression_configs;
}

void ContextCompressionService::SaveConfigs(const std::string& project_id, const std::vector<ContextCompressionConfig>& configs) const {
    auto settings = storage_->LoadProjectSettings(project_id);
    settings.compression_configs = configs;
    storage_->SaveProjectSettings(project_id, settings);
}

std::optional<ContextCompressionConfig> ContextCompressionService::GetConfig(const std::string& project_id, const std::string& config_id) const {
    auto configs = LoadConfigs(project_id);
    for (const auto& c : configs) {
        if (c.id == config_id) {
            return c;
        }
    }
    return std::nullopt;
}

// Global config management (shared across all projects)
std::vector<ContextCompressionConfig> ContextCompressionService::LoadGlobalConfigs() const {
    return storage_->LoadCompressionConfigs();
}

void ContextCompressionService::SaveGlobalConfigs(const std::vector<ContextCompressionConfig>& configs) const {
    storage_->SaveCompressionConfigs(configs);
}

std::optional<ContextCompressionConfig> ContextCompressionService::GetGlobalConfig(const std::string& config_id) const {
    auto configs = LoadGlobalConfigs();
    for (const auto& c : configs) {
        if (c.id == config_id) {
            return c;
        }
    }
    return std::nullopt;
}

ChatCompressionState ContextCompressionService::LoadChatState(const std::string& project_id, const std::string& chat_id) const {
    return storage_->LoadChatCompressionState(project_id, chat_id);
}

void ContextCompressionService::SaveChatState(const std::string& project_id, const std::string& chat_id, const ChatCompressionState& state) const {
    storage_->SaveChatCompressionState(project_id, chat_id, state);
}

bool ContextCompressionService::ShouldCompress(const std::string& project_id, const std::string& chat_id, size_t total_messages) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check force flag first
    std::string key = MakeTurnKey(project_id, chat_id);
    if (force_compression_chats_.count(key) > 0) {
        force_compression_chats_.erase(key);
        return true;
    }

    auto project_settings = storage_->LoadProjectSettings(project_id);
    if (project_settings.selected_compression_config_id.empty()) {
        return false;
    }

    auto config_opt = GetGlobalConfig(project_settings.selected_compression_config_id);
    if (!config_opt) {
        return false;
    }
    if (config_opt->frequency_every_n_prompts <= 0) {
        return false;
    }

    auto state = LoadChatState(project_id, chat_id);
    if (state.last_compression_message_index >= total_messages) {
        return false;
    }
    size_t new_messages = total_messages - state.last_compression_message_index;

    return new_messages >= static_cast<size_t>(config_opt->frequency_every_n_prompts);
}

void ContextCompressionService::MarkCompressionScheduled(const std::string& project_id, const std::string& chat_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    force_compression_chats_.insert(MakeTurnKey(project_id, chat_id));
}

std::string ContextCompressionService::CompressConversation(
    const std::vector<MessageRecord>& messages,
    const std::string& project_id,
    const std::string& chat_id,
    const std::string& config_id,
    std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller,
    bool force_rebuild,
    const std::string& trigger_reason) {

    auto config_opt = GetGlobalConfig(config_id);
    if (!config_opt) {
        return "";
    }

    ChatCompressionState state = LoadChatState(project_id, chat_id);
    const ChatCompressionState previous_state = state;
    const size_t previous_message_index = std::min(state.last_compression_message_index, messages.size());
    std::string block;

    if (config_opt->strategy == ContextCompressionStrategy::TruncateTop) {
        block = CompressTruncateTop(messages, *config_opt, state);
        if (block.empty() && force_rebuild) {
            block = BuildTruncateTopBlock(messages, *config_opt, state);
        }
    } else if (config_opt->strategy == ContextCompressionStrategy::HierarchicalStructured) {
        block = CompressHierarchical(messages, *config_opt, state, model_caller);
        if (block.empty() && force_rebuild) {
            state.layer1_pinned_messages = Layer1_Pin(messages, config_opt->layers.layer1);
            state.last_compression_message_index = messages.size();
            block = BuildHierarchicalContextBlock(messages, *config_opt, state);
        }
    } else {
        return "";
    }

    if (!block.empty()) {
        state.current_compressed_context = block;
        ChatCompressionSnapshot snapshot;
        snapshot.id = MakeId("compression");
        snapshot.created_at = CurrentTimestampUtc();
        snapshot.trigger_reason = trigger_reason.empty() ? "automatic" : trigger_reason;
        snapshot.config_id = config_opt->id;
        snapshot.config_name = config_opt->name;
        snapshot.strategy = CompressionStrategyToString(config_opt->strategy);
        snapshot.previous_snapshot_id = previous_state.latest_snapshot_id;
        snapshot.previous_message_index = previous_message_index;
        snapshot.compressed_through_message_index = std::min(state.last_compression_message_index, messages.size());
        snapshot.previous_compressed_context = previous_state.current_compressed_context;
        snapshot.compressed_context = block;
        snapshot.layer2_summary = state.layer2_previous_summary;
        snapshot.layer3_state_json = state.layer3_previous_state_json;
        snapshot.pinned_messages = state.layer1_pinned_messages;
        for (size_t i = previous_message_index; i < snapshot.compressed_through_message_index; ++i) {
            snapshot.source_messages.push_back(messages[i]);
        }
        state.latest_snapshot_id = snapshot.id;
        storage_->AppendChatCompressionSnapshot(project_id, chat_id, snapshot);
    }
    SaveChatState(project_id, chat_id, state);
    return block;
}

std::string ContextCompressionService::BuildCompressedContextBlock(
    const ContextCompressionConfig& config,
    const ChatCompressionState& state) const {
    (void)config;

    if (!state.current_compressed_context.empty()) {
        return state.current_compressed_context;
    }

    std::ostringstream block;
    block << "=== COMPRESSED CONTEXT ===\n\n";

    block << "[Pinned Messages (Layer 1)]\n";
    if (!state.layer1_pinned_messages.empty()) {
        for (const auto& p : state.layer1_pinned_messages) {
            block << "[" << p.role << "]: " << p.content << "\n\n";
        }
    } else {
        block << "(No pinned messages)\n";
    }

    block << "\n[Running Summary - Layer 2]\n";
    block << (state.layer2_previous_summary.empty() ? "(No summary yet)" : state.layer2_previous_summary) << "\n\n";

    block << "[Extracted State - Layer 3]\n";
    if (!state.layer3_previous_state_json.empty()) {
        block << state.layer3_previous_state_json << "\n";
    } else {
        block << "{}\n";
    }

    block << "\n=== END COMPRESSED CONTEXT ===\n";
    return block.str();
}
