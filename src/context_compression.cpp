#include "context_compression.h"

#include "openai_client.h"
#include "util.h"
#include "variable_resolver.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_map>
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
    case ContextCompressionStrategy::RollingSummary:
        return "rolling_summary";
    case ContextCompressionStrategy::ToolTraceDistillation:
        return "tool_trace_distillation";
    case ContextCompressionStrategy::HierarchicalStructured:
        return "hierarchical_structured";
    case ContextCompressionStrategy::None:
    default:
        return "none";
    }
}

std::string Layer3SchemaJson() {
    return R"({
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
}

std::string ReplaceAllCopy(std::string text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return text;
    }

    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string TruncateMiddleForCompressedContext(const std::string& content,
                                               size_t max_chars,
                                               const std::string& reason) {
    if (max_chars == 0 || content.size() <= max_chars) {
        return content;
    }

    const std::string note =
        "\n\n[Compressed context truncated this message: original " +
        std::to_string(content.size()) + " chars. " + reason + "]\n\n";

    if (max_chars <= note.size() + 32) {
        return content.substr(0, max_chars) + note;
    }

    const size_t available = max_chars - note.size();
    const size_t head = (available * 2) / 3;
    const size_t tail = available - head;
    return content.substr(0, head) + note + content.substr(content.size() - tail);
}

std::string CompressionMessageLabel(const MessageRecord& message) {
    std::string label = message.role;
    if (!message.name.empty()) {
        label += "/";
        label += message.name;
    }
    return label;
}

void AppendMessageToCompressedContext(std::ostringstream& block,
                                      const MessageRecord& message,
                                      size_t max_chars,
                                      const std::string& reason) {
    block << "[" << CompressionMessageLabel(message) << "]: "
          << TruncateMiddleForCompressedContext(message.content, max_chars, reason)
          << "\n\n";
}

bool IsToolTraceMessage(const MessageRecord& message) {
    return message.role == "tool" ||
           !TrimStr(message.tool_call_id).empty() ||
           !TrimStr(message.tool_calls_json).empty();
}

bool HasToolTraceMessages(const std::vector<MessageRecord>& messages) {
    return std::any_of(messages.begin(), messages.end(), IsToolTraceMessage);
}

std::string TruncateForPrompt(const std::string& text, size_t max_chars) {
    if (max_chars == 0 || text.size() <= max_chars) {
        return text;
    }
    const std::string note = "\n[...truncated for compression prompt...]\n";
    if (max_chars <= note.size() + 16) {
        return text.substr(0, max_chars);
    }
    const size_t available = max_chars - note.size();
    const size_t head = (available * 2) / 3;
    const size_t tail = available - head;
    return text.substr(0, head) + note + text.substr(text.size() - tail);
}

std::string BuildDeterministicRollingSummary(const std::string& prior_summary,
                                             const std::vector<MessageRecord>& new_turns,
                                             int max_tokens) {
    std::ostringstream summary;
    if (!TrimStr(prior_summary).empty()) {
        summary << prior_summary << "\n\n";
    }
    summary << "Recent updates:\n";
    for (const auto& turn : new_turns) {
        if (turn.role == "tool") {
            summary << "- Tool output was received";
            if (!turn.name.empty()) summary << " from " << turn.name;
            if (!turn.tool_call_id.empty()) summary << " (" << turn.tool_call_id << ")";
            summary << ".\n";
            continue;
        }
        summary << "- " << CompressionMessageLabel(turn) << ": "
                << TruncateForPrompt(turn.content, 900) << "\n";
    }

    const size_t max_chars = static_cast<size_t>(std::max(200, max_tokens <= 0 ? 500 : max_tokens) * 4);
    std::string result = summary.str();
    if (result.size() > max_chars) {
        result = TruncateForPrompt(result, max_chars);
    }
    return TrimStr(result);
}

std::string BuildToolTraceSourceText(const std::vector<MessageRecord>& messages) {
    constexpr size_t kMaxSourceChars = 32000;
    std::ostringstream source;
    size_t count = 0;
    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& message = messages[i];
        if (!IsToolTraceMessage(message)) {
            continue;
        }
        ++count;
        source << "Trace " << count << " (message " << i << ", " << CompressionMessageLabel(message) << ")\n";
        if (!message.tool_call_id.empty()) {
            source << "tool_call_id: " << message.tool_call_id << "\n";
        }
        if (!TrimStr(message.tool_calls_json).empty()) {
            source << "tool_calls_json:\n" << TruncateForPrompt(message.tool_calls_json, 3000) << "\n";
        }
        if (!TrimStr(message.content).empty()) {
            source << "content:\n" << TruncateForPrompt(message.content, 5000) << "\n";
        }
        source << "\n";
        if (static_cast<size_t>(source.tellp()) >= kMaxSourceChars) {
            source << "[Additional tool trace material omitted before model distillation.]\n";
            break;
        }
    }
    return count == 0 ? "" : source.str();
}

std::string BuildDeterministicToolTraceSummary(const std::string& prior_summary,
                                               const std::vector<MessageRecord>& messages,
                                               int max_tokens) {
    std::ostringstream summary;
    if (!TrimStr(prior_summary).empty()) {
        summary << "Prior distilled tool trace:\n" << prior_summary << "\n\n";
    }
    summary << "Distilled tool trace:\n";
    size_t count = 0;
    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& message = messages[i];
        if (!IsToolTraceMessage(message)) {
            continue;
        }
        ++count;
        summary << "- Message " << i << " [" << CompressionMessageLabel(message) << "]";
        if (!message.name.empty()) summary << " name=" << message.name;
        if (!message.tool_call_id.empty()) summary << " call_id=" << message.tool_call_id;
        summary << "\n";
        if (!TrimStr(message.tool_calls_json).empty()) {
            summary << "  tool calls: " << TruncateForPrompt(message.tool_calls_json, 1200) << "\n";
        }
        if (!TrimStr(message.content).empty()) {
            summary << "  result: " << TruncateForPrompt(message.content, 1800) << "\n";
        }
    }
    if (count == 0) {
        summary << "- No tool calls or tool outputs were found in this range.\n";
    }

    const size_t max_chars = static_cast<size_t>(std::max(200, max_tokens <= 0 ? 500 : max_tokens) * 4);
    std::string result = summary.str();
    if (result.size() > max_chars) {
        result = TruncateForPrompt(result, max_chars);
    }
    return TrimStr(result);
}

std::string FormatToolTraceDistillationContext(const std::vector<MessageRecord>& messages,
                                               const ContextCompressionConfig& config,
                                               const ChatCompressionState& state) {
    int keep = config.truncate_top_keep_messages;
    if (keep <= 0) keep = 20;

    std::vector<MessageRecord> recent_non_tool;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (IsToolTraceMessage(*it)) {
            continue;
        }
        recent_non_tool.insert(recent_non_tool.begin(), *it);
        if (recent_non_tool.size() >= static_cast<size_t>(keep)) {
            break;
        }
    }

    std::ostringstream block;
    block << "=== TOOL TRACE DISTILLATION ===\n\n";
    block << "## Distilled Tool Trace\n";
    block << (state.layer2_previous_summary.empty()
        ? "No meaningful tool traces were found."
        : state.layer2_previous_summary)
          << "\n\n";

    block << "## Recent Non-Tool Conversation - Verbatim Tail\n";
    if (recent_non_tool.empty()) {
        block << "(No recent non-tool messages)\n\n";
    } else {
        for (const auto& message : recent_non_tool) {
            AppendMessageToCompressedContext(
                block,
                message,
                10000,
                "Tool trace distillation keeps a bounded non-tool tail for continuity.");
        }
    }

    block << "=== END TOOL TRACE DISTILLATION ===\n";
    return block.str();
}

std::string ResolveLayer2PromptTemplate(const Layer2Config& config) {
    return TrimStr(config.prompt_template).empty()
        ? ContextCompressionService::DefaultLayer2PromptTemplate()
        : config.prompt_template;
}

std::string ResolveRollingSummaryPromptTemplate(const Layer2Config& config) {
    return TrimStr(config.prompt_template).empty()
        ? ContextCompressionService::DefaultRollingSummaryPromptTemplate()
        : config.prompt_template;
}

std::string ResolveToolTraceDistillationPromptTemplate(const Layer2Config& config) {
    return TrimStr(config.prompt_template).empty()
        ? ContextCompressionService::DefaultToolTraceDistillationPromptTemplate()
        : config.prompt_template;
}

std::string ResolveLayer3PromptTemplate(const Layer3Config& config) {
    return TrimStr(config.prompt_template).empty()
        ? ContextCompressionService::DefaultLayer3PromptTemplate()
        : config.prompt_template;
}

std::string ApplyPromptTemplate(std::string prompt_template, int max_tokens,
                                const std::optional<std::string>& schema = std::nullopt) {
    prompt_template = TrimStr(prompt_template);
    if (prompt_template.empty()) {
        return prompt_template;
    }

    prompt_template = ReplaceAllCopy(std::move(prompt_template), "{{max_tokens}}", std::to_string(max_tokens));

    if (schema.has_value()) {
        if (prompt_template.find("{{schema}}") != std::string::npos) {
            prompt_template = ReplaceAllCopy(std::move(prompt_template), "{{schema}}", *schema);
        } else {
            prompt_template += "\n\nSchema:\n";
            prompt_template += *schema;
        }
    }

    return prompt_template;
}

std::string ResolveLayer0CapturePromptTemplate(const Layer0Config& config) {
    return TrimStr(config.capture_prompt_template).empty()
        ? ContextCompressionService::DefaultLayer0CapturePromptTemplate()
        : config.capture_prompt_template;
}

std::string ResolveLayer0SelectionPromptTemplate(const Layer0Config& config) {
    return TrimStr(config.selection_prompt_template).empty()
        ? ContextCompressionService::DefaultLayer0SelectionPromptTemplate()
        : config.selection_prompt_template;
}

std::string NormalizeArtifactKey(std::string value) {
    value = TrimStr(std::move(value));
    std::string normalized;
    normalized.reserve(value.size());
    bool last_was_underscore = false;
    for (const unsigned char ch : value) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
            last_was_underscore = false;
        } else if (!last_was_underscore) {
            normalized.push_back('_');
            last_was_underscore = true;
        }
    }
    while (!normalized.empty() && normalized.front() == '_') normalized.erase(normalized.begin());
    while (!normalized.empty() && normalized.back() == '_') normalized.pop_back();
    return normalized;
}

std::string SlugFromText(const std::string& value, const std::string& fallback) {
    std::string slug = NormalizeArtifactKey(value);
    if (!slug.empty()) return slug;
    return NormalizeArtifactKey(fallback);
}

std::string NormalizeMultilineText(const std::string& value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        char ch = value[i];
        if (ch == '\r') {
            if (i + 1 < value.size() && value[i + 1] == '\n') continue;
            normalized.push_back('\n');
        } else {
            normalized.push_back(ch);
        }
    }
    return normalized;
}

std::string Fnv1aHashHex(const std::string& value) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

std::string MakeArtifactId() {
    return GenerateId("artifact");
}

std::string JsonStringOrEmpty(const json& value, const char* key) {
    if (!value.is_object() || !value.contains(key)) return {};
    if (!value[key].is_string()) return {};
    return TrimStr(value[key].get<std::string>());
}

std::vector<std::string> JsonStringArray(const json& value, const char* key) {
    std::vector<std::string> out;
    if (!value.is_object() || !value.contains(key) || !value[key].is_array()) return out;
    for (const auto& item : value[key]) {
        if (item.is_string()) {
            const std::string text = TrimStr(item.get<std::string>());
            if (!text.empty()) out.push_back(text);
        }
    }
    return out;
}

std::string NormalizeArtifactType(std::string type, std::string language);
std::string NormalizeArtifactLanguage(std::string language, const std::string& type);

std::string LowerAsciiCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string FileNameFromText(const std::string& text) {
    try {
        const std::regex file_re(
            R"(([A-Za-z0-9_$@~.-]+\.(?:cpp|cc|cxx|c\+\+|c|h|hh|hpp|hxx|py|pyw|js|jsx|mjs|cjs|ts|tsx|java|html|htm|svg|json|md|markdown|css)))",
            std::regex_constants::icase);
        std::smatch match;
        if (std::regex_search(text, match, file_re) && match.size() > 1) {
            return match[1].str();
        }
    } catch (...) {
    }
    return {};
}

std::string ExtensionFromPathText(const std::string& text) {
    std::string file_name = FileNameFromText(text);
    if (file_name.empty()) {
        file_name = text;
        std::replace(file_name.begin(), file_name.end(), '\\', '/');
        const size_t slash = file_name.find_last_of('/');
        if (slash != std::string::npos) {
            file_name = file_name.substr(slash + 1);
        }
    }
    const size_t dot = file_name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= file_name.size()) {
        return {};
    }
    return LowerAsciiCopy(file_name.substr(dot + 1));
}

std::string LanguageFromPathText(const std::string& text) {
    const std::string ext = ExtensionFromPathText(text);
    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "c++" ||
        ext == "hpp" || ext == "hxx" || ext == "hh") return "cpp";
    if (ext == "c" || ext == "h") return "c";
    if (ext == "py" || ext == "pyw") return "python";
    if (ext == "js" || ext == "jsx" || ext == "mjs" || ext == "cjs") return "javascript";
    if (ext == "ts" || ext == "tsx") return "typescript";
    if (ext == "java") return "java";
    if (ext == "htm" || ext == "html") return "html";
    if (ext == "svg") return "svg";
    if (ext == "json") return "json";
    if (ext == "md" || ext == "markdown") return "markdown";
    if (ext == "css") return "css";
    return {};
}

bool LooksLikeArtifactContent(const std::string& value) {
    const std::string trimmed = TrimStr(value);
    if (trimmed.empty()) return false;
    const std::string lower = LowerAsciiCopy(trimmed.substr(0, std::min<size_t>(trimmed.size(), 512)));
    if (lower.find("<svg") != std::string::npos || lower.find("<html") != std::string::npos ||
        lower.find("<!doctype html") != std::string::npos) {
        return true;
    }
    if (trimmed.find("```") != std::string::npos) return true;
    if (trimmed.size() < 40) return false;
    if (trimmed.find('\n') != std::string::npos) return true;
    return (trimmed.front() == '{' || trimmed.front() == '[') && trimmed.size() > 80;
}

bool KeyImpliesArtifactContent(const std::string& key) {
    const std::string lower = LowerAsciiCopy(NormalizeArtifactKey(key));
    if (lower.find("path") != std::string::npos || lower.find("file_name") != std::string::npos ||
        lower.find("filename") != std::string::npos || lower.find("description") != std::string::npos ||
        lower.find("instructions") != std::string::npos) {
        return false;
    }
    return lower.find("content") != std::string::npos ||
           lower.find("code") != std::string::npos ||
           lower.find("source") != std::string::npos ||
           lower.find("body") != std::string::npos ||
           lower.find("html") != std::string::npos ||
           lower.find("svg") != std::string::npos ||
           lower == "text" ||
           lower == "replacement" ||
           lower == "new_text" ||
           lower == "new_content";
}

std::string FindPathLikeValue(const json& value) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            const std::string key = LowerAsciiCopy(NormalizeArtifactKey(it.key()));
            if ((key.find("path") != std::string::npos || key.find("file") != std::string::npos ||
                 key.find("filename") != std::string::npos) && it.value().is_string()) {
                const std::string text = it.value().get<std::string>();
                if (!FileNameFromText(text).empty() || !LanguageFromPathText(text).empty()) {
                    return text;
                }
            }
        }
        for (auto it = value.begin(); it != value.end(); ++it) {
            const std::string nested = FindPathLikeValue(it.value());
            if (!nested.empty()) return nested;
        }
    } else if (value.is_array()) {
        for (const auto& item : value) {
            const std::string nested = FindPathLikeValue(item);
            if (!nested.empty()) return nested;
        }
    }
    return {};
}

size_t FindCaseInsensitive(const std::string& haystack_lower,
                           const std::string& needle_lower,
                           size_t start = 0) {
    return haystack_lower.find(needle_lower, start);
}

std::string ShortHash(const std::string& value) {
    const std::string hash = Fnv1aHashHex(value);
    return hash.substr(0, std::min<size_t>(hash.size(), 8));
}

std::string StripMarkdownDecorators(std::string line) {
    line = TrimStr(std::move(line));
    while (!line.empty() && (line.front() == '#' || line.front() == '-' ||
                             line.front() == '*' || line.front() == ':' ||
                             std::isspace(static_cast<unsigned char>(line.front())))) {
        line.erase(line.begin());
        line = TrimStr(std::move(line));
    }
    if (!line.empty() && line.back() == ':') {
        line.pop_back();
    }
    return TrimStr(std::move(line));
}

std::string LastMeaningfulLineBefore(const std::string& text, size_t offset) {
    size_t line_end = std::min(offset, text.size());
    int inspected = 0;
    while (line_end > 0 && inspected < 12) {
        const size_t search_from = line_end == 0 ? 0 : line_end - 1;
        const size_t line_start_pos = text.rfind('\n', search_from);
        const size_t line_start = line_start_pos == std::string::npos ? 0 : line_start_pos + 1;
        std::string line = StripMarkdownDecorators(text.substr(line_start, line_end - line_start));
        if (!line.empty() && line.rfind("```", 0) != 0) {
            if (line.size() > 90) line.resize(90);
            return line;
        }
        if (line_start_pos == std::string::npos) break;
        line_end = line_start_pos;
        ++inspected;
    }
    return {};
}

std::string RegexFirstCapture(const std::string& text,
                              const std::string& pattern,
                              std::regex_constants::syntax_option_type flags = std::regex_constants::ECMAScript) {
    try {
        const std::regex re(pattern, flags);
        std::smatch match;
        if (std::regex_search(text, match, re) && match.size() > 1) {
            return TrimStr(match[1].str());
        }
    } catch (...) {
    }
    return {};
}

std::string InferArtifactSymbol(const std::string& content, const std::string& type) {
    if (type == "html") {
        const std::string title = RegexFirstCapture(
            content, R"(<title[^>]*>([^<]+)</title>)", std::regex_constants::icase);
        if (!title.empty()) return title;
    }
    const std::string named_decl = RegexFirstCapture(
        content,
        R"(\b(?:class|struct|interface|enum|function|def)\s+([A-Za-z_][A-Za-z0-9_\-]*))");
    if (!named_decl.empty()) return named_decl;
    const std::string variable_decl = RegexFirstCapture(
        content,
        R"(\b(?:const|let|var)\s+([A-Za-z_][A-Za-z0-9_\-]*)\s*=)");
    if (!variable_decl.empty()) return variable_decl;
    const std::string c_style_function = RegexFirstCapture(
        content,
        R"(\b(?:[A-Za-z_][A-Za-z0-9_:<>~\*&]*\s+)+([A-Za-z_][A-Za-z0-9_:~]*)\s*\([^;{}]*\)\s*(?:const\s*)?\{)");
    if (!c_style_function.empty()) return c_style_function;
    const std::string id_attr = RegexFirstCapture(
        content, R"(\bid\s*=\s*["']([^"']+)["'])", std::regex_constants::icase);
    if (!id_attr.empty()) return id_attr;
    return {};
}

json MakeFallbackArtifactCandidate(const std::string& content,
                                   const std::string& raw_language,
                                   const std::string& context_hint,
                                   const std::string& role) {
    std::string type = NormalizeArtifactType(raw_language, raw_language);
    if (type == "html" || content.find("<html") != std::string::npos ||
        content.find("<!DOCTYPE html") != std::string::npos) {
        type = "html";
    } else if (type == "svg" || content.find("<svg") != std::string::npos) {
        type = "svg";
    }
    std::string language_seed = raw_language;
    const std::string file_name = FileNameFromText(context_hint);
    if (!file_name.empty()) {
        const std::string path_language = LanguageFromPathText(file_name);
        if (!path_language.empty() && NormalizeArtifactKey(language_seed) == "code") {
            language_seed = path_language;
            type = NormalizeArtifactType(path_language, path_language);
        }
    }
    if (NormalizeArtifactKey(language_seed) == "code" && type != "code") {
        language_seed.clear();
    }
    const std::string language = NormalizeArtifactLanguage(language_seed, type);
    const std::string symbol = InferArtifactSymbol(content, type);

    std::string key_seed;
    if (!context_hint.empty()) key_seed += context_hint + " ";
    if (!file_name.empty()) key_seed += file_name + " ";
    if (!symbol.empty()) key_seed += symbol + " ";
    key_seed += type;
    std::string artifact_key = SlugFromText(key_seed, type + "_" + ShortHash(content));
    if (artifact_key == type || artifact_key.empty()) {
        artifact_key = type + "_" + ShortHash(content);
    }

    std::string summary = context_hint.empty()
        ? ("Preserved " + type + " artifact from a " + role + " message.")
        : ("Preserved " + type + " artifact: " + context_hint + ".");
    if (!symbol.empty() && summary.find(symbol) == std::string::npos) {
        summary += " Symbol: " + symbol + ".";
    }
    if (!file_name.empty() && summary.find(file_name) == std::string::npos) {
        summary += " File: " + file_name + ".";
    }

    return json{
        {"artifact_key", artifact_key},
        {"type", type},
        {"language", language},
        {"summary", summary},
        {"user_intent", context_hint},
        {"problem_it_solves", "Preserves exact generated artifact content for later version-aware recall."},
        {"tags", json::array({type, "fallback_capture"})},
        {"content", content},
    };
}

void AppendToolArgumentArtifacts(const json& value,
                                 const std::string& key,
                                 const std::string& tool_name,
                                 const std::string& path_hint,
                                 const std::string& role,
                                 std::vector<json>* artifacts) {
    if (!artifacts) return;
    if (value.is_string()) {
        std::string content = NormalizeMultilineText(value.get<std::string>());
        content = TrimStr(std::move(content));
        if (!KeyImpliesArtifactContent(key) || !LooksLikeArtifactContent(content)) {
            return;
        }

        std::string language = LanguageFromPathText(path_hint);
        if (language.empty()) language = LanguageFromPathText(key);
        if (language.empty()) language = "code";

        std::string context_hint = tool_name;
        const std::string file_name = FileNameFromText(path_hint);
        if (!file_name.empty()) {
            context_hint += " " + file_name;
        } else if (!key.empty()) {
            context_hint += " " + key;
        }
        artifacts->push_back(MakeFallbackArtifactCandidate(content, language, context_hint, role));
        return;
    }

    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            AppendToolArgumentArtifacts(it.value(), it.key(), tool_name, path_hint, role, artifacts);
        }
    } else if (value.is_array()) {
        for (const auto& item : value) {
            AppendToolArgumentArtifacts(item, key, tool_name, path_hint, role, artifacts);
        }
    }
}

void AppendArtifactsFromToolCalls(const MessageRecord& turn, std::vector<json>* artifacts) {
    if (!artifacts || TrimStr(turn.tool_calls_json).empty()) {
        return;
    }

    json calls;
    try {
        calls = json::parse(turn.tool_calls_json);
    } catch (...) {
        return;
    }
    if (!calls.is_array()) {
        return;
    }

    for (const auto& call : calls) {
        if (!call.is_object()) continue;
        const auto function_it = call.find("function");
        if (function_it == call.end() || !function_it->is_object()) continue;
        const std::string tool_name = function_it->value("name", "tool_call");
        const std::string arguments_text = function_it->value("arguments", "");
        if (TrimStr(arguments_text).empty()) continue;

        json args;
        try {
            args = json::parse(arguments_text);
        } catch (...) {
            continue;
        }
        const std::string path_hint = FindPathLikeValue(args);
        AppendToolArgumentArtifacts(args, {}, tool_name, path_hint, turn.role, artifacts);
    }
}

void AppendDelimitedArtifactBlocks(const std::string& text,
                                   const std::string& start_token,
                                   const std::string& end_token,
                                   const std::string& language,
                                   const std::string& role,
                                   std::vector<json>* artifacts) {
    if (!artifacts || text.empty()) return;
    const std::string lower = LowerAsciiCopy(text);
    const std::string start_lower = LowerAsciiCopy(start_token);
    const std::string end_lower = LowerAsciiCopy(end_token);

    size_t pos = 0;
    while (pos < text.size()) {
        const size_t start = FindCaseInsensitive(lower, start_lower, pos);
        if (start == std::string::npos) break;
        const size_t end_start = FindCaseInsensitive(lower, end_lower, start + start_lower.size());
        if (end_start == std::string::npos) break;
        const size_t end = end_start + end_token.size();
        std::string content = TrimStr(NormalizeMultilineText(text.substr(start, end - start)));
        if (!content.empty()) {
            artifacts->push_back(MakeFallbackArtifactCandidate(
                content,
                language,
                LastMeaningfulLineBefore(text, start),
                role));
        }
        pos = end;
    }
}

std::vector<json> ExtractFallbackArtifactCandidates(const std::vector<MessageRecord>& new_turns) {
    std::vector<json> artifacts;
    for (const auto& turn : new_turns) {
        const std::string& text = turn.content;
        size_t pos = 0;
        while (pos < text.size()) {
            const size_t fence = text.find("```", pos);
            if (fence == std::string::npos) break;
            const size_t language_start = fence + 3;
            const size_t language_end = text.find_first_of("\r\n", language_start);
            if (language_end == std::string::npos) break;
            std::string raw_language = TrimStr(text.substr(language_start, language_end - language_start));
            const size_t body_start = text.find('\n', language_end);
            if (body_start == std::string::npos) break;
            const size_t content_start = body_start + 1;
            const size_t closing = text.find("```", content_start);
            if (closing == std::string::npos) break;

            std::string content = NormalizeMultilineText(text.substr(content_start, closing - content_start));
            content = TrimStr(std::move(content));
            if (!content.empty()) {
                artifacts.push_back(MakeFallbackArtifactCandidate(
                    content,
                    raw_language.empty() ? "code" : raw_language,
                    LastMeaningfulLineBefore(text, fence),
                    turn.role));
            }
            pos = closing + 3;
        }

        AppendArtifactsFromToolCalls(turn, &artifacts);

        const std::string trimmed = TrimStr(text);
        const std::string lower_trimmed = LowerAsciiCopy(trimmed);
        if (lower_trimmed.find("<svg") != std::string::npos && lower_trimmed.find("</svg>") != std::string::npos) {
            AppendDelimitedArtifactBlocks(text, "<svg", "</svg>", "svg", turn.role, &artifacts);
        }
        if (lower_trimmed.find("</html>") != std::string::npos) {
            if (lower_trimmed.find("<!doctype html") != std::string::npos) {
                AppendDelimitedArtifactBlocks(text, "<!doctype html", "</html>", "html", turn.role, &artifacts);
            } else if (lower_trimmed.find("<html") != std::string::npos) {
                AppendDelimitedArtifactBlocks(text, "<html", "</html>", "html", turn.role, &artifacts);
            }
        }
    }
    return artifacts;
}

std::optional<json> ParseLooseJson(const std::string& text) {
    const std::string trimmed = TrimStr(text);
    if (trimmed.empty()) return std::nullopt;

    try {
        return json::parse(trimmed);
    } catch (...) {
    }

    const size_t object_start = trimmed.find('{');
    const size_t object_end = trimmed.rfind('}');
    if (object_start != std::string::npos && object_end != std::string::npos && object_end > object_start) {
        try {
            return json::parse(trimmed.substr(object_start, object_end - object_start + 1));
        } catch (...) {
        }
    }

    const size_t array_start = trimmed.find('[');
    const size_t array_end = trimmed.rfind(']');
    if (array_start != std::string::npos && array_end != std::string::npos && array_end > array_start) {
        try {
            return json::parse(trimmed.substr(array_start, array_end - array_start + 1));
        } catch (...) {
        }
    }

    return std::nullopt;
}

std::string NormalizeArtifactAlias(std::string value) {
    const std::string raw = LowerAsciiCopy(TrimStr(value));
    if (raw == "c++" || raw == "cpp" || raw == "cxx" || raw == "cc" ||
        raw == "hpp" || raw == "hxx" || raw == "hh") return "cpp";
    if (raw == "c" || raw == "h") return "c";
    if (raw == "py" || raw == "pyw" || raw == "python3") return "python";
    if (raw == "js" || raw == "jsx" || raw == "mjs" || raw == "cjs" ||
        raw == "node" || raw == "nodejs" || raw == "ecmascript") return "javascript";
    if (raw == "ts" || raw == "tsx") return "typescript";
    if (raw == "java") return "java";

    std::string normalized = NormalizeArtifactKey(raw);
    if (normalized == "c_plus_plus" || normalized == "cplusplus" ||
        normalized == "cxx" || normalized == "cc" || normalized == "hpp" ||
        normalized == "hxx" || normalized == "hh") return "cpp";
    if (normalized == "py" || normalized == "pyw" || normalized == "python3") return "python";
    if (normalized == "js" || normalized == "jsx" || normalized == "mjs" ||
        normalized == "cjs" || normalized == "node" || normalized == "nodejs" ||
        normalized == "ecmascript") return "javascript";
    if (normalized == "ts" || normalized == "tsx") return "typescript";
    if (normalized == "vegalite" || normalized == "vega_lite") return "vega-lite";
    if (normalized == "cytoscapejs" || normalized == "cytoscape_js") return "cytoscape";
    if (normalized == "md") return "markdown";
    if (normalized == "htm") return "html";
    return normalized;
}

std::string NormalizeArtifactType(std::string type, std::string language) {
    type = NormalizeArtifactAlias(std::move(type));
    language = NormalizeArtifactAlias(std::move(language));
    if (type.empty()) type = language;
    if (type == "yml") type = "yaml";
    if (type.empty()) type = "code";
    return type;
}

std::string NormalizeArtifactLanguage(std::string language, const std::string& type) {
    language = NormalizeArtifactAlias(std::move(language));
    if (language == "source") language.clear();
    if (!language.empty()) return language;
    if (type == "vega-lite") return "json";
    if (type == "cytoscape") return "json";
    if (type == "code") return "text";
    return type;
}

std::filesystem::path ResolveLayer0StorageRoot(
    const Layer0Config& config,
    const std::vector<ProjectMcpVariableValue>& resolved_variables) {
    const std::string resolved =
        Trim(variable_resolver::ExpandTemplate(config.storage_folder_template, resolved_variables));
    if (resolved.empty()) return {};
    std::filesystem::path root(Utf8ToWide(resolved));
    if (!root.is_absolute()) return {};
    return root;
}

json EmptyArtifactIndex(const std::string& project_id, const std::string& chat_id) {
    return json{
        {"schema_version", 1},
        {"project_id", project_id},
        {"chat_id", chat_id},
        {"updated_at", CurrentTimestampUtc()},
        {"artifacts", json::array()},
    };
}

bool LoadArtifactIndex(const std::filesystem::path& path, json* index_out) {
    if (!index_out) return false;
    if (!std::filesystem::exists(path)) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    try {
        *index_out = json::parse(buffer.str());
        if (!index_out->is_object()) return false;
        if (!index_out->contains("artifacts") || !(*index_out)["artifacts"].is_array()) {
            (*index_out)["artifacts"] = json::array();
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool SaveTextFile(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) return false;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

json* FindArtifactById(json& index, const std::string& artifact_id) {
    if (!index.is_object() || !index.contains("artifacts") || !index["artifacts"].is_array()) return nullptr;
    for (auto& artifact : index["artifacts"]) {
        if (artifact.is_object() && artifact.value("artifact_id", "") == artifact_id) {
            return &artifact;
        }
    }
    return nullptr;
}

json* FindLatestArtifactForKey(json& index, const std::string& artifact_key) {
    if (!index.is_object() || !index.contains("artifacts") || !index["artifacts"].is_array()) return nullptr;
    json* latest = nullptr;
    int latest_version = -1;
    for (auto& artifact : index["artifacts"]) {
        if (!artifact.is_object()) continue;
        if (artifact.value("artifact_key", "") != artifact_key) continue;
        const int version = artifact.value("version", 0);
        const bool is_latest = artifact.value("latest", false);
        if (is_latest && version >= latest_version) {
            latest = &artifact;
            latest_version = version;
        } else if (!latest && version > latest_version) {
            latest = &artifact;
            latest_version = version;
        }
    }
    return latest;
}

std::vector<json*> CollectArtifactsForKey(json& index, const std::string& artifact_key) {
    std::vector<json*> out;
    if (!index.is_object() || !index.contains("artifacts") || !index["artifacts"].is_array()) return out;
    for (auto& artifact : index["artifacts"]) {
        if (artifact.is_object() && artifact.value("artifact_key", "") == artifact_key) {
            out.push_back(&artifact);
        }
    }
    return out;
}

std::string BuildArtifactMarkdown(const json& artifact) {
    std::ostringstream stream;
    auto yaml_line = [&](const std::string& key, const std::string& value) {
        stream << key << ": " << value << "\n";
    };
    auto yaml_line_quoted = [&](const std::string& key, const std::string& value) {
        std::string escaped = value;
        escaped = ReplaceAllCopy(std::move(escaped), "\\", "\\\\");
        escaped = ReplaceAllCopy(std::move(escaped), "\"", "\\\"");
        stream << key << ": \"" << escaped << "\"\n";
    };

    stream << "---\n";
    yaml_line_quoted("artifact_id", artifact.value("artifact_id", ""));
    yaml_line_quoted("artifact_key", artifact.value("artifact_key", ""));
    yaml_line("version", std::to_string(artifact.value("version", 1)));
    yaml_line_quoted("type", artifact.value("type", ""));
    yaml_line_quoted("language", artifact.value("language", ""));
    yaml_line_quoted("status", artifact.value("status", "active"));
    yaml_line_quoted("supersedes", artifact.value("supersedes", ""));
    yaml_line_quoted("project_id", artifact.value("project_id", ""));
    yaml_line_quoted("chat_id", artifact.value("chat_id", ""));
    yaml_line("source_turn_start", std::to_string(artifact.value("source_turn_start", 0)));
    yaml_line("source_turn_end", std::to_string(artifact.value("source_turn_end", 0)));
    yaml_line_quoted("summary", artifact.value("summary", ""));
    yaml_line_quoted("user_intent", artifact.value("user_intent", ""));
    yaml_line_quoted("problem_it_solves", artifact.value("problem_it_solves", ""));
    stream << "tags:\n";
    if (artifact.contains("tags") && artifact["tags"].is_array() && !artifact["tags"].empty()) {
        for (const auto& tag : artifact["tags"]) {
            if (tag.is_string()) {
                stream << "  - \"" << ReplaceAllCopy(tag.get<std::string>(), "\"", "\\\"") << "\"\n";
            }
        }
    } else {
        stream << "  - \"artifact\"\n";
    }
    yaml_line_quoted("content_hash", artifact.value("content_hash", ""));
    yaml_line_quoted("created_at", artifact.value("created_at", ""));
    yaml_line_quoted("updated_at", artifact.value("updated_at", ""));
    stream << "---\n\n";

    stream << "# Summary\n\n" << artifact.value("summary", "") << "\n\n";
    stream << "## User Intent\n\n" << artifact.value("user_intent", "") << "\n\n";
    stream << "## Problem Solved\n\n" << artifact.value("problem_it_solves", "") << "\n\n";
    stream << "## Artifact\n\n";
    stream << "```" << artifact.value("language", "") << "\n";
    stream << artifact.value("content", "") << "\n";
    stream << "```\n";

    return stream.str();
}

std::string BuildArtifactIndexMarkdown(const json& index) {
    std::vector<json> latest;
    if (index.is_object() && index.contains("artifacts") && index["artifacts"].is_array()) {
        for (const auto& artifact : index["artifacts"]) {
            if (artifact.is_object() && artifact.value("latest", false)) {
                latest.push_back(artifact);
            }
        }
    }
    std::sort(latest.begin(), latest.end(), [](const json& left, const json& right) {
        return left.value("artifact_key", "") < right.value("artifact_key", "");
    });

    std::ostringstream stream;
    stream << "# Artifact Memory Index\n\n";
    if (latest.empty()) {
        stream << "(No artifacts stored yet)\n";
        return stream.str();
    }

    stream << "| Key | Version | Type | Summary | Status | File |\n";
    stream << "| --- | --- | --- | --- | --- | --- |\n";
    for (const auto& artifact : latest) {
        stream << "| " << artifact.value("artifact_key", "") << " | "
               << artifact.value("version", 1) << " | "
               << artifact.value("type", "") << " | "
               << artifact.value("summary", "") << " | "
               << artifact.value("status", "active") << " | "
               << artifact.value("file_path", "") << " |\n";
    }
    return stream.str();
}

std::string BuildCompactArtifactIndexSummary(const json& index, int max_rows) {
    std::vector<json> latest;
    if (index.is_object() && index.contains("artifacts") && index["artifacts"].is_array()) {
        for (const auto& artifact : index["artifacts"]) {
            if (artifact.is_object() && artifact.value("latest", false)) {
                latest.push_back(artifact);
            }
        }
    }

    std::sort(latest.begin(), latest.end(), [](const json& left, const json& right) {
        const std::string left_time = left.value("updated_at", left.value("created_at", ""));
        const std::string right_time = right.value("updated_at", right.value("created_at", ""));
        return left_time > right_time;
    });

    if (max_rows > 0 && static_cast<int>(latest.size()) > max_rows) {
        latest.resize(static_cast<size_t>(max_rows));
    }

    if (latest.empty()) return "(No artifact memory entries yet)";

    std::ostringstream stream;
    for (const auto& artifact : latest) {
        stream << "- " << artifact.value("artifact_id", "")
               << " | " << artifact.value("artifact_key", "")
               << " v" << artifact.value("version", 1)
               << " | " << artifact.value("type", "")
               << " | " << artifact.value("summary", "") << "\n";
    }
    return stream.str();
}

std::vector<json> LatestArtifacts(const json& index) {
    std::vector<json> latest;
    if (index.is_object() && index.contains("artifacts") && index["artifacts"].is_array()) {
        for (const auto& artifact : index["artifacts"]) {
            if (artifact.is_object() && artifact.value("latest", false)) {
                latest.push_back(artifact);
            }
        }
    }
    std::sort(latest.begin(), latest.end(), [](const json& left, const json& right) {
        const std::string left_time = left.value("updated_at", left.value("created_at", ""));
        const std::string right_time = right.value("updated_at", right.value("created_at", ""));
        if (left_time != right_time) return left_time > right_time;
        return left.value("artifact_key", "") < right.value("artifact_key", "");
    });
    return latest;
}

std::string BuildLayer0IndexBlock(const std::vector<json>& selected_artifacts) {
    if (selected_artifacts.empty()) {
        return "(No artifact memory entries)";
    }
    std::ostringstream stream;
    for (const auto& artifact : selected_artifacts) {
        stream << "- " << artifact.value("artifact_key", "")
               << " v" << artifact.value("version", 1)
               << " | " << artifact.value("type", "")
               << " | ";
        if (artifact.contains("relevance_note") && artifact["relevance_note"].is_string() &&
            !TrimStr(artifact["relevance_note"].get<std::string>()).empty()) {
            stream << artifact["relevance_note"].get<std::string>();
        } else {
            stream << artifact.value("summary", "");
        }
        stream << "\n";
    }
    return TrimStr(stream.str());
}

std::vector<json> FallbackSelectedArtifacts(const json& index, int max_rows) {
    auto latest = LatestArtifacts(index);
    if (max_rows > 0 && static_cast<int>(latest.size()) > max_rows) {
        latest.resize(static_cast<size_t>(max_rows));
    }
    for (auto& artifact : latest) {
        artifact["relevance_note"] = artifact.value("summary", "Latest stored artifact");
    }
    return latest;
}

std::vector<json> ExtractArtifactCandidates(
    const std::vector<MessageRecord>& new_turns,
    const json& current_index,
    const std::string& project_id,
    const std::string& chat_id,
    const Layer0Config& config,
    std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller) {
    std::vector<json> artifacts;
    if (!config.enabled || new_turns.empty()) {
        return artifacts;
    }

    const auto fallback_artifacts = ExtractFallbackArtifactCandidates(new_turns);
    if (config.capture_model_id.empty() || config.capture_model_provider_id.empty()) {
        return fallback_artifacts;
    }

    ChatRequestOptions opts;
    opts.model.id = config.capture_model_id;
    if (auto provider = OpenAIClient::LookupProvider(config.capture_model_provider_id)) {
        opts.provider = *provider;
    } else {
        return fallback_artifacts;
    }
    opts.max_tokens = 4096;
    opts.temperature = 0.1;

    std::ostringstream prompt;
    prompt << ApplyPromptTemplate(ResolveLayer0CapturePromptTemplate(config), 0) << "\n\n";
    prompt << "PROJECT ID: " << project_id << "\n";
    prompt << "CHAT ID: " << chat_id << "\n\n";
    prompt << "CURRENT ARTIFACT INDEX (compact):\n";
    prompt << BuildCompactArtifactIndexSummary(current_index, 50) << "\n\n";
    prompt << "NEW TURNS SINCE LAST L0 CAPTURE:\n";
    for (size_t i = 0; i < new_turns.size(); ++i) {
        prompt << "[Turn " << i << "][" << new_turns[i].role << "]:\n"
               << new_turns[i].content << "\n\n";
    }
    prompt << "Return a JSON object using this schema:\n"
           << "{\n"
           << "  \"artifacts\": [\n"
           << "    {\n"
           << "      \"artifact_key\": \"stable_key\",\n"
           << "      \"type\": \"html|svg|mermaid|vega-lite|cytoscape|c|cpp|python|javascript|typescript|java|json|markdown|code|...\",\n"
           << "      \"language\": \"language tag for fenced block\",\n"
           << "      \"summary\": \"short summary\",\n"
           << "      \"user_intent\": \"what the user wanted\",\n"
           << "      \"problem_it_solves\": \"why it matters\",\n"
           << "      \"tags\": [\"tag\"],\n"
           << "      \"content\": \"normalized artifact content\",\n"
           << "      \"supersedes_artifact_id\": \"optional\",\n"
           << "      \"supersedes_artifact_key\": \"optional\"\n"
           << "    }\n"
           << "  ]\n"
           << "}\n";

    MessageRecord user_message;
    user_message.role = "user";
    user_message.content = prompt.str();
    opts.messages.push_back(std::move(user_message));

    const auto result = model_caller(opts);
    if (!result || !result->success || TrimStr(result->message.content).empty()) {
        return fallback_artifacts;
    }

    const auto payload = ParseLooseJson(result->message.content);
    if (!payload) return fallback_artifacts;

    if (payload->is_array()) {
        for (const auto& item : *payload) {
            if (item.is_object()) artifacts.push_back(item);
        }
        return artifacts.empty() ? fallback_artifacts : artifacts;
    }

    if (payload->is_object() && payload->contains("artifacts") && (*payload)["artifacts"].is_array()) {
        for (const auto& item : (*payload)["artifacts"]) {
            if (item.is_object()) artifacts.push_back(item);
        }
    }
    return artifacts.empty() ? fallback_artifacts : artifacts;
}

void PersistArtifactCandidates(
    json& index,
    const std::vector<json>& candidates,
    const std::filesystem::path& storage_root,
    const std::string& project_id,
    const std::string& chat_id,
    size_t source_turn_start,
    size_t source_turn_end) {
    const std::string now = CurrentTimestampUtc();
    if (!index.contains("artifacts") || !index["artifacts"].is_array()) {
        index["artifacts"] = json::array();
    }

    for (const auto& candidate : candidates) {
        std::string content = NormalizeMultilineText(JsonStringOrEmpty(candidate, "content"));
        if (TrimStr(content).empty()) {
            continue;
        }

        std::string artifact_key = NormalizeArtifactKey(JsonStringOrEmpty(candidate, "artifact_key"));
        std::string supersedes_id = JsonStringOrEmpty(candidate, "supersedes_artifact_id");
        std::string supersedes_key = NormalizeArtifactKey(JsonStringOrEmpty(candidate, "supersedes_artifact_key"));

        if (artifact_key.empty() && !supersedes_id.empty()) {
            if (json* prior = FindArtifactById(index, supersedes_id)) {
                artifact_key = NormalizeArtifactKey(prior->value("artifact_key", ""));
            }
        }
        if (artifact_key.empty() && !supersedes_key.empty()) {
            artifact_key = supersedes_key;
        }
        if (artifact_key.empty()) {
            artifact_key = SlugFromText(
                JsonStringOrEmpty(candidate, "summary"),
                JsonStringOrEmpty(candidate, "type") + "_" + JsonStringOrEmpty(candidate, "language"));
        }
        if (artifact_key.empty()) {
            artifact_key = "artifact";
        }

        std::string type = NormalizeArtifactType(
            JsonStringOrEmpty(candidate, "type"),
            JsonStringOrEmpty(candidate, "language"));
        std::string language = NormalizeArtifactLanguage(JsonStringOrEmpty(candidate, "language"), type);
        std::vector<std::string> tags = JsonStringArray(candidate, "tags");
        if (tags.empty()) {
            tags.push_back(type);
        }
        const std::string summary = JsonStringOrEmpty(candidate, "summary");
        const std::string user_intent = JsonStringOrEmpty(candidate, "user_intent");
        const std::string problem_it_solves = JsonStringOrEmpty(candidate, "problem_it_solves");
        const std::string content_hash = Fnv1aHashHex(content);

        json* latest = FindLatestArtifactForKey(index, artifact_key);
        if (!latest && !supersedes_id.empty()) {
            latest = FindArtifactById(index, supersedes_id);
        }

        if (latest && latest->value("content_hash", "") == content_hash) {
            (*latest)["summary"] = summary;
            (*latest)["user_intent"] = user_intent;
            (*latest)["problem_it_solves"] = problem_it_solves;
            (*latest)["type"] = type;
            (*latest)["language"] = language;
            (*latest)["tags"] = tags;
            (*latest)["status"] = "active";
            (*latest)["latest"] = true;
            (*latest)["updated_at"] = now;
            (*latest)["last_seen_at"] = now;
            const std::string file_path_text = latest->value("file_path", "");
            if (!file_path_text.empty()) {
                json markdown_artifact = *latest;
                markdown_artifact["content"] = content;
                SaveTextFile(storage_root / std::filesystem::path(Utf8ToWide(file_path_text)),
                    BuildArtifactMarkdown(markdown_artifact));
            }
            continue;
        }

        int next_version = 1;
        std::string supersedes = supersedes_id;
        for (json* artifact : CollectArtifactsForKey(index, artifact_key)) {
            next_version = std::max(next_version, artifact->value("version", 0) + 1);
            if (artifact->value("latest", false)) {
                artifact->operator[]("latest") = false;
                artifact->operator[]("status") = "superseded";
                artifact->operator[]("updated_at") = now;
                supersedes = artifact->value("artifact_id", supersedes);
            }
        }

        json artifact = {
            {"artifact_id", MakeArtifactId()},
            {"artifact_key", artifact_key},
            {"version", next_version},
            {"latest", true},
            {"status", "active"},
            {"type", type},
            {"language", language},
            {"summary", summary},
            {"user_intent", user_intent},
            {"problem_it_solves", problem_it_solves},
            {"tags", tags},
            {"content_hash", content_hash},
            {"supersedes", supersedes},
            {"project_id", project_id},
            {"chat_id", chat_id},
            {"source_turn_start", static_cast<int>(source_turn_start)},
            {"source_turn_end", static_cast<int>(source_turn_end)},
            {"created_at", now},
            {"updated_at", now},
            {"last_seen_at", now},
        };

        std::ostringstream file_name;
        file_name << "artifact_" << artifact_key << "_v"
                  << std::setw(3) << std::setfill('0') << next_version << ".md";
        const std::filesystem::path relative_path = std::filesystem::path("artifacts") / file_name.str();
        artifact["file_path"] = WideToUtf8(relative_path.wstring());
        json markdown_artifact = artifact;
        markdown_artifact["content"] = content;
        index["artifacts"].push_back(artifact);

        SaveTextFile(storage_root / relative_path, BuildArtifactMarkdown(markdown_artifact));
    }

    index["updated_at"] = now;
}

std::vector<json> SelectArtifactsForInjection(
    const json& index,
    const std::string& state_json,
    const std::vector<MessageRecord>& recent_turns,
    const Layer0Config& config,
    std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller) {
    if (!config.enabled || config.selection_model_id.empty() || config.selection_model_provider_id.empty()) {
        return FallbackSelectedArtifacts(index, std::max(1, config.max_injected_rows));
    }

    ChatRequestOptions opts;
    opts.model.id = config.selection_model_id;
    if (auto provider = OpenAIClient::LookupProvider(config.selection_model_provider_id)) {
        opts.provider = *provider;
    }
    opts.max_tokens = 2048;
    opts.temperature = 0.1;

    std::ostringstream prompt;
    prompt << ResolveLayer0SelectionPromptTemplate(config) << "\n\n";
    prompt << "CURRENT ARTIFACT INDEX (latest entries):\n";
    prompt << BuildCompactArtifactIndexSummary(index, 100) << "\n\n";
    prompt << "LATEST STRUCTURED CONVERSATION STATE:\n";
    prompt << (state_json.empty() ? "{}" : state_json) << "\n\n";
    prompt << "RECENT TURNS:\n";
    for (size_t i = 0; i < recent_turns.size(); ++i) {
        prompt << "[Turn " << i << "][" << recent_turns[i].role << "]:\n"
               << recent_turns[i].content << "\n\n";
    }
    prompt << "Return JSON only using this schema:\n"
           << "{\n"
           << "  \"selected\": [\n"
           << "    {\n"
           << "      \"artifact_id\": \"artifact_...\",\n"
           << "      \"artifact_key\": \"stable_key\",\n"
           << "      \"version\": 1,\n"
           << "      \"type\": \"html\",\n"
           << "      \"relevance_note\": \"why this matters next\"\n"
           << "    }\n"
           << "  ]\n"
           << "}\n"
           << "Keep the list to at most " << std::max(1, config.max_injected_rows) << " items.";

    MessageRecord user_message;
    user_message.role = "user";
    user_message.content = prompt.str();
    opts.messages.push_back(std::move(user_message));

    const auto result = model_caller(opts);
    if (!result || !result->success || TrimStr(result->message.content).empty()) {
        return FallbackSelectedArtifacts(index, std::max(1, config.max_injected_rows));
    }

    const auto payload = ParseLooseJson(result->message.content);
    if (!payload || !payload->is_object() || !payload->contains("selected") || !(*payload)["selected"].is_array()) {
        return FallbackSelectedArtifacts(index, std::max(1, config.max_injected_rows));
    }

    std::vector<json> selected;
    for (const auto& item : (*payload)["selected"]) {
        if (!item.is_object()) continue;
        std::string artifact_id = JsonStringOrEmpty(item, "artifact_id");
        std::string artifact_key = NormalizeArtifactKey(JsonStringOrEmpty(item, "artifact_key"));

        json chosen;
        bool found = false;
        if (!artifact_id.empty()) {
            json index_copy = index;
            if (json* artifact = FindArtifactById(index_copy, artifact_id)) {
                chosen = *artifact;
                found = true;
            }
        }
        if (!found && !artifact_key.empty()) {
            json index_copy = index;
            if (json* artifact = FindLatestArtifactForKey(index_copy, artifact_key)) {
                chosen = *artifact;
                found = true;
            }
        }
        if (!found) continue;
        chosen["relevance_note"] = JsonStringOrEmpty(item, "relevance_note");
        selected.push_back(std::move(chosen));
        if (static_cast<int>(selected.size()) >= std::max(1, config.max_injected_rows)) {
            break;
        }
    }

    if (selected.empty()) {
        return FallbackSelectedArtifacts(index, std::max(1, config.max_injected_rows));
    }
    return selected;
}
}  // namespace

std::string ContextCompressionService::DefaultLayer0CapturePromptTemplate() {
    return
        "You are extracting durable artifacts from a conversation for long-term chat memory.\n\n"
        "INPUT:\n"
        "- New turns since the last compression cycle\n"
        "- A compact current artifact index\n\n"
        "TASK:\n"
        "Identify any code, diagram, markup, configuration, or structured artifacts that should be preserved as durable references.\n\n"
        "For each artifact:\n"
        "1. Decide whether it is new or a revision of an existing artifact.\n"
        "2. Propose a stable artifact_key.\n"
        "3. Summarize what the artifact is.\n"
        "4. Summarize the user intent behind it.\n"
        "5. Summarize what problem it solves.\n"
        "6. Identify artifact type and language.\n"
        "7. Return the normalized artifact content.\n"
        "8. If it supersedes a prior artifact, identify the prior artifact_id or artifact_key.\n\n"
        "RULES:\n"
        "- Prefer durable artifacts over incidental snippets.\n"
        "- Prefer the latest corrected or revised version of an artifact.\n"
        "- Do not invent code that is not present in the conversation.\n"
        "- Do not write files.\n"
        "- Return JSON only.";
}

std::string ContextCompressionService::DefaultLayer0SelectionPromptTemplate() {
    return
        "You are selecting which artifact memory entries should be surfaced in the next compressed context window.\n\n"
        "INPUT:\n"
        "- The current artifact index\n"
        "- The latest structured conversation state\n"
        "- Recent turns\n\n"
        "TASK:\n"
        "Choose the latest and most relevant artifacts that the next model call should know about.\n\n"
        "For each selected artifact:\n"
        "1. Include artifact_id\n"
        "2. Include artifact_key\n"
        "3. Include version\n"
        "4. Include type\n"
        "5. Include a short relevance note\n\n"
        "RULES:\n"
        "- Prefer latest versions unless an older version is explicitly relevant.\n"
        "- Keep the result compact.\n"
        "- Do not include full artifact content.\n"
        "- Return JSON only.";
}

std::string ContextCompressionService::DefaultLayer2PromptTemplate() {
    return
        "Generate a concise narrative summary (under {{max_tokens}} tokens) capturing:\n"
        "1. The user's original goal and any evolution of that goal\n"
        "2. Key decisions made and their reasoning\n"
        "3. Approaches attempted, including failures and why they failed\n"
        "4. Current status and what the next step should be\n"
        "5. Any constraints, preferences, or requirements the user has stated\n\n"
        "Rules:\n"
        "- Do NOT include specific code, URLs, or exact numbers (those are preserved elsewhere)\n"
        "- Do NOT summarize-from-summary: treat the previous summary as context, but prioritize accuracy from the new turns if there are contradictions\n"
        "- Write in third person past/present tense (\"The user asked for...\", \"The current approach is...\")\n"
        "- Flag any ambiguity or unresolved questions explicitly";
}

std::string ContextCompressionService::DefaultRollingSummaryPromptTemplate() {
    return
        "Update the rolling summary under {{max_tokens}} tokens.\n\n"
        "Preserve durable context that future turns need:\n"
        "1. The user's goal, requirements, and preferences\n"
        "2. Important decisions and why they were made\n"
        "3. Files, paths, commands, model/provider choices, and configuration names when they matter\n"
        "4. Work already completed, current status, blockers, and next steps\n"
        "5. Explicit user corrections or constraints\n\n"
        "Rules:\n"
        "- Merge the previous summary with the new turns; do not simply append a transcript.\n"
        "- Replace obsolete details when new turns contradict them.\n"
        "- Keep the summary compact but operationally useful.\n"
        "- Include exact names, paths, commands, and errors when they are needed to continue the work.\n"
        "- Do not include large tool outputs; summarize their effect instead.";
}

std::string ContextCompressionService::DefaultToolTraceDistillationPromptTemplate() {
    return
        "Distill the tool trace under {{max_tokens}} tokens.\n\n"
        "Keep only the operational facts that future turns need:\n"
        "1. Tool names and commands/actions used\n"
        "2. Files, folders, URLs, ports, providers, and model IDs touched\n"
        "3. Success/failure status and the exact important error messages\n"
        "4. Files changed, tests/builds run, and verification results\n"
        "5. Windows and PowerShell-specific details when they affect follow-up work\n\n"
        "Rules:\n"
        "- Drop repeated stdout, progress noise, stack traces that do not matter, and bulky raw listings.\n"
        "- Keep a concise chronological timeline.\n"
        "- Preserve exact commands and error text when they explain why a later correction was made.\n"
        "- If there were no meaningful tool traces, say that briefly.";
}

std::string ContextCompressionService::DefaultLayer3PromptTemplate() {
    return
        "Update the state object based on the new turns. Rules:\n"
        "- Only ADD or MODIFY fields that the new turns provide evidence for.\n"
        "- Do NOT remove existing entries unless the user explicitly contradicts or revokes them.\n"
        "- For decisions and failed_approaches, include the turn index for traceability.\n"
        "- Keep all values concise - single sentences, not paragraphs.\n"
        "- Return ONLY valid JSON matching the schema. No commentary.\n\n"
        "Schema:\n"
        "{{schema}}";
}

// ============================================================================
// Layer 1: Verbatim Pinning
// ============================================================================

bool MessageMatchesPinPattern(const MessageRecord& message, bool match_code, bool match_urls,
                            bool match_numbers, bool match_explicit_instructions, bool match_user_flagged) {
    // Layer 1 is for user-authored anchors, not historical tool transcripts.
    // Tool output often contains paths, numbers, and code, so allowing tools
    // here causes compression to pin exactly the noisy data it should replace.
    if (message.role != "user") {
        return false;
    }
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
        prompt << "PREVIOUS SUMMARY: (No previous summary - this is the first compression.)\n\n";
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
    prompt << "\n" << ApplyPromptTemplate(ResolveLayer2PromptTemplate(config), config.max_tokens) << "\n";

    ChatRequestOptions opts;
    opts.model.id = config.model_id;
    if (auto provider = OpenAIClient::LookupProvider(config.model_provider_id)) {
        opts.provider = *provider;
    } else {
        return prior_summary;
    }
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

    prompt << "\n"
           << ApplyPromptTemplate(ResolveLayer3PromptTemplate(config), config.max_tokens, Layer3SchemaJson())
           << "\n";

    ChatRequestOptions opts;
    opts.model.id = config.model_id;
    if (auto provider = OpenAIClient::LookupProvider(config.model_provider_id)) {
        opts.provider = *provider;
    } else {
        return prior_state_json;
    }
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
        AppendMessageToCompressedContext(
            block,
            messages[i],
            messages[i].role == "tool" ? 6000 : 12000,
            "Rolling-window context keeps a bounded copy of each message.");
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
    size_t layer0_tokens = config.layers.layer0.enabled
        ? CountTokensSimple(state.layer0_current_index_block)
        : 0;
    size_t summary_tokens = CountTokensSimple(state.layer2_previous_summary);
    size_t state_tokens = CountTokensSimple(state.layer3_previous_state_json);
    size_t overhead = 200;
    size_t used = layer0_tokens + pinned_tokens + summary_tokens + state_tokens + overhead;
    size_t remaining = used >= 4000 ? 1000 : 4000 - used;

    std::vector<MessageRecord> recency = Layer4_RecencyWindow(messages, config.layers.layer4, remaining);

    std::ostringstream block;
    block << "=== COMPRESSED CONTEXT ===\n\n";

    if (config.layers.layer0.enabled) {
        block << "## Artifact Memory (Layer 0)\n";
        block << (state.layer0_current_index_block.empty()
            ? "(No artifact memory entries)"
            : state.layer0_current_index_block) << "\n\n";
    }

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
            AppendMessageToCompressedContext(
                block,
                p,
                4000,
                "Pinned messages preserve user intent, while large verbatim text remains in the full chat log.");
        }
    } else {
        block << "(No pinned messages)\n\n";
    }

    block << "## Recent Conversation - Verbatim (Layer 4)\n";
    if (!recency.empty()) {
        for (const auto& r : recency) {
            AppendMessageToCompressedContext(
                block,
                r,
                r.role == "tool" ? 6000 : 12000,
                "Recent compressed context keeps tool output bounded; re-run or re-read tools for exact details.");
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
// Rolling Summary Strategy
// ============================================================================

std::string ContextCompressionService::BuildRollingSummaryBlock(
    const std::vector<MessageRecord>& messages,
    const ContextCompressionConfig& config,
    ChatCompressionState& state) const {

    int keep = config.truncate_top_keep_messages;
    if (keep <= 0) keep = 20;

    const size_t start = messages.size() > static_cast<size_t>(keep)
        ? messages.size() - static_cast<size_t>(keep)
        : 0;

    std::ostringstream block;
    block << "=== ROLLING SUMMARY CONTEXT ===\n\n";
    block << "## Rolling Summary\n";
    block << (state.layer2_previous_summary.empty() ? "(No rolling summary yet)" : state.layer2_previous_summary)
          << "\n\n";

    block << "## Recent Conversation - Verbatim Tail\n";
    if (messages.empty()) {
        block << "(No recent messages)\n\n";
    } else {
        for (size_t i = start; i < messages.size(); ++i) {
            AppendMessageToCompressedContext(
                block,
                messages[i],
                messages[i].role == "tool" ? 3000 : 10000,
                "Rolling summary keeps a short verbatim tail; large outputs remain in the full chat log.");
        }
    }

    state.last_compression_message_index = messages.size();
    block << "=== END ROLLING SUMMARY CONTEXT ===\n";
    return block.str();
}

std::string ContextCompressionService::CompressRollingSummary(
    const std::vector<MessageRecord>& messages,
    const ContextCompressionConfig& config,
    ChatCompressionState& state,
    std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller,
    bool force_rebuild) const {

    const size_t last_idx = force_rebuild
        ? 0
        : std::min(state.last_compression_message_index, messages.size());

    std::vector<MessageRecord> new_turns;
    for (size_t i = last_idx; i < messages.size(); ++i) {
        new_turns.push_back(messages[i]);
    }
    if (new_turns.empty() && !force_rebuild) {
        return "";
    }

    Layer2Config rolling_config = config.layers.layer2;
    if (TrimStr(rolling_config.prompt_template).empty()) {
        rolling_config.prompt_template = DefaultRollingSummaryPromptTemplate();
    }

    std::string summary;
    if (rolling_config.enabled &&
        !rolling_config.model_id.empty() &&
        !rolling_config.model_provider_id.empty()) {
        summary = Layer2_Summarize(
            {},
            new_turns,
            state.layer2_previous_summary,
            state.layer3_previous_state_json,
            rolling_config,
            model_caller);
    }

    if (TrimStr(summary).empty() ||
        (summary == state.layer2_previous_summary && !new_turns.empty() &&
         (!rolling_config.enabled || rolling_config.model_id.empty() || rolling_config.model_provider_id.empty()))) {
        summary = BuildDeterministicRollingSummary(
            state.layer2_previous_summary,
            new_turns,
            rolling_config.max_tokens);
    }

    state.layer2_previous_summary = TrimStr(summary);
    return BuildRollingSummaryBlock(messages, config, state);
}

// ============================================================================
// Tool Trace Distillation Strategy
// ============================================================================

std::string ContextCompressionService::BuildToolTraceDistillationBlock(
    const std::vector<MessageRecord>& messages,
    const ContextCompressionConfig& config,
    ChatCompressionState& state,
    std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller) const {

    Layer2Config trace_config = config.layers.layer2;
    if (TrimStr(trace_config.prompt_template).empty()) {
        trace_config.prompt_template = DefaultToolTraceDistillationPromptTemplate();
    }

    const std::string source_text = BuildToolTraceSourceText(messages);
    std::string distilled;
    if (!source_text.empty() &&
        trace_config.enabled &&
        !trace_config.model_id.empty() &&
        !trace_config.model_provider_id.empty()) {
        std::ostringstream prompt;
        prompt << "You are compressing tool calls and tool outputs for future context.\n\n";
        if (state.layer2_previous_summary.empty()) {
            prompt << "PREVIOUS DISTILLED TOOL TRACE: (none)\n\n";
        } else {
            prompt << "PREVIOUS DISTILLED TOOL TRACE:\n"
                   << state.layer2_previous_summary << "\n\n";
        }
        prompt << "NEW TOOL TRACE MATERIAL:\n" << source_text << "\n";
        prompt << ApplyPromptTemplate(
            ResolveToolTraceDistillationPromptTemplate(trace_config),
            trace_config.max_tokens) << "\n";

        ChatRequestOptions opts;
        opts.model.id = trace_config.model_id;
        if (auto provider = OpenAIClient::LookupProvider(trace_config.model_provider_id)) {
            opts.provider = *provider;
            opts.messages.push_back(MessageRecord{});
            opts.messages.back().role = "user";
            opts.messages.back().content = prompt.str();
            opts.max_tokens = trace_config.max_tokens;
            opts.temperature = 0.2;

            auto result = model_caller(opts);
            if (result && result->success && !result->message.content.empty()) {
                distilled = TrimStr(result->message.content);
            }
        }
    }

    if (distilled.empty()) {
        distilled = BuildDeterministicToolTraceSummary(
            state.layer2_previous_summary,
            messages,
            trace_config.max_tokens);
    }

    state.layer2_previous_summary = TrimStr(distilled);
    state.last_compression_message_index = messages.size();
    return FormatToolTraceDistillationContext(messages, config, state);
}

std::string ContextCompressionService::CompressToolTraceDistillation(
    const std::vector<MessageRecord>& messages,
    const ContextCompressionConfig& config,
    ChatCompressionState& state,
    std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller,
    bool force_rebuild) const {

    const size_t last_idx = force_rebuild
        ? 0
        : std::min(state.last_compression_message_index, messages.size());

    std::vector<MessageRecord> new_turns;
    for (size_t i = last_idx; i < messages.size(); ++i) {
        new_turns.push_back(messages[i]);
    }

    if (new_turns.empty() && !force_rebuild) {
        return "";
    }
    if (!HasToolTraceMessages(new_turns) && !force_rebuild) {
        state.last_compression_message_index = messages.size();
        return "";
    }

    const std::vector<MessageRecord>& source = force_rebuild ? messages : new_turns;
    (void)BuildToolTraceDistillationBlock(source, config, state, model_caller);
    state.last_compression_message_index = messages.size();
    return FormatToolTraceDistillationContext(messages, config, state);
}

std::vector<MessageRecord> ContextCompressionService::ApplyPrePass(
    const std::vector<MessageRecord>& messages,
    const ContextCompressionConfig& config,
    std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller) const {

    if (config.pre_pass_config_id.empty() || config.pre_pass_config_id == config.id || messages.empty()) {
        return messages;
    }

    auto pre_config_opt = GetGlobalConfig(config.pre_pass_config_id);
    if (!pre_config_opt || pre_config_opt->strategy == ContextCompressionStrategy::None) {
        return messages;
    }

    const ContextCompressionConfig& pre_config = *pre_config_opt;
    ChatCompressionState pre_state;
    std::string pre_block;
    if (pre_config.strategy == ContextCompressionStrategy::ToolTraceDistillation) {
        if (!HasToolTraceMessages(messages)) {
            return messages;
        }
        pre_block = BuildToolTraceDistillationBlock(messages, pre_config, pre_state, model_caller);
    } else if (pre_config.strategy == ContextCompressionStrategy::RollingSummary) {
        pre_block = CompressRollingSummary(messages, pre_config, pre_state, model_caller, true);
    } else if (pre_config.strategy == ContextCompressionStrategy::TruncateTop) {
        pre_block = BuildTruncateTopBlock(messages, pre_config, pre_state);
    } else if (pre_config.strategy == ContextCompressionStrategy::HierarchicalStructured) {
        pre_state.layer1_pinned_messages = Layer1_Pin(messages, pre_config.layers.layer1);
        pre_state.last_compression_message_index = messages.size();
        pre_block = BuildHierarchicalContextBlock(messages, pre_config, pre_state);
    }

    if (TrimStr(pre_block).empty()) {
        return messages;
    }

    std::vector<MessageRecord> preprocessed = messages;
    if (pre_config.strategy == ContextCompressionStrategy::ToolTraceDistillation) {
        bool inserted_summary = false;
        for (auto& message : preprocessed) {
            if (!IsToolTraceMessage(message)) {
                continue;
            }
            if (!inserted_summary) {
                message.role = "system";
                message.name.clear();
                message.tool_call_id.clear();
                message.tool_calls_json.clear();
                message.content =
                    "[Compression pre-pass: " + pre_config.name + "]\n" + pre_block;
                inserted_summary = true;
            } else {
                message.tool_calls_json.clear();
                message.content =
                    "[Tool trace omitted by compression pre-pass: " + pre_config.name +
                    ". Use the distilled trace above.]";
            }
        }
        return preprocessed;
    }

    preprocessed.front().content =
        "[Compression pre-pass: " + pre_config.name + "]\n" + pre_block +
        "\n[Original message]\n" + preprocessed.front().content;
    return preprocessed;
}

// ============================================================================
// Hierarchical Structured Compression Strategy
// ============================================================================

std::string ContextCompressionService::CompressHierarchical(
    const std::vector<MessageRecord>& messages,
    const ContextCompressionConfig& config,
    ChatCompressionState& state,
    std::function<std::optional<ChatCompletionResult>(const ChatRequestOptions& opts)> model_caller,
    bool force_rebuild,
    const std::vector<ProjectMcpVariableValue>& resolved_variables,
    const std::string& project_id,
    const std::string& chat_id) const {
    const size_t last_idx = force_rebuild
        ? 0
        : std::min(state.last_compression_message_index, messages.size());
    const size_t layer0_last_idx = force_rebuild
        ? 0
        : std::min(state.layer0_last_processed_message_index, messages.size());

    std::vector<MessageRecord> new_turns;
    for (size_t i = last_idx; i < messages.size(); ++i) {
        new_turns.push_back(messages[i]);
    }
    if (new_turns.empty() && !force_rebuild) {
        return "";
    }

    std::vector<MessageRecord> layer0_turns;
    for (size_t i = layer0_last_idx; i < messages.size(); ++i) {
        layer0_turns.push_back(messages[i]);
    }

    // Layer 0: artifact extraction and persistence happen before L2/L3.
    json artifact_index = EmptyArtifactIndex(project_id, chat_id);
    if (config.layers.layer0.enabled) {
        const auto storage_root = ResolveLayer0StorageRoot(config.layers.layer0, resolved_variables);
        if (!storage_root.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(storage_root / "artifacts", ec);
            state.layer0_storage_path = WideToUtf8(storage_root.wstring());

            const auto index_path = storage_root / "index.json";
            if (!force_rebuild) {
                LoadArtifactIndex(index_path, &artifact_index);
            }
            if (!artifact_index.is_object() || !artifact_index.contains("artifacts")) {
                artifact_index = EmptyArtifactIndex(project_id, chat_id);
            }

            SaveTextFile(index_path, artifact_index.dump(2));
            SaveTextFile(storage_root / "INDEX.md", BuildArtifactIndexMarkdown(artifact_index));

            if (!layer0_turns.empty()) {
                try {
                    const auto candidates = ExtractArtifactCandidates(
                        layer0_turns,
                        artifact_index,
                        project_id,
                        chat_id,
                        config.layers.layer0,
                        model_caller);
                    PersistArtifactCandidates(
                        artifact_index,
                        candidates,
                        storage_root,
                        project_id,
                        chat_id,
                        layer0_last_idx,
                        messages.empty() ? 0 : messages.size() - 1);
                } catch (...) {
                    // Keep the index files usable even if the capture model or
                    // fallback extraction fails for a specific compression pass.
                }
            }

            SaveTextFile(index_path, artifact_index.dump(2));
            SaveTextFile(storage_root / "INDEX.md", BuildArtifactIndexMarkdown(artifact_index));
            state.layer0_last_index_hash = Fnv1aHashHex(artifact_index.dump());
            state.layer0_last_processed_message_index = messages.size();
        } else {
            state.layer0_storage_path.clear();
            state.layer0_current_index_block.clear();
            state.layer0_last_index_hash.clear();
        }
    } else {
        state.layer0_current_index_block.clear();
    }

    // Layer 1: Verbatim Pinning (on full conversation for context)
    std::vector<MessageRecord> pinned = Layer1_Pin(messages, config.layers.layer1);

    // Parallel L2 and L3 using model calls
    ParallelCompressionResult parallel_result;
    std::mutex result_mutex;

    auto layer2_config = config.layers.layer2;
    auto layer3_config = config.layers.layer3;

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

        auto summary = Layer2_Summarize(
            summary_context,
            new_turns,
            state.layer2_previous_summary,
            state.layer3_previous_state_json,
            layer2_config,
            model_caller);

        std::lock_guard<std::mutex> lock(result_mutex);
        parallel_result.layer2_summary = summary;
        parallel_result.layer2_success = true;
    });

    std::thread l3_thread([&]() {
        if (!layer3_config.enabled || layer3_config.model_id.empty()) {
            std::lock_guard<std::mutex> lock(result_mutex);
            parallel_result.layer3_state_json = state.layer3_previous_state_json;
            parallel_result.layer3_success = true;
            return;
        }

        auto state_json = Layer3_ExtractState(
            new_turns,
            state.layer3_previous_state_json,
            layer3_config,
            model_caller);

        std::lock_guard<std::mutex> lock(result_mutex);
        parallel_result.layer3_state_json = state_json;
        parallel_result.layer3_success = true;
    });

    l2_thread.join();
    l3_thread.join();

    if (parallel_result.layer2_success) {
        state.layer2_previous_summary = parallel_result.layer2_summary;
    }
    if (parallel_result.layer3_success) {
        state.layer3_previous_state_json = parallel_result.layer3_state_json;
    }

    // Layer 0 selection runs after L3 so it can use the latest state.
    if (config.layers.layer0.enabled) {
        std::vector<json> selected_artifacts = SelectArtifactsForInjection(
            artifact_index,
            state.layer3_previous_state_json,
            new_turns.empty() ? messages : new_turns,
            config.layers.layer0,
            model_caller);
        state.layer0_current_index_block = BuildLayer0IndexBlock(selected_artifacts);
    }

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
    const std::string& trigger_reason,
    const std::vector<ProjectMcpVariableValue>& resolved_variables) {

    auto config_opt = GetGlobalConfig(config_id);
    if (!config_opt) {
        return "";
    }

    ChatCompressionState state = LoadChatState(project_id, chat_id);
    const ChatCompressionState previous_state = state;
    const size_t previous_message_index = std::min(state.last_compression_message_index, messages.size());
    const std::vector<MessageRecord> working_messages = ApplyPrePass(messages, *config_opt, model_caller);
    std::string block;

    if (config_opt->strategy == ContextCompressionStrategy::TruncateTop) {
        block = CompressTruncateTop(working_messages, *config_opt, state);
        if (block.empty() && force_rebuild) {
            block = BuildTruncateTopBlock(working_messages, *config_opt, state);
        }
    } else if (config_opt->strategy == ContextCompressionStrategy::RollingSummary) {
        block = CompressRollingSummary(
            working_messages,
            *config_opt,
            state,
            model_caller,
            force_rebuild);
        if (block.empty() && force_rebuild) {
            block = BuildRollingSummaryBlock(working_messages, *config_opt, state);
        }
    } else if (config_opt->strategy == ContextCompressionStrategy::ToolTraceDistillation) {
        block = CompressToolTraceDistillation(
            working_messages,
            *config_opt,
            state,
            model_caller,
            force_rebuild);
        if (block.empty() && force_rebuild) {
            block = BuildToolTraceDistillationBlock(working_messages, *config_opt, state, model_caller);
        }
    } else if (config_opt->strategy == ContextCompressionStrategy::HierarchicalStructured) {
        block = CompressHierarchical(
            working_messages,
            *config_opt,
            state,
            model_caller,
            force_rebuild,
            resolved_variables,
            project_id,
            chat_id);
        if (block.empty() && force_rebuild) {
            state.layer1_pinned_messages = Layer1_Pin(working_messages, config_opt->layers.layer1);
            state.last_compression_message_index = working_messages.size();
            block = BuildHierarchicalContextBlock(working_messages, *config_opt, state);
        }
    } else {
        return "";
    }

    if (!block.empty()) {
        state.last_compression_message_index = std::min(state.last_compression_message_index, messages.size());
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
        snapshot.layer0_index_block = state.layer0_current_index_block;
        snapshot.layer0_previous_index_hash = previous_state.layer0_last_index_hash;
        snapshot.layer0_index_hash = state.layer0_last_index_hash;
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

std::string ContextCompressionService::RebuildCompressedContextFromExistingState(
    const std::vector<MessageRecord>& messages,
    const std::string& project_id,
    const std::string& chat_id,
    const std::string& config_id) {
    auto config_opt = GetGlobalConfig(config_id);
    if (!config_opt) {
        return "";
    }

    ChatCompressionState state = LoadChatState(project_id, chat_id);
    const size_t compressed_through =
        state.last_compression_message_index == 0
            ? messages.size()
            : std::min(state.last_compression_message_index, messages.size());
    std::vector<MessageRecord> compressed_prefix(
        messages.begin(),
        messages.begin() + compressed_through);

    std::string block;
    if (config_opt->strategy == ContextCompressionStrategy::TruncateTop) {
        block = BuildTruncateTopBlock(compressed_prefix, *config_opt, state);
    } else if (config_opt->strategy == ContextCompressionStrategy::RollingSummary) {
        state.last_compression_message_index = compressed_through;
        block = BuildRollingSummaryBlock(compressed_prefix, *config_opt, state);
    } else if (config_opt->strategy == ContextCompressionStrategy::ToolTraceDistillation) {
        state.last_compression_message_index = compressed_through;
        block = BuildToolTraceDistillationBlock(
            compressed_prefix,
            *config_opt,
            state,
            [](const ChatRequestOptions&) -> std::optional<ChatCompletionResult> { return std::nullopt; });
    } else if (config_opt->strategy == ContextCompressionStrategy::HierarchicalStructured) {
        state.layer1_pinned_messages = Layer1_Pin(compressed_prefix, config_opt->layers.layer1);
        state.last_compression_message_index = compressed_through;
        block = BuildHierarchicalContextBlock(compressed_prefix, *config_opt, state);
    }

    if (!block.empty()) {
        state.current_compressed_context = block;
        SaveChatState(project_id, chat_id, state);
    }
    return block;
}

std::string ContextCompressionService::BuildCompressedContextBlock(
    const ContextCompressionConfig& config,
    const ChatCompressionState& state) const {
    if (!state.current_compressed_context.empty()) {
        return state.current_compressed_context;
    }

    if (config.strategy == ContextCompressionStrategy::RollingSummary) {
        std::ostringstream block;
        block << "=== ROLLING SUMMARY CONTEXT ===\n\n";
        block << "## Rolling Summary\n";
        block << (state.layer2_previous_summary.empty() ? "(No rolling summary yet)" : state.layer2_previous_summary)
              << "\n\n";
        block << "=== END ROLLING SUMMARY CONTEXT ===\n";
        return block.str();
    }

    if (config.strategy == ContextCompressionStrategy::ToolTraceDistillation) {
        std::ostringstream block;
        block << "=== TOOL TRACE DISTILLATION ===\n\n";
        block << (state.layer2_previous_summary.empty()
            ? "No meaningful tool traces have been distilled yet."
            : state.layer2_previous_summary)
              << "\n\n";
        block << "=== END TOOL TRACE DISTILLATION ===\n";
        return block.str();
    }

    std::ostringstream block;
    block << "=== COMPRESSED CONTEXT ===\n\n";

    if (config.layers.layer0.enabled) {
        block << "[Artifact Memory (Layer 0)]\n";
        block << (state.layer0_current_index_block.empty()
            ? "(No artifact memory entries)"
            : state.layer0_current_index_block) << "\n\n";
    }

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
