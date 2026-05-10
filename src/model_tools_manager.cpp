#include "model_tools_manager.h"

#include "util.h"

#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kModelToolsClassName[] = L"AgentModelToolsManager";
constexpr wchar_t kToolTestClassName[]   = L"AgentToolTestWindow";

enum ControlId : int {
    // Left panel
    kToolList      = 7101,
    kAddTool       = 7102,
    kRemoveTool    = 7103,

    // Right panel – header
    kNameLabel     = 7110,
    kNameEdit      = 7111,
    kDescLabel     = 7112,
    kDescEdit      = 7113,
    kModelLabel    = 7114,
    kModelCombo    = 7115,

    // Context window compression
    kCompressionLabel = 7116,
    kCompressionCombo = 7117,

    // Right panel – MCP servers
    kMcpHeader        = 7120,
    kMcpList          = 7121,
    kMcpDetailsPanel  = 7122,   // groupbox below MCP list
    kMcpEnabledCheck  = 7123,   // "Use this server" checkbox in details panel
    kMcpVarHeader     = 7124,   // "Variables:" label in details panel
    // Browse buttons for MCP variable edits: IDs kMcpBrowseVarBase..kMcpBrowseVarBase+99
    kMcpBrowseVarBase = 8000,

    // Right panel – RAG services
    kRagHeader        = 7130,
    kRagList          = 7131,
    kRagEnabledCheck  = 7132,
    kRagReadCheck     = 7133,
    kRagWriteCheck    = 7134,
    kRagToolCheck     = 7135,
    kRagDeleteCheck   = 7136,
    kRagExportCheck   = 7137,
    kRagDefaultIngest = 7138,
    kRagInjectOnStart = 7139,
    kRagPriorityLbl   = 7140,
    kRagPriorityEdit  = 7141,
    kRagMaxChunksLbl  = 7142,
    kRagMaxChunksEdit = 7143,
    kRagMinConfLbl    = 7144,
    kRagMinConfEdit   = 7145,
    kRagMaxConfLbl    = 7146,
    kRagMaxConfEdit   = 7147,
    kRagExportPathLbl = 7148,
    kRagExportPathEdit= 7149,
    kRagRetModeLbl    = 7150,
    kRagRetModeCombo  = 7151,

    // Right panel – instructions
    kInstructionsLbl    = 7160,
    kImportMarkdown     = 7161,
    kInstructionsEdit   = 7162,

    // Footer
    kTestButton   = 7163,
    kSaveButton   = IDOK,
    kCloseButton  = IDCANCEL,

    // Test-window controls
    kTestTopLabel    = 7170,
    kTestTopEdit     = 7171,
    kTestBottomLabel = 7172,
    kTestBottomEdit  = 7173,
    kTestCloseButton = 7174,
};

// ──────────────────────────────────────────────────────────────────────────────
// Helper structs
// ──────────────────────────────────────────────────────────────────────────────

struct McpServerRow {
    std::string server_id;
    std::string server_name;
    bool selected = false;
    std::vector<ProjectMcpVariableValue> variables;
};

struct McpVarControl {
    std::string name;
    McpVariableKind kind = McpVariableKind::None;
    HWND label  = nullptr;
    HWND edit   = nullptr;
    HWND browse = nullptr;
};

struct RagRow {
    std::string rag_id;
    std::string rag_name;
    bool enabled = false;
    bool can_read = false;
    bool can_write = false;
    bool expose_as_tool = false;
    bool can_delete = false;
    bool can_export = false;
    std::string export_path_template;
    bool default_ingest_target = false;
    bool inject_on_start = false;   // pre-search before first model call
    int retrieval_priority = 10;
    int max_chunks = 8;
    double default_min_confidence = 0.0;
    double default_max_confidence = 1.0;
    RagRetrievalMode retrieval_mode = RagRetrievalMode::Both;
};

// ──────────────────────────────────────────────────────────────────────────────
// Small utilities
// ──────────────────────────────────────────────────────────────────────────────

static int Scale(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

static std::wstring FormatConfidence(double v) {
    std::wostringstream ss;
    ss << std::fixed << std::setprecision(2) << std::clamp(v, 0.0, 1.0);
    return ss.str();
}

static std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file");
    std::string content{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xef &&
        static_cast<unsigned char>(content[1]) == 0xbb &&
        static_cast<unsigned char>(content[2]) == 0xbf) {
        content.erase(0, 3);
    }
    return content;
}

static std::optional<std::filesystem::path> PickMarkdownFile(HWND owner) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = static_cast<DWORD>(std::size(path));
    ofn.lpstrFilter = L"Markdown and Text Files\0*.md;*.markdown;*.txt\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"Import Tool Instructions";
    if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    return std::filesystem::path(path);
}

static int RagModeToComboIndex(RagRetrievalMode mode) {
    switch (mode) {
        case RagRetrievalMode::PassiveOnly:    return 1;
        case RagRetrievalMode::ActiveToolOnly: return 2;
        case RagRetrievalMode::Disabled:       return 3;
        default:                               return 0;
    }
}

static RagRetrievalMode ComboIndexToRagMode(int idx) {
    switch (idx) {
        case 1:  return RagRetrievalMode::PassiveOnly;
        case 2:  return RagRetrievalMode::ActiveToolOnly;
        case 3:  return RagRetrievalMode::Disabled;
        default: return RagRetrievalMode::Both;
    }
}

static void NormalizeRagRow(RagRow& row) {
    row.max_chunks = std::clamp(row.max_chunks, 1, 200);
    row.default_min_confidence = std::clamp(row.default_min_confidence, 0.0, 1.0);
    row.default_max_confidence = std::clamp(row.default_max_confidence, 0.0, 1.0);
    if (row.default_min_confidence > row.default_max_confidence)
        std::swap(row.default_min_confidence, row.default_max_confidence);

    if (row.can_delete || row.default_ingest_target) { row.enabled = true; row.can_read = true; row.can_write = true; }
    if (row.can_write || row.expose_as_tool || row.can_export) { row.enabled = true; row.can_read = true; }
    if (!row.enabled) {
        row.can_read = row.can_write = row.expose_as_tool = row.can_delete = row.can_export = row.default_ingest_target = false;
        row.inject_on_start = false;
    }
    if (!row.can_read) { row.can_write = row.expose_as_tool = row.can_delete = row.can_export = row.default_ingest_target = false; }
    if (!row.can_write) { row.can_delete = row.default_ingest_target = false; }

    // Passive/pre-search RAG injection is inactive in this phase.
    row.inject_on_start = false;
}

// Build a sanitized tool name for agent_ prefix (mirrors SanitizeToolName in main.cpp)
static std::string LocalSanitizeToolName(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            result += '_';
        }
    }
    std::string collapsed;
    bool prev_under = false;
    for (char c : result) {
        if (c == '_' && prev_under) continue;
        prev_under = (c == '_');
        collapsed += c;
    }
    if (!collapsed.empty() && collapsed.back() == '_') collapsed.pop_back();
    return collapsed;
}

// ──────────────────────────────────────────────────────────────────────────────
// Build caller-context text (what the parent model sees as the tool definition)
// ──────────────────────────────────────────────────────────────────────────────
static std::string BuildCallerContextText(const ModelToolConfig& tool,
                                          const std::vector<McpServerConfig>& servers,
                                          const std::vector<RagLibraryConfig>& available_rags)
{
    // Build description with auto-appended RAG/capability notes
    std::string description = tool.description.empty()
        ? ("Sub-agent: " + tool.name)
        : tool.description;

    std::vector<std::string> rag_names;
    for (const auto& binding : tool.rag_bindings) {
        if (!binding.enabled) continue;
        if (binding.can_read && binding.expose_as_tool &&
            binding.retrieval_mode != RagRetrievalMode::PassiveOnly &&
            binding.retrieval_mode != RagRetrievalMode::Disabled) {
            // Find the display name
            std::string rag_name = binding.rag_id;
            for (const auto& lib : available_rags) {
                if (lib.id == binding.rag_id) { rag_name = lib.name; break; }
            }
            rag_names.push_back(rag_name);
        }
    }

    // Collect enabled MCP servers
    std::vector<std::string> mcp_names;
    for (const auto& binding : tool.mcp_bindings) {
        std::string srv_name = binding.server_id;
        for (const auto& srv : servers) {
            if (srv.id == binding.server_id) { srv_name = srv.name; break; }
        }
        mcp_names.push_back(srv_name);
    }

    // Append automatic notes to description
    std::string notes;
    if (!mcp_names.empty()) {
        notes += " Has access to: ";
        for (size_t i = 0; i < mcp_names.size(); ++i) {
            if (i) notes += ", ";
            notes += mcp_names[i];
        }
        notes += ".";
    }
    if (!rag_names.empty()) {
        notes += " Has access to RAG MCP server(s): ";
        for (size_t i = 0; i < rag_names.size(); ++i) {
            if (i) notes += ", ";
            notes += rag_names[i];
        }
        notes += ".";
    }

    // Build param description
    std::string instructions_desc = "Detailed task instructions for this agent. Describe exactly what you want accomplished.";
    if (!mcp_names.empty() || !rag_names.empty()) {
        instructions_desc += " Available capabilities:";
        if (!mcp_names.empty()) {
            instructions_desc += " tools from [";
            for (size_t i = 0; i < mcp_names.size(); ++i) { if (i) instructions_desc += ", "; instructions_desc += mcp_names[i]; }
            instructions_desc += "]";
        }
        if (!rag_names.empty()) {
            instructions_desc += " and RAG MCP servers [";
            for (size_t i = 0; i < rag_names.size(); ++i) { if (i) instructions_desc += ", "; instructions_desc += rag_names[i]; }
            instructions_desc += "]";
        }
        instructions_desc += ". Example: 'Search for X, then write a summary to Y.'";
    }

    // Build tool definition JSON (OpenAI function format)
    std::string safe_name = "agent_" + LocalSanitizeToolName(tool.name.empty() ? "tool" : tool.name);

    // Escape quotes in strings for JSON embedding
    auto esc = [](const std::string& s) {
        std::string r;
        for (char c : s) {
            if (c == '"') r += "\\\"";
            else if (c == '\\') r += "\\\\";
            else if (c == '\n') r += "\\n";
            else if (c == '\r') {}
            else r += c;
        }
        return r;
    };

    std::ostringstream j;
    j << "{\n"
      << "  \"type\": \"function\",\n"
      << "  \"function\": {\n"
      << "    \"name\": \"" << esc(safe_name) << "\",\n"
      << "    \"description\": \"" << esc(description + notes) << "\",\n"
      << "    \"parameters\": {\n"
      << "      \"type\": \"object\",\n"
      << "      \"properties\": {\n"
      << "        \"instructions\": {\n"
      << "          \"type\": \"string\",\n"
      << "          \"description\": \"" << esc(instructions_desc) << "\"\n"
      << "        }\n"
      << "      },\n"
      << "      \"required\": [\"instructions\"]\n"
      << "    }\n"
      << "  }\n"
      << "}";
    return j.str();
}

// ──────────────────────────────────────────────────────────────────────────────
// Build tool-context text (what the sub-agent sees as its system prompt template)
// ──────────────────────────────────────────────────────────────────────────────
static std::string BuildToolContextText(const ModelToolConfig& tool,
                                        const std::vector<McpServerConfig>& servers,
                                        const std::vector<RagLibraryConfig>& available_rags)
{
    std::ostringstream out;

    // System instructions
    out << "=== TOOL SYSTEM INSTRUCTIONS ===\n";
    if (!tool.instructions.empty()) {
        out << tool.instructions << "\n";
    } else {
        out << "(No instructions defined)\n";
    }

    // MCP tools
    if (!tool.mcp_bindings.empty()) {
        out << "\n=== ACCESSIBLE MCP SERVERS ===\n";
        for (const auto& binding : tool.mcp_bindings) {
            std::string name = binding.server_id;
            for (const auto& srv : servers) {
                if (srv.id == binding.server_id) { name = srv.name; break; }
            }
            out << "  • " << name << "\n";
        }
    }

    // RAG libraries
    if (!tool.rag_bindings.empty()) {
        out << "\n=== ACCESSIBLE RAG LIBRARIES ===\n";
        for (const auto& binding : tool.rag_bindings) {
            if (!binding.enabled) continue;
            std::string name = binding.rag_id;
            for (const auto& lib : available_rags) {
                if (lib.id == binding.rag_id) { name = lib.name; break; }
            }
            out << "  • " << name << "\n";

            auto mode_str = [](RagRetrievalMode m) -> const char* {
                switch (m) {
                    case RagRetrievalMode::PassiveOnly:    return "Passive only (inactive)";
                    case RagRetrievalMode::ActiveToolOnly: return "Active tool only";
                    case RagRetrievalMode::Disabled:       return "Disabled";
                    default:                               return "Tool access (passive later)";
                }
            };

            out << "    Read=" << (binding.can_read ? "yes" : "no")
                << "  Write=" << (binding.can_write ? "yes" : "no")
                << "  Tool=" << (binding.expose_as_tool ? "yes" : "no")
                << "  Priority=" << binding.retrieval_priority
                << "  MaxChunks=" << binding.max_chunks
                << "\n"
                << "    RetrievalMode=" << mode_str(binding.retrieval_mode)
                << "  Confidence=[" << std::fixed << std::setprecision(2)
                << binding.default_min_confidence << " - "
                << binding.default_max_confidence << "]\n";

            out << "    Passive/pre-search injection is inactive in this phase.\n";

        }
    }

    // Structured reply format (auto-appended to every call)
    out << "\n=== AUTO-APPENDED RESPONSE FORMAT ===\n";
    out << "At the end of your FINAL response, include this JSON block:\n";
    out << "{\n"
        << "  \"status\": \"complete|partial|needs_clarification\",\n"
        << "  \"summary\": \"One-sentence summary of what was accomplished\",\n"
        << "  \"result\": \"The main output, answer, or result of the task\",\n"
        << "  \"actions\": [\"list\", \"of\", \"actions\", \"taken\"]\n"
        << "}\n";

    // Sample call
    out << "\n=== SAMPLE INVOCATION ===\n";
    out << "{\n"
        << "  \"instructions\": \"[Sample] Describe the task for this agent here.";
    if (!tool.mcp_bindings.empty()) {
        out << " Use the available MCP tools as needed.";
    }
    if (!tool.rag_bindings.empty()) {
        for (const auto& b : tool.rag_bindings) {
            if (b.enabled && (b.retrieval_mode == RagRetrievalMode::Both || b.retrieval_mode == RagRetrievalMode::PassiveOnly)) {
                out << " Relevant context will be auto-searched from the RAG libraries.";
                break;
            }
        }
    }
    out << "\"\n}\n";

    return out.str();
}

// ──────────────────────────────────────────────────────────────────────────────
// ToolTestWindow – simple popup showing caller context + tool context
// ──────────────────────────────────────────────────────────────────────────────

class ToolTestWindow {
public:
    static void Show(HWND owner,
                     const ModelToolConfig& tool,
                     const std::vector<McpServerConfig>& servers,
                     const std::vector<RagLibraryConfig>& available_rags)
    {
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
        RegisterWndClass(inst);

        auto* w = new ToolTestWindow(tool, servers, available_rags);
        HWND hwnd = CreateWindowExW(
            WS_EX_CONTROLPARENT,
            kToolTestClassName,
            L"Tool Test Preview",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            800, 700,
            owner,
            nullptr, inst, w);

        if (!hwnd) { delete w; return; }
        // modeless – owner continues normally
    }

private:
    ToolTestWindow(const ModelToolConfig& tool,
                   const std::vector<McpServerConfig>& servers,
                   const std::vector<RagLibraryConfig>& available_rags)
        : caller_text_(BuildCallerContextText(tool, servers, available_rags))
        , tool_text_(BuildToolContextText(tool, servers, available_rags))
    {}

    static void RegisterWndClass(HINSTANCE inst) {
        static bool done = false;
        if (done) return;
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.hInstance     = inst;
        wc.lpfnWndProc   = &ToolTestWindow::WndProc;
        wc.lpszClassName = kToolTestClassName;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);
        done = true;
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<ToolTestWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<ToolTestWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        switch (msg) {
        case WM_CREATE:
            self->OnCreate();
            return 0;
        case WM_SIZE:
            self->Layout(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == kTestCloseButton || LOWORD(wp) == IDCANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_DESTROY:
            delete self;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void OnCreate() {
        HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HFONT mono = CreateFontW(-Scale(hwnd_, 12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        top_label_ = CreateWindowExW(0, L"STATIC",
            L"Caller Context — tool definition injected into the calling model's context:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kTestTopLabel), nullptr, nullptr);
        top_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kTestTopEdit), nullptr, nullptr);

        bottom_label_ = CreateWindowExW(0, L"STATIC",
            L"Tool Context — sub-agent system prompt when this tool is called:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kTestBottomLabel), nullptr, nullptr);
        bottom_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kTestBottomEdit), nullptr, nullptr);

        close_button_ = CreateWindowExW(0, L"BUTTON", L"Close",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kTestCloseButton), nullptr, nullptr);

        SendMessageW(top_label_,    WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SendMessageW(bottom_label_, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SendMessageW(close_button_, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        SendMessageW(top_edit_,    WM_SETFONT, reinterpret_cast<WPARAM>(mono ? mono : font), FALSE);
        SendMessageW(bottom_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(mono ? mono : font), FALSE);

        SetWindowTextW(top_edit_,    Utf8ToWide(caller_text_).c_str());
        SetWindowTextW(bottom_edit_, Utf8ToWide(tool_text_).c_str());

        Layout(800, 700);
    }

    void Layout(int w, int h) const {
        const int margin    = Scale(hwnd_, 12);
        const int gutter    = Scale(hwnd_, 8);
        const int label_h   = Scale(hwnd_, 18);
        const int button_h  = Scale(hwnd_, 28);
        const int button_w  = Scale(hwnd_, 80);
        const int footer_y  = h - margin - button_h;

        const int half_h    = (footer_y - margin - label_h * 2 - gutter * 4) / 2;

        int y = margin;
        MoveWindow(top_label_, margin, y, w - 2 * margin, label_h, TRUE);
        y += label_h + gutter;
        MoveWindow(top_edit_, margin, y, w - 2 * margin, half_h, TRUE);
        y += half_h + gutter * 2;

        MoveWindow(bottom_label_, margin, y, w - 2 * margin, label_h, TRUE);
        y += label_h + gutter;
        MoveWindow(bottom_edit_, margin, y, w - 2 * margin, footer_y - y - gutter, TRUE);

        MoveWindow(close_button_, w - margin - button_w, footer_y, button_w, button_h, TRUE);
    }

    HWND hwnd_ = nullptr;
    std::string caller_text_;
    std::string tool_text_;

    HWND top_label_    = nullptr;
    HWND top_edit_     = nullptr;
    HWND bottom_label_ = nullptr;
    HWND bottom_edit_  = nullptr;
    HWND close_button_ = nullptr;
};

// ──────────────────────────────────────────────────────────────────────────────
// ModelToolsManager window class
// ──────────────────────────────────────────────────────────────────────────────

class ModelToolsManager {
public:
    ModelToolsManager(HWND owner, AppStorage* storage,
                      const std::vector<ProviderConfig>& providers,
                      McpManager* mcp_manager, RagService* rag_service)
        : owner_(owner), storage_(storage), providers_(providers),
          mcp_manager_(mcp_manager), rag_service_(rag_service) {
        tools_ = storage_->LoadModelTools();
        available_servers_ = mcp_manager_->configs();
        available_rags_ = rag_service_->ListLibraries();
        compression_configs_ = storage_->LoadCompressionConfigs();
    }

    HWND Create() {
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
        RegisterClass(inst);

        hwnd_ = CreateWindowExW(
            WS_EX_CONTROLPARENT,
            kModelToolsClassName,
            L"Model Tools",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            1100, 820,
            owner_,
            nullptr, inst, this);

        return hwnd_;
    }

private:
    static void RegisterClass(HINSTANCE inst) {
        static bool registered = false;
        if (registered) return;
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.hInstance     = inst;
        wc.lpfnWndProc   = &ModelToolsManager::WndProc;
        wc.lpszClassName = kModelToolsClassName;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        registered = true;
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<ModelToolsManager*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<ModelToolsManager*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        switch (msg) {
        case WM_CREATE:    self->OnCreate();  return 0;
        case WM_SIZE:
            self->LayoutControls(LOWORD(lp), HIWORD(lp));
            self->LayoutMcpVariableControls();
            return 0;
        case WM_COMMAND:   self->OnCommand(LOWORD(wp), HIWORD(wp)); return 0;
        case WM_CLOSE:     self->OnClose(); return 0;
        case WM_DESTROY:   delete self; return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // ── Creation ───────────────────────────────────────────────────────────────

    void OnCreate() {
        font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        // Left panel
        tool_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kToolList), nullptr, nullptr);
        add_tool_button_ = CreateWindowExW(0, L"BUTTON", L"+ Add",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kAddTool), nullptr, nullptr);
        remove_tool_button_ = CreateWindowExW(0, L"BUTTON", L"- Remove",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kRemoveTool), nullptr, nullptr);

        // Right panel – Name
        name_label_ = CreateWindowExW(0, L"STATIC", L"Tool Name:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kNameLabel), nullptr, nullptr);
        name_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kNameEdit), nullptr, nullptr);

        // Description
        desc_label_ = CreateWindowExW(0, L"STATIC", L"Description (shown to calling model):",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kDescLabel), nullptr, nullptr);
        desc_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDescEdit), nullptr, nullptr);

        // Model selection
        model_label_ = CreateWindowExW(0, L"STATIC", L"AI Model:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kModelLabel), nullptr, nullptr);
        model_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kModelCombo), nullptr, nullptr);
        PopulateModelCombo("", "");

        // Context window compression
        compression_label_ = CreateWindowExW(0, L"STATIC", L"Context Window Compression:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kCompressionLabel), nullptr, nullptr);
        compression_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCompressionCombo), nullptr, nullptr);
        ComboBox_AddString(compression_combo_, L"None");
        for (const auto& cfg : compression_configs_) {
            ComboBox_AddString(compression_combo_, Utf8ToWide(cfg.name).c_str());
        }
        ComboBox_SetCurSel(compression_combo_, 0);

        // MCP servers section
        mcp_header_ = CreateWindowExW(0, L"STATIC", L"MCP Server Access:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kMcpHeader), nullptr, nullptr);
        mcp_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kMcpList), nullptr, nullptr);
        // MCP server details panel (groupbox, shown below the list when a server is selected)
        mcp_details_panel_ = CreateWindowExW(0, L"BUTTON", nullptr,
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kMcpDetailsPanel), nullptr, nullptr);
        mcp_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Use this MCP server",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kMcpEnabledCheck), nullptr, nullptr);
        mcp_var_header_ = CreateWindowExW(0, L"STATIC", L"Variables:",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kMcpVarHeader), nullptr, nullptr);

        // RAG section
        rag_header_ = CreateWindowExW(0, L"STATIC", L"RAG Library Access:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kRagHeader), nullptr, nullptr);
        rag_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagList), nullptr, nullptr);
        rag_enabled_check_  = CreateWindowExW(0, L"BUTTON", L"Enabled",       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagEnabledCheck),  nullptr, nullptr);
        rag_read_check_     = CreateWindowExW(0, L"BUTTON", L"Read",          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagReadCheck),     nullptr, nullptr);
        rag_write_check_    = CreateWindowExW(0, L"BUTTON", L"Write",         WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagWriteCheck),    nullptr, nullptr);
        rag_tool_check_     = CreateWindowExW(0, L"BUTTON", L"Tool",          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagToolCheck),     nullptr, nullptr);
        rag_delete_check_   = CreateWindowExW(0, L"BUTTON", L"Delete",        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagDeleteCheck),   nullptr, nullptr);
        rag_export_check_   = CreateWindowExW(0, L"BUTTON", L"Write file",    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagExportCheck),   nullptr, nullptr);
        rag_default_ingest_ = CreateWindowExW(0, L"BUTTON", L"Default ingest",WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagDefaultIngest), nullptr, nullptr);
        rag_inject_onstart_ = CreateWindowExW(0, L"BUTTON", L"Inject on start (inactive)",WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagInjectOnStart), nullptr, nullptr);
        rag_priority_lbl_   = CreateWindowExW(0, L"STATIC", L"Priority:",     WS_CHILD | WS_VISIBLE, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagPriorityLbl), nullptr, nullptr);
        rag_priority_edit_  = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagPriorityEdit), nullptr, nullptr);
        rag_maxchunks_lbl_  = CreateWindowExW(0, L"STATIC", L"Max chunks:",   WS_CHILD | WS_VISIBLE, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagMaxChunksLbl), nullptr, nullptr);
        rag_maxchunks_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagMaxChunksEdit), nullptr, nullptr);
        rag_minconf_lbl_    = CreateWindowExW(0, L"STATIC", L"Min confidence:", WS_CHILD | WS_VISIBLE, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagMinConfLbl), nullptr, nullptr);
        rag_minconf_edit_   = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagMinConfEdit), nullptr, nullptr);
        rag_maxconf_lbl_    = CreateWindowExW(0, L"STATIC", L"Max confidence:", WS_CHILD | WS_VISIBLE, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagMaxConfLbl), nullptr, nullptr);
        rag_maxconf_edit_   = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagMaxConfEdit), nullptr, nullptr);
        rag_exportpath_lbl_ = CreateWindowExW(0, L"STATIC", L"Write file folder:", WS_CHILD | WS_VISIBLE, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagExportPathLbl), nullptr, nullptr);
        rag_exportpath_edit_= CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagExportPathEdit), nullptr, nullptr);
        rag_retmode_lbl_    = CreateWindowExW(0, L"STATIC", L"Retrieval mode:", WS_CHILD | WS_VISIBLE, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagRetModeLbl), nullptr, nullptr);
        rag_retmode_combo_  = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kRagRetModeCombo), nullptr, nullptr);
        ComboBox_AddString(rag_retmode_combo_, L"Tool access (passive later)");
        ComboBox_AddString(rag_retmode_combo_, L"Passive only (inactive)");
        ComboBox_AddString(rag_retmode_combo_, L"Active tool only");
        ComboBox_AddString(rag_retmode_combo_, L"Disabled");

        // Instructions
        instructions_lbl_ = CreateWindowExW(0, L"STATIC", L"Tool Instructions (not exposed to calling model):",
            WS_CHILD | WS_VISIBLE, 0,0,0,0, hwnd_,
            reinterpret_cast<HMENU>(kInstructionsLbl), nullptr, nullptr);
        import_markdown_button_ = CreateWindowExW(0, L"BUTTON", L"Import Markdown",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,0,0,0, hwnd_,
            reinterpret_cast<HMENU>(kImportMarkdown), nullptr, nullptr);
        instructions_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kInstructionsEdit), nullptr, nullptr);

        // Footer
        test_button_  = CreateWindowExW(0, L"BUTTON", L"Test",  WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kTestButton),  nullptr, nullptr);
        save_button_  = CreateWindowExW(0, L"BUTTON", L"Save",  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kSaveButton),  nullptr, nullptr);
        close_button_ = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP,                    0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kCloseButton), nullptr, nullptr);

        // Apply font
        HWND all[] = {
            tool_list_, add_tool_button_, remove_tool_button_,
            name_label_, name_edit_, desc_label_, desc_edit_,
            model_label_, model_combo_,
            compression_label_, compression_combo_,
            mcp_header_, mcp_list_, mcp_details_panel_, mcp_enabled_check_, mcp_var_header_,
            rag_header_, rag_list_,
            rag_enabled_check_, rag_read_check_, rag_write_check_, rag_tool_check_,
            rag_delete_check_, rag_export_check_, rag_default_ingest_, rag_inject_onstart_,
            rag_priority_lbl_, rag_priority_edit_, rag_maxchunks_lbl_, rag_maxchunks_edit_,
            rag_minconf_lbl_, rag_minconf_edit_, rag_maxconf_lbl_, rag_maxconf_edit_,
            rag_exportpath_lbl_, rag_exportpath_edit_, rag_retmode_lbl_, rag_retmode_combo_,
            instructions_lbl_, import_markdown_button_, instructions_edit_,
            test_button_, save_button_, close_button_,
        };
        for (HWND h : all) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);

        RefreshToolList();
        SetRightPanelEnabled(false);
    }

    // ── Layout ─────────────────────────────────────────────────────────────────

    void LayoutControls(int w, int h) {
        const int margin       = Scale(hwnd_, 12);
        const int gutter       = Scale(hwnd_, 8);
        const int label_h      = Scale(hwnd_, 18);
        const int button_h     = Scale(hwnd_, 28);
        const int button_w     = Scale(hwnd_, 80);
        const int edit_h       = Scale(hwnd_, 22);
        const int footer_y     = h - margin - button_h;
        const int left_w       = Scale(hwnd_, 200);
        const int right_x      = margin + left_w + gutter * 2;
        const int right_w      = w - right_x - margin;

        // Left panel
        int ly = margin;
        const int btn_half = (left_w - gutter) / 2;
        MoveWindow(add_tool_button_,    margin,                   ly, btn_half,  button_h, TRUE);
        MoveWindow(remove_tool_button_, margin + btn_half + gutter, ly, btn_half, button_h, TRUE);
        ly += button_h + gutter;
        MoveWindow(tool_list_, margin, ly, left_w, footer_y - ly - gutter, TRUE);

        // Right panel y cursor
        int ry = margin;

        // Name
        MoveWindow(name_label_, right_x, ry, right_w, label_h, TRUE);
        ry += label_h + gutter;
        MoveWindow(name_edit_, right_x, ry, right_w, edit_h, TRUE);
        ry += edit_h + gutter * 2;

        // Description (3 lines tall)
        MoveWindow(desc_label_, right_x, ry, right_w, label_h, TRUE);
        ry += label_h + gutter;
        const int desc_h = Scale(hwnd_, 56);
        MoveWindow(desc_edit_, right_x, ry, right_w, desc_h, TRUE);
        ry += desc_h + gutter * 2;

        // Model
        MoveWindow(model_label_, right_x, ry, right_w, label_h, TRUE);
        ry += label_h + gutter;
        MoveWindow(model_combo_, right_x, ry, right_w, Scale(hwnd_, 250), TRUE);
        ry += Scale(hwnd_, 28) + gutter * 2;

        // Compression
        MoveWindow(compression_label_, right_x, ry, right_w, label_h, TRUE);
        ry += label_h + gutter;
        MoveWindow(compression_combo_, right_x, ry, right_w, Scale(hwnd_, 200), TRUE);
        ry += Scale(hwnd_, 28) + gutter * 2;

        // MCP servers
        MoveWindow(mcp_header_, right_x, ry, right_w, label_h, TRUE);
        ry += label_h + gutter;
        const int mcp_list_h = Scale(hwnd_, 70);
        MoveWindow(mcp_list_, right_x, ry, right_w, mcp_list_h, TRUE);
        ry += mcp_list_h + gutter;

        // MCP server details panel (fixed height; variable controls are positioned inside by LayoutMcpVariableControls)
        const int mcp_details_h = Scale(hwnd_, 160);
        MoveWindow(mcp_details_panel_, right_x, ry, right_w, mcp_details_h, TRUE);
        const int mdx = right_x + Scale(hwnd_, 6);
        const int mdw = right_w - Scale(hwnd_, 12);
        MoveWindow(mcp_enabled_check_, mdx, ry + Scale(hwnd_, 18), mdw, Scale(hwnd_, 22), TRUE);
        MoveWindow(mcp_var_header_,    mdx, ry + Scale(hwnd_, 46), mdw, label_h,          TRUE);
        LayoutMcpVariableControls();
        ry += mcp_details_h + gutter;

        // RAG
        MoveWindow(rag_header_, right_x, ry, right_w, label_h, TRUE);
        ry += label_h + gutter;
        const int rag_list_h = Scale(hwnd_, 70);
        MoveWindow(rag_list_, right_x, ry, right_w, rag_list_h, TRUE);
        ry += rag_list_h + gutter;

        // RAG permission checkboxes row 1: Enabled Read Write Tool Write-file
        const int cbw  = Scale(hwnd_, 70);
        const int cbg  = Scale(hwnd_, 10);
        MoveWindow(rag_enabled_check_,  right_x,                   ry, cbw,             Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_read_check_,     right_x + (cbw+cbg),       ry, cbw,             Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_write_check_,    right_x + (cbw+cbg)*2,     ry, cbw,             Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_tool_check_,     right_x + (cbw+cbg)*3,     ry, cbw,             Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_export_check_,   right_x + (cbw+cbg)*4,     ry, Scale(hwnd_,85), Scale(hwnd_, 20), TRUE);
        ry += Scale(hwnd_, 24);

        // RAG permission checkboxes row 2: Delete Default-ingest Inject-on-start
        const int inj_w = Scale(hwnd_, 120);
        MoveWindow(rag_delete_check_,   right_x,               ry, cbw,   Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_default_ingest_, right_x + (cbw+cbg),   ry, Scale(hwnd_,120), Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_inject_onstart_, right_x + (cbw+cbg) + Scale(hwnd_,120) + cbg, ry, inj_w, Scale(hwnd_, 20), TRUE);
        ry += Scale(hwnd_, 24) + gutter;

        // Export path
        const int pl = Scale(hwnd_, 115);
        MoveWindow(rag_exportpath_lbl_,  right_x,      ry + Scale(hwnd_,3), pl,         label_h, TRUE);
        MoveWindow(rag_exportpath_edit_, right_x + pl, ry,                   right_w-pl, edit_h,  TRUE);
        ry += edit_h + gutter;

        // Numeric rows
        const int nl = Scale(hwnd_, 105);
        const int ne = Scale(hwnd_, 60);
        const int pp = nl + ne + gutter * 2;
        MoveWindow(rag_priority_lbl_,   right_x,       ry+Scale(hwnd_,3), nl, label_h, TRUE);
        MoveWindow(rag_priority_edit_,  right_x+nl,    ry,                ne, edit_h,  TRUE);
        MoveWindow(rag_maxchunks_lbl_,  right_x+pp,    ry+Scale(hwnd_,3), nl, label_h, TRUE);
        MoveWindow(rag_maxchunks_edit_, right_x+pp+nl, ry,                ne, edit_h,  TRUE);
        ry += edit_h + gutter;
        MoveWindow(rag_minconf_lbl_,    right_x,       ry+Scale(hwnd_,3), nl, label_h, TRUE);
        MoveWindow(rag_minconf_edit_,   right_x+nl,    ry,                ne, edit_h,  TRUE);
        MoveWindow(rag_maxconf_lbl_,    right_x+pp,    ry+Scale(hwnd_,3), nl, label_h, TRUE);
        MoveWindow(rag_maxconf_edit_,   right_x+pp+nl, ry,                ne, edit_h,  TRUE);
        ry += edit_h + gutter;
        MoveWindow(rag_retmode_lbl_,    right_x,       ry+Scale(hwnd_,3), nl,              label_h, TRUE);
        MoveWindow(rag_retmode_combo_,  right_x+nl,    ry,                Scale(hwnd_,180),Scale(hwnd_,200), TRUE);
        ry += edit_h + gutter * 2;

        // Instructions
        const int import_w = Scale(hwnd_, 130);
        MoveWindow(instructions_lbl_,       right_x,                        ry+Scale(hwnd_,4), right_w - import_w - gutter, label_h, TRUE);
        MoveWindow(import_markdown_button_,  right_x + right_w - import_w,  ry,                import_w,                     button_h, TRUE);
        ry += button_h + gutter;
        MoveWindow(instructions_edit_, right_x, ry, right_w, std::max(Scale(hwnd_, 60), footer_y - ry - gutter), TRUE);

        // Footer
        MoveWindow(close_button_, w - margin - button_w,              footer_y, button_w, button_h, TRUE);
        MoveWindow(save_button_,  w - margin - button_w*2 - gutter,   footer_y, button_w, button_h, TRUE);
        MoveWindow(test_button_,  w - margin - button_w*3 - gutter*2, footer_y, button_w, button_h, TRUE);
    }

    // ── Commands ───────────────────────────────────────────────────────────────

    void OnCommand(int id, int code) {
        switch (id) {
        case kToolList:
            if (code == LBN_SELCHANGE) OnToolSelectionChanged();
            break;
        case kAddTool:    AddTool();    break;
        case kRemoveTool: RemoveTool(); break;

        case kMcpList:
            if (code == LBN_SELCHANGE) SelectMcpRow(ListBox_GetCurSel(mcp_list_));
            break;
        case kMcpEnabledCheck:
            OnMcpEnabledChanged();
            break;

        case kRagList:
            if (code == LBN_SELCHANGE) OnRagSelectionChanged();
            break;
        case kRagEnabledCheck:
        case kRagReadCheck:
        case kRagWriteCheck:
        case kRagToolCheck:
        case kRagDeleteCheck:
        case kRagExportCheck:
        case kRagDefaultIngest:
        case kRagInjectOnStart:
        case kRagRetModeCombo:
            OnRagPermissionChanged();
            break;

        case kImportMarkdown: ImportMarkdown();   break;
        case kTestButton:     ShowTestWindow();   break;
        case kSaveButton:     SaveCurrentTool();  break;
        case kCloseButton:    OnClose();          break;
        default:
            if (id >= kMcpBrowseVarBase && id < kMcpBrowseVarBase + 100) {
                OnMcpBrowseVariable(id - kMcpBrowseVarBase);
            }
            break;
        }
    }

    void OnClose() {
        if (selected_tool_index_ >= 0 && !tools_.empty()) {
            if (MessageBoxW(hwnd_, L"Save the current tool before closing?", L"Model Tools",
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                SaveCurrentTool();
            }
        }
        DestroyWindow(hwnd_);
    }

    // ── Tool list management ───────────────────────────────────────────────────

    void RefreshToolList() {
        ListBox_ResetContent(tool_list_);
        for (const auto& tool : tools_) {
            ListBox_AddString(tool_list_, Utf8ToWide(tool.name.empty() ? "(unnamed)" : tool.name).c_str());
        }
        if (selected_tool_index_ >= 0 && selected_tool_index_ < static_cast<int>(tools_.size())) {
            ListBox_SetCurSel(tool_list_, selected_tool_index_);
        }
    }

    void OnToolSelectionChanged() {
        const int sel = ListBox_GetCurSel(tool_list_);
        if (sel == selected_tool_index_) return;
        if (selected_tool_index_ >= 0) CollectCurrentTool();
        selected_tool_index_ = (sel >= 0 && sel < static_cast<int>(tools_.size())) ? sel : -1;
        LoadToolIntoRightPanel(selected_tool_index_);
    }

    void AddTool() {
        if (selected_tool_index_ >= 0) CollectCurrentTool();
        ModelToolConfig tool;
        tool.id = MakeId("mt");
        tool.name = "New Tool";
        tools_.push_back(tool);
        selected_tool_index_ = static_cast<int>(tools_.size()) - 1;
        RefreshToolList();
        LoadToolIntoRightPanel(selected_tool_index_);
        SetFocus(name_edit_);
        Edit_SetSel(name_edit_, 0, -1);
    }

    void RemoveTool() {
        if (selected_tool_index_ < 0 || selected_tool_index_ >= static_cast<int>(tools_.size())) return;
        if (MessageBoxW(hwnd_, L"Remove this tool?", L"Model Tools", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        tools_.erase(tools_.begin() + selected_tool_index_);
        storage_->SaveModelTools(tools_);
        if (selected_tool_index_ >= static_cast<int>(tools_.size()))
            selected_tool_index_ = static_cast<int>(tools_.size()) - 1;
        RefreshToolList();
        LoadToolIntoRightPanel(selected_tool_index_);
    }

    // ── Load / collect current tool ────────────────────────────────────────────

    void LoadToolIntoRightPanel(int index) {
        if (index < 0 || index >= static_cast<int>(tools_.size())) {
            SetRightPanelEnabled(false);
            SetWindowTextW(name_edit_, L"");
            SetWindowTextW(desc_edit_, L"");
            SetWindowTextW(instructions_edit_, L"");
            PopulateModelCombo("", "");
            SetCompressionCombo("");
            PopulateMcpList({});
            SelectMcpRow(-1);
            PopulateRagList({});
            selected_rag_index_ = -1;
            LoadRagControls(-1);
            return;
        }

        SetRightPanelEnabled(true);
        const auto& tool = tools_[index];
        SetWindowTextW(name_edit_, Utf8ToWide(tool.name).c_str());
        SetWindowTextW(desc_edit_, Utf8ToWide(tool.description).c_str());
        SetWindowTextW(instructions_edit_, Utf8ToWide(tool.instructions).c_str());
        PopulateModelCombo(tool.preferred_provider_id, tool.preferred_model_id);
        SetCompressionCombo(tool.selected_compression_config_id);
        PopulateMcpList(tool.mcp_bindings);
        SelectMcpRow(-1);   // clear details panel; user clicks a server to inspect/edit it
        PopulateRagList(tool.rag_bindings);
        selected_rag_index_ = -1;
        LoadRagControls(-1);
    }

    void CollectCurrentTool() {
        if (selected_tool_index_ < 0 || selected_tool_index_ >= static_cast<int>(tools_.size())) return;
        SaveMcpVariableValues();   // flush any in-flight MCP variable edits
        SaveSelectedRagControls();

        auto& tool = tools_[selected_tool_index_];
        tool.name         = WideToUtf8(GetWindowTextString(name_edit_));
        tool.description  = WideToUtf8(GetWindowTextString(desc_edit_));
        tool.instructions = WideToUtf8(GetWindowTextString(instructions_edit_));

        // Model
        const int msel = ComboBox_GetCurSel(model_combo_);
        if (msel > 0 && static_cast<size_t>(msel) < model_entries_.size()) {
            tool.preferred_provider_id = model_entries_[msel].provider_id;
            tool.preferred_model_id    = model_entries_[msel].model_id;
        } else {
            tool.preferred_provider_id.clear();
            tool.preferred_model_id.clear();
        }

        // Compression
        {
            const int csel = ComboBox_GetCurSel(compression_combo_);
            if (csel <= 0 || csel - 1 >= static_cast<int>(compression_configs_.size())) {
                tool.selected_compression_config_id.clear();
            } else {
                tool.selected_compression_config_id = compression_configs_[csel - 1].id;
            }
        }

        // MCP bindings
        tool.mcp_bindings.clear();
        for (const auto& row : mcp_rows_) {
            if (row.selected) {
                ProjectMcpServerBinding b;
                b.server_id = row.server_id;
                b.variables = row.variables;
                tool.mcp_bindings.push_back(b);
            }
        }

        // RAG bindings
        tool.rag_bindings.clear();
        for (const auto& row : rag_rows_) {
            if (row.enabled) {
                ProjectRagBinding b;
                b.rag_id                = row.rag_id;
                b.enabled               = row.enabled;
                b.can_read              = row.can_read;
                b.can_write             = row.can_write;
                b.expose_as_tool        = row.expose_as_tool;
                b.can_delete            = row.can_delete;
                b.can_export            = row.can_export;
                b.export_path_template  = row.export_path_template;
                b.default_ingest_target = row.default_ingest_target;
                b.inject_on_start       = row.inject_on_start;
                b.retrieval_priority    = row.retrieval_priority;
                b.max_chunks            = row.max_chunks;
                b.default_min_confidence = row.default_min_confidence;
                b.default_max_confidence = row.default_max_confidence;
                b.retrieval_mode        = row.retrieval_mode;
                tool.rag_bindings.push_back(b);
            }
        }
    }

    void SaveCurrentTool() {
        if (selected_tool_index_ < 0) return;
        CollectCurrentTool();
        const auto name = tools_[selected_tool_index_].name;
        ListBox_DeleteString(tool_list_, selected_tool_index_);
        ListBox_InsertString(tool_list_, selected_tool_index_, Utf8ToWide(name.empty() ? "(unnamed)" : name).c_str());
        ListBox_SetCurSel(tool_list_, selected_tool_index_);
        storage_->SaveModelTools(tools_);
        MessageBoxW(hwnd_, L"Tool saved.", L"Model Tools", MB_OK | MB_ICONINFORMATION);
    }

    // ── Test window ────────────────────────────────────────────────────────────

    void ShowTestWindow() {
        if (selected_tool_index_ < 0) {
            MessageBoxW(hwnd_, L"Select a tool first.", L"Test", MB_OK | MB_ICONINFORMATION);
            return;
        }
        // Collect current edits into memory (don't save to disk)
        CollectCurrentTool();
        const auto& tool = tools_[selected_tool_index_];
        ToolTestWindow::Show(hwnd_, tool, available_servers_, available_rags_);
    }

    // ── Model combo ────────────────────────────────────────────────────────────

    void PopulateModelCombo(const std::string& pref_provider, const std::string& pref_model) {
        ComboBox_ResetContent(model_combo_);
        model_entries_.clear();
        ComboBox_AddString(model_combo_, L"(No preference — use first available)");
        model_entries_.push_back({"", ""});

        int preferred = 0;
        for (const auto& prov : providers_) {
            for (const auto& model : prov.models) {
                std::wstring label = Utf8ToWide(prov.name + " / " + model.display_name);
                if (model.context_window > 0) label += L"  " + std::to_wstring(model.context_window) + L" ctx";
                if (model.supports_streaming) label += L"  [stream]";
                if (model.supports_tools)     label += L" [tools]";
                if (model.supports_vision)    label += L" [vision]";
                if (model.supports_embedding) label += L" [embed]";
                if (model.supports_thinking)  label += L" [think]";
                if (prov.id == pref_provider && model.id == pref_model)
                    preferred = static_cast<int>(model_entries_.size());
                model_entries_.push_back({prov.id, model.id});
                ComboBox_AddString(model_combo_, label.c_str());
            }
        }
        ComboBox_SetCurSel(model_combo_, preferred);
        EnableWindow(model_combo_, model_entries_.size() > 1);
    }

    // ── Compression combo ──────────────────────────────────────────────────────

    void SetCompressionCombo(const std::string& config_id) {
        int sel = 0;  // default: None
        for (int i = 0; i < static_cast<int>(compression_configs_.size()); ++i) {
            if (compression_configs_[i].id == config_id) {
                sel = i + 1;
                break;
            }
        }
        ComboBox_SetCurSel(compression_combo_, sel);
    }

    // ── MCP list ───────────────────────────────────────────────────────────────

    void PopulateMcpList(const std::vector<ProjectMcpServerBinding>& bindings) {
        mcp_rows_.clear();
        ListBox_ResetContent(mcp_list_);
        for (const auto& server : available_servers_) {
            McpServerRow row;
            row.server_id   = server.id;
            row.server_name = server.name;
            const auto it = std::find_if(bindings.begin(), bindings.end(),
                [&](const ProjectMcpServerBinding& b){ return b.server_id == server.id; });
            if (it != bindings.end()) {
                row.selected  = true;
                row.variables = it->variables;
            }
            mcp_rows_.push_back(row);
        }
        RefreshMcpList();
    }

    void RefreshMcpList() {
        const int cur = ListBox_GetCurSel(mcp_list_);
        ListBox_ResetContent(mcp_list_);
        for (const auto& row : mcp_rows_) {
            std::wstring label = (row.selected ? L"[✓] " : L"[ ] ");
            label += Utf8ToWide(row.server_name);
            ListBox_AddString(mcp_list_, label.c_str());
        }
        if (cur >= 0 && cur < static_cast<int>(mcp_rows_.size()))
            ListBox_SetCurSel(mcp_list_, cur);
    }

    // Save any edited variable values for the currently-displayed MCP row back into mcp_rows_.
    void SaveMcpVariableValues() {
        if (selected_mcp_index_ < 0 || selected_mcp_index_ >= static_cast<int>(mcp_rows_.size())) return;
        auto& row = mcp_rows_[selected_mcp_index_];
        for (const auto& vc : mcp_var_controls_) {
            const std::string value = WideToUtf8(GetWindowTextString(vc.edit));
            auto it = std::find_if(row.variables.begin(), row.variables.end(),
                [&](const ProjectMcpVariableValue& v) { return v.name == vc.name; });
            if (it != row.variables.end()) it->value = value;
            else row.variables.push_back({vc.name, value});
        }
    }

    // Select and display the details for an MCP server row (save old edits first).
    void SelectMcpRow(int index) {
        SaveMcpVariableValues();
        selected_mcp_index_ = index;

        // Destroy old dynamic variable controls.
        for (auto& vc : mcp_var_controls_) {
            if (vc.label)  DestroyWindow(vc.label);
            if (vc.edit)   DestroyWindow(vc.edit);
            if (vc.browse) DestroyWindow(vc.browse);
        }
        mcp_var_controls_.clear();

        const bool valid = (index >= 0 && index < static_cast<int>(mcp_rows_.size()));
        ShowWindow(mcp_details_panel_, valid ? SW_SHOW : SW_HIDE);
        ShowWindow(mcp_enabled_check_, valid ? SW_SHOW : SW_HIDE);
        ShowWindow(mcp_var_header_,    valid ? SW_SHOW : SW_HIDE);

        if (!valid) return;

        const auto& row = mcp_rows_[index];
        Button_SetCheck(mcp_enabled_check_, row.selected ? BST_CHECKED : BST_UNCHECKED);

        // Find the server config to get variable definitions.
        const McpServerConfig* server_cfg = nullptr;
        for (const auto& s : available_servers_) {
            if (s.id == row.server_id) { server_cfg = &s; break; }
        }

        if (!server_cfg || server_cfg->variables.empty()) {
            SetWindowTextW(mcp_var_header_, L"(no variables)");
            return;
        }

        SetWindowTextW(mcp_var_header_, L"Variables:");

        int var_index = 0;
        for (const auto& variable : server_cfg->variables) {
            HWND lbl = CreateWindowExW(0, L"STATIC", Utf8ToWide(variable.name).c_str(),
                WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd_, nullptr, nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

            HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hwnd_, nullptr, nullptr, nullptr);
            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

            // Load saved value.
            auto it = std::find_if(row.variables.begin(), row.variables.end(),
                [&](const ProjectMcpVariableValue& v) { return v.name == variable.name; });
            if (it != row.variables.end()) SetWindowTextW(edit, Utf8ToWide(it->value).c_str());

            HWND browse = nullptr;
            if (variable.kind != McpVariableKind::None) {
                const int btn_id = kMcpBrowseVarBase + var_index;
                browse = CreateWindowExW(0, L"BUTTON", L"...",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hwnd_,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(btn_id)), nullptr, nullptr);
                SendMessageW(browse, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            }

            mcp_var_controls_.push_back({variable.name, variable.kind, lbl, edit, browse});
            ++var_index;
        }

        LayoutMcpVariableControls();
    }

    // Position the dynamic MCP variable controls relative to the mcp_var_header_ label.
    void LayoutMcpVariableControls() {
        if (mcp_var_controls_.empty()) return;
        RECT hdr_rc;
        GetWindowRect(mcp_var_header_, &hdr_rc);
        POINT pt = {hdr_rc.left, hdr_rc.top};
        ScreenToClient(hwnd_, &pt);
        // Variables start just below the "Variables:" label.
        const int base_x = pt.x;
        int base_y = pt.y + Scale(hwnd_, 20);
        const int w = hdr_rc.right - hdr_rc.left;

        for (const auto& vc : mcp_var_controls_) {
            const bool has_browse = (vc.browse != nullptr);
            const int edit_w = w - (has_browse ? Scale(hwnd_, 32) : 0);
            if (vc.label)  MoveWindow(vc.label,  base_x, base_y,                               w,                Scale(hwnd_, 16), TRUE);
            if (vc.edit)   MoveWindow(vc.edit,   base_x, base_y + Scale(hwnd_, 18),            edit_w,           Scale(hwnd_, 22), TRUE);
            if (vc.browse) MoveWindow(vc.browse, base_x + edit_w + Scale(hwnd_, 2),
                                                 base_y + Scale(hwnd_, 18), Scale(hwnd_, 28),  Scale(hwnd_, 22), TRUE);
            base_y += Scale(hwnd_, 44);
        }
    }

    // Called when the "Use this MCP server" checkbox changes in the details panel.
    void OnMcpEnabledChanged() {
        if (selected_mcp_index_ < 0 || selected_mcp_index_ >= static_cast<int>(mcp_rows_.size())) return;
        mcp_rows_[selected_mcp_index_].selected =
            (Button_GetCheck(mcp_enabled_check_) == BST_CHECKED);
        RefreshMcpList();
        ListBox_SetCurSel(mcp_list_, selected_mcp_index_);
    }

    // Called when a browse button ("...") for an MCP variable is clicked.
    void OnMcpBrowseVariable(int var_index) {
        if (var_index < 0 || var_index >= static_cast<int>(mcp_var_controls_.size())) return;
        auto& vc = mcp_var_controls_[var_index];
        if (vc.kind == McpVariableKind::Folder) {
            BROWSEINFOW bi{};
            bi.hwndOwner = hwnd_;
            bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            bi.lpszTitle = L"Select Folder";
            PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH]{};
                if (SHGetPathFromIDListW(pidl, path)) SetWindowTextW(vc.edit, path);
                CoTaskMemFree(pidl);
            }
        } else if (vc.kind == McpVariableKind::File) {
            wchar_t path[MAX_PATH]{};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd_;
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = static_cast<DWORD>(std::size(path));
            ofn.lpstrFilter = L"All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn)) SetWindowTextW(vc.edit, path);
        }
    }

    // ── RAG list ───────────────────────────────────────────────────────────────

    void PopulateRagList(const std::vector<ProjectRagBinding>& bindings) {
        rag_rows_.clear();
        ListBox_ResetContent(rag_list_);
        for (const auto& lib : available_rags_) {
            RagRow row;
            row.rag_id   = lib.id;
            row.rag_name = lib.name;
            row.max_chunks = lib.default_max_chunks;
            const auto it = std::find_if(bindings.begin(), bindings.end(),
                [&](const ProjectRagBinding& b){ return b.rag_id == lib.id; });
            if (it != bindings.end()) {
                row.enabled             = it->enabled;
                row.can_read            = it->can_read;
                row.can_write           = it->can_write;
                row.expose_as_tool      = it->expose_as_tool;
                row.can_delete          = it->can_delete;
                row.can_export          = it->can_export;
                row.export_path_template = it->export_path_template;
                row.default_ingest_target = it->default_ingest_target;
                row.inject_on_start     = it->inject_on_start;
                row.retrieval_priority  = it->retrieval_priority;
                row.max_chunks          = it->max_chunks;
                row.default_min_confidence = it->default_min_confidence;
                row.default_max_confidence = it->default_max_confidence;
                row.retrieval_mode      = it->retrieval_mode;
            }
            rag_rows_.push_back(row);
        }
        RefreshRagList();
    }

    void RefreshRagList() {
        const int cur = ListBox_GetCurSel(rag_list_);
        ListBox_ResetContent(rag_list_);
        for (const auto& row : rag_rows_) {
            std::wstring text = Utf8ToWide(row.rag_name);
            text += L" [";
            if (row.enabled)             text += L"E";
            if (row.can_read)            text += L"R";
            if (row.can_write)           text += L"W";
            if (row.expose_as_tool)      text += L"T";
            if (row.can_delete)          text += L"D";
            if (row.can_export)          text += L"X";
            if (row.default_ingest_target) text += L"I";
            if (row.inject_on_start)     text += L"⚡";
            text += L"]";
            if (row.enabled) {
                text += L" P:" + std::to_wstring(row.retrieval_priority);
                text += L" C:" + std::to_wstring(row.max_chunks);
                text += L" " + FormatConfidence(row.default_min_confidence) + L"-" + FormatConfidence(row.default_max_confidence);
            }
            ListBox_AddString(rag_list_, text.c_str());
        }
        if (cur >= 0 && cur < static_cast<int>(rag_rows_.size()))
            ListBox_SetCurSel(rag_list_, cur);
    }

    void OnRagSelectionChanged() {
        const int sel = ListBox_GetCurSel(rag_list_);
        SaveSelectedRagControls();
        RefreshRagList();
        if (sel >= 0 && sel < static_cast<int>(rag_rows_.size()))
            ListBox_SetCurSel(rag_list_, sel);
        selected_rag_index_ = (sel >= 0 && sel < static_cast<int>(rag_rows_.size())) ? sel : -1;
        LoadRagControls(selected_rag_index_);
    }

    void OnRagPermissionChanged() {
        SaveSelectedRagControls();
        const int sel = selected_rag_index_;
        RefreshRagList();
        if (sel >= 0 && sel < static_cast<int>(rag_rows_.size()))
            ListBox_SetCurSel(rag_list_, sel);
        LoadRagControls(sel);
    }

    void SetRagControlsEnabled(bool on) {
        HWND ctrls[] = {
            rag_enabled_check_, rag_read_check_, rag_write_check_, rag_tool_check_,
            rag_delete_check_, rag_export_check_, rag_default_ingest_, rag_inject_onstart_,
            rag_priority_edit_, rag_maxchunks_edit_, rag_minconf_edit_, rag_maxconf_edit_,
            rag_exportpath_edit_, rag_retmode_combo_,
        };
        for (HWND c : ctrls) EnableWindow(c, on);
    }

    void LoadRagControls(int index) {
        const bool ok = index >= 0 && index < static_cast<int>(rag_rows_.size());
        SetRagControlsEnabled(ok);
        if (!ok) {
            Button_SetCheck(rag_enabled_check_,  BST_UNCHECKED);
            Button_SetCheck(rag_read_check_,     BST_UNCHECKED);
            Button_SetCheck(rag_write_check_,    BST_UNCHECKED);
            Button_SetCheck(rag_tool_check_,     BST_UNCHECKED);
            Button_SetCheck(rag_delete_check_,   BST_UNCHECKED);
            Button_SetCheck(rag_export_check_,   BST_UNCHECKED);
            Button_SetCheck(rag_default_ingest_, BST_UNCHECKED);
            Button_SetCheck(rag_inject_onstart_, BST_UNCHECKED);
            SetWindowTextW(rag_priority_edit_,    L"");
            SetWindowTextW(rag_maxchunks_edit_,   L"");
            SetWindowTextW(rag_minconf_edit_,     L"");
            SetWindowTextW(rag_maxconf_edit_,     L"");
            SetWindowTextW(rag_exportpath_edit_,  L"");
            ComboBox_SetCurSel(rag_retmode_combo_, 0);
            return;
        }
        auto& row = rag_rows_[index];
        NormalizeRagRow(row);
        Button_SetCheck(rag_enabled_check_,  row.enabled             ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_read_check_,     row.can_read            ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_write_check_,    row.can_write           ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_tool_check_,     row.expose_as_tool      ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_delete_check_,   row.can_delete          ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_export_check_,   row.can_export          ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_default_ingest_, row.default_ingest_target ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_inject_onstart_, row.inject_on_start     ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(rag_exportpath_edit_, row.can_export);

        EnableWindow(rag_inject_onstart_, FALSE);

        SetWindowTextW(rag_priority_edit_,   std::to_wstring(row.retrieval_priority).c_str());
        SetWindowTextW(rag_maxchunks_edit_,  std::to_wstring(row.max_chunks).c_str());
        SetWindowTextW(rag_minconf_edit_,    FormatConfidence(row.default_min_confidence).c_str());
        SetWindowTextW(rag_maxconf_edit_,    FormatConfidence(row.default_max_confidence).c_str());
        SetWindowTextW(rag_exportpath_edit_, Utf8ToWide(row.export_path_template).c_str());
        ComboBox_SetCurSel(rag_retmode_combo_, RagModeToComboIndex(row.retrieval_mode));
    }

    void SaveSelectedRagControls() {
        if (selected_rag_index_ < 0 || selected_rag_index_ >= static_cast<int>(rag_rows_.size())) return;
        auto& row = rag_rows_[selected_rag_index_];
        row.enabled             = Button_GetCheck(rag_enabled_check_)  == BST_CHECKED;
        row.can_read            = Button_GetCheck(rag_read_check_)     == BST_CHECKED;
        row.can_write           = Button_GetCheck(rag_write_check_)    == BST_CHECKED;
        row.expose_as_tool      = Button_GetCheck(rag_tool_check_)     == BST_CHECKED;
        row.can_delete          = Button_GetCheck(rag_delete_check_)   == BST_CHECKED;
        row.can_export          = Button_GetCheck(rag_export_check_)   == BST_CHECKED;
        row.export_path_template = WideToUtf8(GetWindowTextString(rag_exportpath_edit_));
        row.default_ingest_target = Button_GetCheck(rag_default_ingest_) == BST_CHECKED;
        row.inject_on_start     = Button_GetCheck(rag_inject_onstart_) == BST_CHECKED;
        if (const auto v = ParseInt(GetWindowTextString(rag_priority_edit_)))   row.retrieval_priority = *v;
        if (const auto v = ParseInt(GetWindowTextString(rag_maxchunks_edit_)))  row.max_chunks = *v;
        if (const auto v = ParseDouble(GetWindowTextString(rag_minconf_edit_))) row.default_min_confidence = *v;
        if (const auto v = ParseDouble(GetWindowTextString(rag_maxconf_edit_))) row.default_max_confidence = *v;
        row.retrieval_mode = ComboIndexToRagMode(ComboBox_GetCurSel(rag_retmode_combo_));

        NormalizeRagRow(row);

        if (row.default_ingest_target) {
            for (int i = 0; i < static_cast<int>(rag_rows_.size()); ++i) {
                if (i != selected_rag_index_) rag_rows_[i].default_ingest_target = false;
            }
        }
    }

    // ── Instructions import ────────────────────────────────────────────────────

    void ImportMarkdown() {
        const auto path = PickMarkdownFile(hwnd_);
        if (!path) return;
        try {
            SetWindowTextW(instructions_edit_, Utf8ToWide(ReadWholeFile(*path)).c_str());
        } catch (const std::exception& ex) {
            MessageBoxW(hwnd_, Utf8ToWide(std::string("Failed to import: ") + ex.what()).c_str(),
                        L"Import Failed", MB_OK | MB_ICONERROR);
        }
    }

    // ── Enable/disable right panel ─────────────────────────────────────────────

    void SetRightPanelEnabled(bool on) {
        HWND ctrls[] = {
            name_edit_, desc_edit_, model_combo_, compression_combo_,
            mcp_list_, mcp_enabled_check_,
            rag_list_,
            rag_enabled_check_, rag_read_check_, rag_write_check_, rag_tool_check_,
            rag_delete_check_, rag_export_check_, rag_default_ingest_, rag_inject_onstart_,
            rag_priority_edit_, rag_maxchunks_edit_, rag_minconf_edit_, rag_maxconf_edit_,
            rag_exportpath_edit_, rag_retmode_combo_,
            instructions_edit_, import_markdown_button_, save_button_, test_button_,
        };
        for (HWND c : ctrls) EnableWindow(c, on);
    }

    // ──────────────────────────────────────────────────────────────────────────
    // Members
    // ──────────────────────────────────────────────────────────────────────────

    HWND  owner_  = nullptr;
    HWND  hwnd_   = nullptr;
    HFONT font_   = nullptr;

    AppStorage*                        storage_      = nullptr;
    std::vector<ProviderConfig>        providers_;
    McpManager*                        mcp_manager_  = nullptr;
    RagService*                        rag_service_  = nullptr;
    std::vector<ContextCompressionConfig> compression_configs_;

    std::vector<ModelToolConfig>   tools_;
    std::vector<McpServerConfig>   available_servers_;
    std::vector<RagLibraryConfig>  available_rags_;

    int selected_tool_index_ = -1;
    int selected_rag_index_  = -1;
    int selected_mcp_index_  = -1;

    std::vector<McpServerRow>   mcp_rows_;
    std::vector<RagRow>         rag_rows_;
    std::vector<McpVarControl>  mcp_var_controls_;

    struct ModelEntry { std::string provider_id, model_id; };
    std::vector<ModelEntry> model_entries_;

    // Controls – left panel
    HWND tool_list_          = nullptr;
    HWND add_tool_button_    = nullptr;
    HWND remove_tool_button_ = nullptr;

    // Controls – right panel header
    HWND name_label_   = nullptr;
    HWND name_edit_    = nullptr;
    HWND desc_label_   = nullptr;
    HWND desc_edit_    = nullptr;
    HWND model_label_  = nullptr;
    HWND model_combo_  = nullptr;

    // Controls – compression
    HWND compression_label_ = nullptr;
    HWND compression_combo_ = nullptr;

    // Controls – MCP
    HWND mcp_header_        = nullptr;
    HWND mcp_list_          = nullptr;
    HWND mcp_details_panel_ = nullptr;  // groupbox below list
    HWND mcp_enabled_check_ = nullptr;  // "Use this MCP server"
    HWND mcp_var_header_    = nullptr;  // "Variables:" label

    // Controls – RAG
    HWND rag_header_         = nullptr;
    HWND rag_list_           = nullptr;
    HWND rag_enabled_check_  = nullptr;
    HWND rag_read_check_     = nullptr;
    HWND rag_write_check_    = nullptr;
    HWND rag_tool_check_     = nullptr;
    HWND rag_delete_check_   = nullptr;
    HWND rag_export_check_   = nullptr;
    HWND rag_default_ingest_ = nullptr;
    HWND rag_inject_onstart_ = nullptr;
    HWND rag_priority_lbl_   = nullptr;
    HWND rag_priority_edit_  = nullptr;
    HWND rag_maxchunks_lbl_  = nullptr;
    HWND rag_maxchunks_edit_ = nullptr;
    HWND rag_minconf_lbl_    = nullptr;
    HWND rag_minconf_edit_   = nullptr;
    HWND rag_maxconf_lbl_    = nullptr;
    HWND rag_maxconf_edit_   = nullptr;
    HWND rag_exportpath_lbl_ = nullptr;
    HWND rag_exportpath_edit_= nullptr;
    HWND rag_retmode_lbl_    = nullptr;
    HWND rag_retmode_combo_  = nullptr;

    // Controls – instructions
    HWND instructions_lbl_       = nullptr;
    HWND import_markdown_button_ = nullptr;
    HWND instructions_edit_      = nullptr;

    // Controls – footer
    HWND test_button_  = nullptr;
    HWND save_button_  = nullptr;
    HWND close_button_ = nullptr;
};

} // namespace

// ──────────────────────────────────────────────────────────────────────────────
// Public factory function
// ──────────────────────────────────────────────────────────────────────────────

HWND OpenModelToolsManager(
    HWND owner,
    AppStorage* storage,
    const std::vector<ProviderConfig>& providers,
    McpManager* mcp_manager,
    RagService* rag_service)
{
    auto* mgr = new ModelToolsManager(owner, storage, providers, mcp_manager, rag_service);
    HWND hwnd = mgr->Create();
    if (!hwnd) {
        delete mgr;
        return nullptr;
    }
    return hwnd;
}
