#pragma once

#include "types.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace message_sanitizer {

inline std::string TrimAscii(const std::string& value) {
    size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

inline std::string StripTaggedBlocks(
    std::string value,
    const std::string& open_tag,
    const std::string& close_tag) {
    for (;;) {
        const size_t open = value.find(open_tag);
        if (open == std::string::npos) break;

        const size_t close = value.find(close_tag, open + open_tag.size());
        if (close == std::string::npos) {
            value.erase(open);
            break;
        }
        value.erase(open, close + close_tag.size() - open);
    }
    return value;
}

inline std::string LowerAsciiCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

inline std::string StripTaggedBlocksCaseInsensitive(
    std::string value,
    const std::string& open_tag,
    const std::string& close_tag) {
    const std::string open_lower = LowerAsciiCopy(open_tag);
    const std::string close_lower = LowerAsciiCopy(close_tag);

    for (;;) {
        const std::string lowered = LowerAsciiCopy(value);
        const size_t open = lowered.find(open_lower);
        if (open == std::string::npos) break;

        const size_t close = lowered.find(close_lower, open + open_lower.size());
        if (close == std::string::npos) {
            value.erase(open);
            break;
        }
        value.erase(open, close + close_tag.size() - open);
    }
    return value;
}

inline std::string StripRawProviderToolCallBlocks(const std::string& content) {
    std::string sanitized = StripTaggedBlocks(
        content,
        "<minimax:tool_call>",
        "</minimax:tool_call>");
    return TrimAscii(sanitized);
}

inline std::string StripProviderInternalReasoningBlocks(const std::string& content) {
    std::string sanitized = content;
    sanitized = StripTaggedBlocksCaseInsensitive(sanitized, "<think>", "</think>");
    sanitized = StripTaggedBlocksCaseInsensitive(sanitized, "<thinking>", "</thinking>");
    sanitized = StripTaggedBlocksCaseInsensitive(sanitized, "<thought>", "</thought>");
    return TrimAscii(sanitized);
}

inline std::string SanitizeAssistantContentForModel(const std::string& content) {
    return StripProviderInternalReasoningBlocks(
        StripRawProviderToolCallBlocks(content));
}

inline std::string TruncateMiddleForModel(const std::string& content,
                                          size_t max_chars,
                                          const std::string& reason) {
    if (max_chars == 0 || content.size() <= max_chars) {
        return content;
    }

    const std::string note =
        "\n\n[Model-visible history truncated this older tool result: original " +
        std::to_string(content.size()) + " chars. " + reason +
        " Re-run or re-read the tool if exact output is needed.]\n\n";

    if (max_chars <= note.size() + 32) {
        return content.substr(0, max_chars) + note;
    }

    const size_t available = max_chars - note.size();
    const size_t head = (available * 2) / 3;
    const size_t tail = available - head;
    return content.substr(0, head) + note + content.substr(content.size() - tail);
}

inline std::string SanitizeToolContentForModel(const std::string& content,
                                               bool recent_result) {
    // Full chat logs keep exact tool output. The provider-visible replay gets
    // a bounded version for older tool results so long chats do not become a
    // transcript of every historical file read, install log, and command run.
    constexpr size_t kRecentToolResultMaxChars = 64000;
    constexpr size_t kOlderToolResultMaxChars = 6000;
    return TruncateMiddleForModel(
        content,
        recent_result ? kRecentToolResultMaxChars : kOlderToolResultMaxChars,
        recent_result
            ? "The output was unusually large."
            : "Older tool output is summarized to keep the active context responsive.");
}

inline std::vector<std::string> ToolCallIdsFromJson(
    const std::string& tool_calls_json) {
    std::vector<std::string> ids;
    if (TrimAscii(tool_calls_json).empty()) return ids;

    try {
        const auto parsed = nlohmann::json::parse(tool_calls_json);
        if (!parsed.is_array()) return ids;
        for (const auto& item : parsed) {
            if (!item.is_object()) continue;
            const std::string id = item.value("id", "");
            if (!id.empty()) ids.push_back(id);
        }
    } catch (...) {
    }
    return ids;
}

inline bool HasImmediateToolResultsForAll(
    const std::vector<MessageRecord>& messages,
    size_t assistant_index,
    const std::vector<std::string>& ids) {
    std::unordered_set<std::string> expected(ids.begin(), ids.end());
    if (expected.empty()) return false;

    for (size_t i = assistant_index + 1;
         i < messages.size() && messages[i].role == "tool";
         ++i) {
        expected.erase(messages[i].tool_call_id);
    }
    return expected.empty();
}

inline std::vector<MessageRecord> SanitizeModelVisibleMessages(
    const std::vector<MessageRecord>& messages) {
    constexpr size_t kRecentToolResultWindowMessages = 24;

    std::vector<MessageRecord> visible;
    visible.reserve(messages.size());
    for (auto message : messages) {
        // The stored chat log is the full record, including UI/debug/error
        // records. Provider APIs only accept the chat protocol roles below.
        if (message.role != "system" &&
            message.role != "user" &&
            message.role != "assistant" &&
            message.role != "tool") {
            continue;
        }
        if (message.role == "assistant") {
            message.content = SanitizeAssistantContentForModel(message.content);
        }
        visible.push_back(std::move(message));
    }

    std::vector<MessageRecord> sanitized;
    sanitized.reserve(visible.size());
    std::unordered_set<std::string> expected_tool_result_ids;

    for (size_t index = 0; index < visible.size(); ++index) {
        auto message = visible[index];

        if (message.role == "tool") {
            if (!message.tool_call_id.empty() &&
                expected_tool_result_ids.erase(message.tool_call_id) > 0) {
                const bool recent_result =
                    visible.size() - index <= kRecentToolResultWindowMessages;
                message.content =
                    SanitizeToolContentForModel(message.content, recent_result);
                sanitized.push_back(std::move(message));
            }
            continue;
        }

        if (!expected_tool_result_ids.empty()) {
            expected_tool_result_ids.clear();
        }

        if (message.role == "assistant" && !message.tool_calls_json.empty()) {
            const auto ids = ToolCallIdsFromJson(message.tool_calls_json);
            if (HasImmediateToolResultsForAll(visible, index, ids)) {
                expected_tool_result_ids.insert(ids.begin(), ids.end());
            } else {
                message.tool_calls_json.clear();
            }
        }

        if (message.role == "assistant" &&
            message.content.empty() &&
            message.tool_calls_json.empty()) {
            continue;
        }

        sanitized.push_back(std::move(message));
    }

    return sanitized;
}

} // namespace message_sanitizer
