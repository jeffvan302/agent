#include "chat_request_logger.h"

#include <string>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <filesystem>

namespace {
    std::mutex g_chatlog_mutex;
    std::unordered_map<std::string, std::filesystem::path> g_chatlog_paths;
} // namespace

// ── Public API ──────────────────────────────────────────────────────────────

void ChatRequestLogger::MaybeInitialize(
    const std::filesystem::path& data_root,
    const std::string& project_id,
    bool enabled) {
    std::lock_guard<std::mutex> lock(g_chatlog_mutex);
    const auto path = data_root / (project_id + ".chatlog");
    if (enabled) {
        g_chatlog_paths[project_id] = path;
        std::error_code ec;
        std::filesystem::create_directories(data_root, ec);
    } else {
        g_chatlog_paths.erase(project_id);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

void ChatRequestLogger::Log(const std::string& project_id, bool enabled, const std::string& block) {
    if (!enabled || block.empty()) return;
    std::lock_guard<std::mutex> lock(g_chatlog_mutex);
    auto it = g_chatlog_paths.find(project_id);
    if (it == g_chatlog_paths.end()) return;
    std::ofstream ofs(it->second, std::ios::app);
    if (!ofs.is_open()) return;
    ofs << block;
    if (block.empty() || block.back() != '\n') ofs << "\n";
    ofs << "\n";
}

// ── Format helpers ──────────────────────────────────────────────────────────

std::string ChatRequestLogger::FormatHeader(
    const std::string& timestamp,
    const std::string& project_id,
    const std::string& chat_id,
    const std::string& username) {
    std::string s = "\n======== " + timestamp + " [" + project_id + "] chat=" + chat_id;
    if (!username.empty()) s += " user=" + username;
    s += " ========\n";
    return s;
}

std::string ChatRequestLogger::FormatFooter() {
    return "======== END ========\n";
}

std::string ChatRequestLogger::FormatBlock(const std::string& label, const std::string& body) {
    std::string s = "=== " + label + " ===\n";
    if (body.empty()) {
        s += "(empty)\n";
    } else {
        s += body;
        if (body.empty() || body.back() != '\n') s += "\n";
    }
    return s;
}

std::string ChatRequestLogger::FormatSections(
    const std::string& label_prefix,
    const std::vector<std::pair<std::string, std::string>>& sections) {
    std::string s;
    for (const auto& section : sections) {
        if (section.second.empty()) continue;
        s += FormatBlock(label_prefix + ": " + section.first, section.second);
    }
    return s;
}

std::string ChatRequestLogger::FormatMessages(const std::vector<MessageRecord>& msgs) {
    std::string s;
    s.reserve(msgs.size() * 256);
    for (size_t i = 0; i < msgs.size(); ++i) {
        s += "[" + std::to_string(i) + "] role=" + msgs[i].role;
        if (!msgs[i].name.empty()) s += " name=" + msgs[i].name;
        s += "\n";
        s += msgs[i].content;
        if (!msgs[i].content.empty() && msgs[i].content.back() != '\n') s += "\n";
        s += "\n";
    }
    return s;
}

std::string ChatRequestLogger::FormatProvider(const ProviderConfig& p, const ModelConfig& m) {
    return "Provider: " + p.name + " (" + p.provider_type + ")\nModel: " + m.display_name + " (id=" + m.id + ")\n";
}

std::string ChatRequestLogger::FormatErrorResponse(const std::string& error) {
    return "=== Model Error ===\n" + error + "\n";
}

std::string ChatRequestLogger::FormatSuccessResponse(const std::string& assistant_text) {
    return "=== Model Response ===\n" + assistant_text + "\n";
}

std::string ChatRequestLogger::FormatCompressionBlock(
    const std::string& trigger_reason,
    size_t before_messages,
    size_t after_messages,
    size_t compressed_through,
    const std::string& compressed_text,
    const std::string& config_name) {
    std::string s = "=== Context Window Compression ===\n";
    s += "Trigger: " + trigger_reason + "\n";
    s += "Config:  " + config_name + "\n";
    s += "Messages before:   " + std::to_string(before_messages) + "\n";
    s += "Messages after:    " + std::to_string(after_messages) + "\n";
    s += "Compressed through index: " + std::to_string(compressed_through) + "\n\n";

    std::string truncated = compressed_text;
    constexpr size_t kPreviewLimit = 2048;
    if (truncated.size() > kPreviewLimit) {
        truncated.resize(kPreviewLimit);
        truncated += "\n... (truncated, see chat compression history for full block) ...\n";
    }
    if (truncated.empty()) {
        s += "No compressed context was generated (nothing to compress or config produced empty result).\n";
    } else {
        s += "--- Compressed Context (preview) ---\n" + truncated + "\n--- End Compressed Context ---\n";
    }
    return s;
}
