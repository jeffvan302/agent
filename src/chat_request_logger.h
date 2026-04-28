#pragma once

#include "types.h"

#include <filesystem>
#include <string>
#include <vector>

// ── ChatRequestLogger ─────────────────────────────────────────────────────────
// Per-project rotating file logger gated by ProjectSettings.enable_chat_logging.
// Outputs plain text blocks (not JSON) so you can tail the log with any pager.
//
// Life-cycle:
//   ChatRequestLogger::MaybeInitialize(data_root, project_id, enabled);
//   ChatRequestLogger::Log(project_id, enabled, block_string);
//
// When enabled == false the log file is removed so it doesn't accumulate.
//
// Typical block:
//   ======== 2026-04-28T14:32:01Z [project_xxx] chat_yyy ========
//   === Request ===
//   user: hello
//   === System Prompt ===
//   ...
//   ================================================================
//
class ChatRequestLogger {
public:
    // Write one block to the project's log file (no-op if enabled==false).
    static void Log(const std::string& project_id, bool enabled, const std::string& block);

    // (Re-)initialise / tear-down the log file on disk for a project.
    static void MaybeInitialize(const std::filesystem::path& data_root,
                                 const std::string& project_id,
                                 bool enabled);

    // Helpers that build readable log blocks
    static std::string FormatHeader(
        const std::string& timestamp,
        const std::string& project_id,
        const std::string& chat_id,
        const std::string& username = {});

    static std::string FormatFooter();

    static std::string FormatBlock(const std::string& label,
                                   const std::string& body);

    static std::string FormatMessages(const std::vector<MessageRecord>& msgs);

    static std::string FormatProvider(const ProviderConfig& p, const ModelConfig& m);

    static std::string FormatErrorResponse(const std::string& error);

    static std::string FormatSuccessResponse(const std::string& assistant_text);
};
