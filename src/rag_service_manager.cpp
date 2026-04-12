#include "rag_service_manager.h"

#include "prompt_dialog.h"
#include "util.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr wchar_t kRagManagerClassName[] = L"AgentRagServiceManagerWindow";
constexpr wchar_t kRagLibraryEditorClassName[] = L"AgentRagLibraryEditorWindow";
constexpr UINT kIngestionFinishedMessage = WM_APP + 70;
constexpr UINT kRebuildProgressMessage = WM_APP + 71;

enum ControlId : int {
    kLibrariesList = 6101,
    kDetailsEdit = 6102,
    kAddLibrary = 6103,
    kEditLibrary = 6104,
    kRemoveLibrary = 6105,
    kAttachReadOnly = 6106,
    kAttachReadWrite = 6107,
    kDetachProject = 6108,
    kIngestFiles = 6109,
    kSearchEdit = 6110,
    kSearchButton = 6111,
    kResultsEdit = 6112,
    kStatusLabel = 6113,
    kIngestFolder = 6114,
    kRebuildLibrary = 6115,
    kProgressBar = 6116,
    kBrowseDocuments = 6117,
    kReindexDocument = 6118,
    kDeleteDocument = 6119,
    kInstallTools = 6120,
    kCloseButton = IDCANCEL,
};

enum LibraryEditorControlId : int {
    kEditorNameLabel = 6201,
    kEditorNameEdit = 6202,
    kEditorDescriptionLabel = 6203,
    kEditorDescriptionEdit = 6204,
    kEditorStorageLabel = 6205,
    kEditorStorageEdit = 6206,
    kEditorBrowseStorage = 6207,
    kEditorChunkSizeLabel = 6208,
    kEditorChunkSizeEdit = 6209,
    kEditorOverlapLabel = 6210,
    kEditorOverlapEdit = 6211,
    kEditorMaxChunksLabel = 6212,
    kEditorMaxChunksEdit = 6213,
    kEditorEnabled = 6214,
    kEditorMaxFileSizeLabel = 6215,
    kEditorMaxFileSizeEdit = 6216,
    kEditorEmbeddingProviderLabel = 6217,
    kEditorEmbeddingProviderEdit = 6218,
    kEditorEmbeddingModelLabel = 6219,
    kEditorEmbeddingModelEdit = 6220,
    kEditorEmbeddingDimensionsLabel = 6221,
    kEditorEmbeddingDimensionsEdit = 6222,
    kEditorVectorBackendLabel = 6223,
    kEditorVectorBackendEdit = 6224,
    kEditorEmbeddingBaseUrlLabel = 6225,
    kEditorEmbeddingBaseUrlEdit = 6226,
    kEditorSegmentEnabled = 6227,
    kEditorSegmentThresholdLabel = 6228,
    kEditorSegmentThresholdEdit = 6229,
    kEditorSegmentSizeLabel = 6230,
    kEditorSegmentSizeEdit = 6231,
    kEditorSegmentOverlapLabel = 6232,
    kEditorSegmentOverlapEdit = 6233,
    kEditorRuntimeStatus = 6234,
    kEditorRuntimeCheck = 6235,
    kEditorRuntimeStart = 6236,
    kEditorRuntimeStop = 6237,
    kEditorRuntimeInstall = 6238,
    kEditorRuntimeLogLabel = 6239,
    kEditorRuntimeLog = 6240,
    kEditorRuntimeInstallModel = 6241,
    kEditorRuntimeTestEmbedding = 6242,
    kEditorSaveButton = IDOK,
    kEditorCancelButton = IDCANCEL,
};

struct IngestionPayload {
    RagIngestionResult result;
    bool rebuild = false;
};

struct ProgressPayload {
    RagProgressUpdate progress;
};

int Scale(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

std::wstring StorageModeLabel(RagDocumentStorageMode mode) {
    switch (mode) {
    case RagDocumentStorageMode::CopyIntoRagStore:
        return L"Copy into RAG store";
    case RagDocumentStorageMode::ReferenceInPlace:
        return L"Reference in place";
    case RagDocumentStorageMode::CopyAndTrackOriginal:
    default:
        return L"Copy and track original";
    }
}

std::optional<ProjectRagBinding> FindBinding(const std::vector<ProjectRagBinding>& bindings, const std::string& rag_id) {
    const auto it = std::find_if(bindings.begin(), bindings.end(), [&](const ProjectRagBinding& binding) {
        return binding.rag_id == rag_id;
    });
    if (it == bindings.end()) {
        return std::nullopt;
    }
    return *it;
}

std::wstring IngestionSummary(const RagIngestionResult& result, bool rebuild) {
    std::wstring text;
    text += rebuild ? L"Documents rebuilt: " : L"Files ingested: ";
    text += std::to_wstring(result.files_ingested) + L"\r\n";
    text += rebuild ? L"Documents skipped: " : L"Files skipped: ";
    text += std::to_wstring(result.files_skipped) + L"\r\n";
    text += L"Chunks added: " + std::to_wstring(result.chunks_added) + L"\r\n";
    if (!result.errors.empty()) {
        text += L"\r\nErrors:\r\n";
        for (const auto& error : result.errors) {
            text += L"- " + Utf8ToWide(error) + L"\r\n";
        }
    }
    return text;
}

std::wstring DocumentBrowserText(const RagLibraryConfig& library, const std::vector<RagDocumentSummary>& documents) {
    std::wstring text;
    text += L"Documents in RAG: " + Utf8ToWide(library.name) + L"\r\n";
    text += L"Count: " + std::to_wstring(documents.size()) + L"\r\n\r\n";
    if (documents.empty()) {
        text += L"No documents have been ingested into this RAG library yet.";
        return text;
    }

    for (size_t i = 0; i < documents.size(); ++i) {
        const auto& summary = documents[i];
        const auto& document = summary.document;
        text += L"Document " + std::to_wstring(i + 1) + L"\r\n";
        text += L"Name: " + Utf8ToWide(document.display_name) + L"\r\n";
        text += L"ID: " + Utf8ToWide(document.id) + L"\r\n";
        text += L"Chunks: " + std::to_wstring(summary.chunk_count) + L"\r\n";
        text += L"Embeddings: " + std::to_wstring(summary.embedding_count) + L"\r\n";
        text += L"Size: " + std::to_wstring(document.file_size) + L" bytes\r\n";
        if (!document.mime_type.empty()) {
            text += L"MIME/type: " + Utf8ToWide(document.mime_type) + L"\r\n";
        }
        if (!document.imported_at.empty()) {
            text += L"Imported: " + Utf8ToWide(document.imported_at) + L"\r\n";
        }
        if (!document.last_indexed_at.empty()) {
            text += L"Last indexed: " + Utf8ToWide(document.last_indexed_at) + L"\r\n";
        }
        if (!document.original_source_uri.empty()) {
            text += L"Original source: " + Utf8ToWide(document.original_source_uri) + L"\r\n";
        }
        if (!document.stored_relative_path.empty()) {
            text += L"Stored copy: " + Utf8ToWide(document.stored_relative_path) + L"\r\n";
        }
        if (!document.extracted_relative_path.empty()) {
            text += L"Extracted text: " + Utf8ToWide(document.extracted_relative_path) + L"\r\n";
        }
        if (!document.metadata_json.empty()) {
            text += L"Metadata: " + Utf8ToWide(document.metadata_json) + L"\r\n";
        }
        text += L"\r\n";
    }
    return text;
}

std::wstring ImportPreviewText(const RagImportPreview& preview) {
    std::wstring text;
    text += L"Import Preview\r\n";
    text += L"Total files scanned: " + std::to_wstring(preview.total_files) + L"\r\n";
    text += L"Ready to ingest: " + std::to_wstring(preview.supported_files) + L"\r\n";
    text += L"Will be skipped: " + std::to_wstring(preview.skipped_files) + L"\r\n";
    text += L"Ready bytes: " + std::to_wstring(preview.supported_bytes) + L"\r\n";
    if (!preview.errors.empty()) {
        text += L"\r\nPreview errors:\r\n";
        for (const auto& error : preview.errors) {
            text += L"- " + Utf8ToWide(error) + L"\r\n";
        }
    }

    text += L"\r\nFiles:\r\n";
    const size_t visible_count = std::min<size_t>(preview.items.size(), 200);
    for (size_t i = 0; i < visible_count; ++i) {
        const auto& item = preview.items[i];
        text += item.supported ? L"[ingest] " : L"[skip] ";
        text += Utf8ToWide(item.display_name.empty() ? item.source_path : item.display_name);
        text += L" (" + std::to_wstring(item.file_size) + L" bytes)";
        if (!item.reason.empty()) {
            text += L" - " + Utf8ToWide(item.reason);
        }
        if (!item.source_path.empty()) {
            text += L"\r\n  " + Utf8ToWide(item.source_path);
        }
        text += L"\r\n";
    }
    if (preview.items.size() > visible_count) {
        text += L"\r\n... " + std::to_wstring(preview.items.size() - visible_count) + L" more files not shown in preview.\r\n";
    }
    return text;
}

std::wstring ExtractionToolsText(const std::vector<RagExtractionToolStatus>& tools) {
    std::wstring text;
    text += L"RAG Extraction Tools\r\n";
    text += L"These tools improve rich document ingestion. Missing tools marked recommended can be installed by this screen.\r\n\r\n";

    int missing_recommended = 0;
    int missing_installable = 0;
    for (const auto& tool : tools) {
        if (!tool.installed && tool.recommended) {
            ++missing_recommended;
        }
        if (!tool.installed && tool.installable) {
            ++missing_installable;
        }
    }
    text += L"Missing recommended tools: " + std::to_wstring(missing_recommended) + L"\r\n";
    text += L"Missing installable tools: " + std::to_wstring(missing_installable) + L"\r\n\r\n";

    for (const auto& tool : tools) {
        text += tool.installed ? L"[installed] " : L"[missing] ";
        text += Utf8ToWide(tool.name);
        if (tool.recommended) {
            text += L" (recommended)";
        }
        text += L"\r\n";
        text += L"Executable: " + Utf8ToWide(tool.executable) + L"\r\n";
        text += L"Purpose: " + Utf8ToWide(tool.purpose) + L"\r\n";
        if (!tool.notes.empty()) {
            text += L"Notes: " + Utf8ToWide(tool.notes) + L"\r\n";
        }
        if (!tool.install_command.empty()) {
            text += L"Install command: " + Utf8ToWide(tool.install_command) + L"\r\n";
        }
        text += L"\r\n";
    }
    return text;
}

std::vector<std::filesystem::path> PickFiles(HWND owner) {
    std::vector<std::filesystem::path> files;
    std::vector<wchar_t> buffer(32768, L'\0');

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrTitle = L"Select files to ingest";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT;
    dialog.lpstrFilter = L"RAG Supported Files\0*.txt;*.md;*.json;*.csv;*.log;*.xml;*.cpp;*.c;*.h;*.hpp;*.cs;*.js;*.ts;*.tsx;*.jsx;*.py;*.ps1;*.bat;*.cmd;*.ini;*.toml;*.yaml;*.yml;*.html;*.htm;*.docx;*.docm;*.xlsx;*.xlsm;*.pdf;*.css;*.sql\0All Files\0*.*\0";

    if (!GetOpenFileNameW(&dialog)) {
        return files;
    }

    const std::wstring first = buffer.data();
    const wchar_t* cursor = buffer.data() + first.size() + 1;
    if (*cursor == L'\0') {
        files.emplace_back(first);
        return files;
    }

    const std::filesystem::path directory(first);
    while (*cursor != L'\0') {
        std::wstring filename = cursor;
        files.push_back(directory / filename);
        cursor += filename.size() + 1;
    }
    return files;
}

std::optional<std::filesystem::path> PickFolder(HWND owner, const std::wstring& title) {
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.lpszTitle = title.c_str();
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

    LPITEMIDLIST selection = SHBrowseForFolderW(&info);
    if (!selection) {
        return std::nullopt;
    }

    wchar_t path[MAX_PATH] = {};
    std::optional<std::filesystem::path> result;
    if (SHGetPathFromIDListW(selection, path) && path[0] != L'\0') {
        result = std::filesystem::path(path);
    }
    CoTaskMemFree(selection);
    return result;
}

std::string NormalizeEmbeddingProvider(std::string provider) {
    provider = Trim(provider);
    std::transform(provider.begin(), provider.end(), provider.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (provider == "lm studio" || provider == "lm-studio" || provider == "lm_studio") {
        return "lmstudio";
    }
    if (provider == "ollama") {
        return "ollama";
    }
    return "none";
}

int ProviderComboIndex(const std::string& provider) {
    const std::string normalized = NormalizeEmbeddingProvider(provider);
    if (normalized == "ollama") {
        return 1;
    }
    if (normalized == "lmstudio") {
        return 2;
    }
    return 0;
}

std::string ProviderFromComboIndex(int index) {
    switch (index) {
    case 1:
        return "ollama";
    case 2:
        return "lmstudio";
    case 0:
    default:
        return "none";
    }
}

class RagLibraryEditorDialog {
public:
    static std::optional<RagLibraryConfig> Show(HWND owner, RagService* rag_service, const RagLibraryConfig& initial, bool editing);

private:
    RagLibraryEditorDialog(HWND owner, RagService* rag_service, RagLibraryConfig initial, bool editing)
        : owner_(owner), rag_service_(rag_service), config_(std::move(initial)), editing_(editing) {}

    static void RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void OnCreate();
    void LayoutControls() const;
    void OnCommand(int control_id);
    std::string SelectedEmbeddingProvider() const;
    void ApplyEmbeddingProviderDefaults(bool force);
    RagLibraryConfig BuildRuntimeConfigFromFields() const;
    void SetRuntimeDiagnostics(const RagEmbeddingRuntimeStatus& status);
    void SetEmbeddingTestDiagnostics(const RagEmbeddingTestResult& result);
    void RefreshRuntimeStatus();
    void StartRuntime();
    void StopRuntime();
    void InstallRuntime();
    void InstallRuntimeModel();
    void TestEmbedding();
    bool ValidateAndSave();

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    RagService* rag_service_ = nullptr;
    HFONT font_ = nullptr;
    RagLibraryConfig config_;
    bool editing_ = false;
    std::optional<RagLibraryConfig> result_;

    HWND name_label_ = nullptr;
    HWND name_edit_ = nullptr;
    HWND description_label_ = nullptr;
    HWND description_edit_ = nullptr;
    HWND storage_label_ = nullptr;
    HWND storage_edit_ = nullptr;
    HWND browse_button_ = nullptr;
    HWND chunk_size_label_ = nullptr;
    HWND chunk_size_edit_ = nullptr;
    HWND overlap_label_ = nullptr;
    HWND overlap_edit_ = nullptr;
    HWND max_chunks_label_ = nullptr;
    HWND max_chunks_edit_ = nullptr;
    HWND max_file_size_label_ = nullptr;
    HWND max_file_size_edit_ = nullptr;
    HWND embedding_provider_label_ = nullptr;
    HWND embedding_provider_edit_ = nullptr;
    HWND embedding_model_label_ = nullptr;
    HWND embedding_model_edit_ = nullptr;
    HWND embedding_dimensions_label_ = nullptr;
    HWND embedding_dimensions_edit_ = nullptr;
    HWND vector_backend_label_ = nullptr;
    HWND vector_backend_edit_ = nullptr;
    HWND embedding_base_url_label_ = nullptr;
    HWND embedding_base_url_edit_ = nullptr;
    HWND segment_enabled_checkbox_ = nullptr;
    HWND segment_threshold_label_ = nullptr;
    HWND segment_threshold_edit_ = nullptr;
    HWND segment_size_label_ = nullptr;
    HWND segment_size_edit_ = nullptr;
    HWND segment_overlap_label_ = nullptr;
    HWND segment_overlap_edit_ = nullptr;
    HWND runtime_status_ = nullptr;
    HWND runtime_check_button_ = nullptr;
    HWND runtime_start_button_ = nullptr;
    HWND runtime_stop_button_ = nullptr;
    HWND runtime_install_button_ = nullptr;
    HWND runtime_install_model_button_ = nullptr;
    HWND runtime_test_button_ = nullptr;
    HWND runtime_log_label_ = nullptr;
    HWND runtime_log_edit_ = nullptr;
    HWND enabled_checkbox_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;
};

class RagServiceManagerWindow {
public:
    RagServiceManagerWindow(HWND owner, RagService* rag_service, std::function<std::string()> active_project_id_provider)
        : owner_(owner), rag_service_(rag_service), active_project_id_provider_(std::move(active_project_id_provider)) {}

    HWND Create(HINSTANCE instance);

private:
    static void RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void OnCreate();
    void LayoutControls(int width, int height) const;
    void OnCommand(int control_id, int notification_code);
    void RefreshLibraries();
    void RefreshDetails();
    void AddLibrary();
    void EditLibrary();
    void RemoveLibrary();
    void AttachSelected(bool read_write);
    void DetachSelected();
    void IngestFiles();
    void IngestFolder();
    void RebuildSelected();
    void BrowseDocuments();
    void ReindexDocument();
    void DeleteDocument();
    void ShowExtractionTools();
    void OnRebuildProgress(ProgressPayload* payload);
    void OnIngestionFinished(IngestionPayload* payload);
    void Search();
    void UpdateStatus(const std::wstring& text) const;
    bool ActiveProjectCanWrite(const std::string& rag_id) const;
    int SelectedIndex() const;
    const RagLibraryConfig* SelectedLibrary() const;
    std::string ActiveProjectId() const;

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    RagService* rag_service_ = nullptr;
    std::function<std::string()> active_project_id_provider_;
    std::vector<RagLibraryConfig> libraries_;

    HWND libraries_list_ = nullptr;
    HWND details_edit_ = nullptr;
    HWND add_button_ = nullptr;
    HWND edit_button_ = nullptr;
    HWND remove_button_ = nullptr;
    HWND attach_read_button_ = nullptr;
    HWND attach_write_button_ = nullptr;
    HWND detach_button_ = nullptr;
    HWND install_tools_button_ = nullptr;
    HWND ingest_button_ = nullptr;
    HWND ingest_folder_button_ = nullptr;
    HWND rebuild_button_ = nullptr;
    HWND documents_button_ = nullptr;
    HWND reindex_document_button_ = nullptr;
    HWND delete_document_button_ = nullptr;
    HWND progress_bar_ = nullptr;
    HWND search_edit_ = nullptr;
    HWND search_button_ = nullptr;
    HWND results_edit_ = nullptr;
    HWND status_label_ = nullptr;
    HWND close_button_ = nullptr;
};

std::optional<RagLibraryConfig> RagLibraryEditorDialog::Show(HWND owner, RagService* rag_service, const RagLibraryConfig& initial, bool editing) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    RegisterWindowClass(instance);

    auto* dialog = new RagLibraryEditorDialog(owner, rag_service, initial, editing);
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE;
    const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

    if (owner) {
        EnableWindow(owner, FALSE);
    }

    dialog->hwnd_ = CreateWindowExW(
        ex_style,
        kRagLibraryEditorClassName,
        editing ? L"Edit RAG Library" : L"Add RAG Library",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        680,
        900,
        owner,
        nullptr,
        instance,
        dialog);

    if (!dialog->hwnd_) {
        if (owner) {
            EnableWindow(owner, TRUE);
        }
        delete dialog;
        return std::nullopt;
    }

    ShowWindow(dialog->hwnd_, SW_SHOW);
    UpdateWindow(dialog->hwnd_);

    MSG msg{};
    while (IsWindow(dialog->hwnd_) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog->hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }

    const auto result = dialog->result_;
    delete dialog;
    return result;
}

void RagLibraryEditorDialog::RegisterWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &RagLibraryEditorDialog::WindowProc;
    wc.lpszClassName = kRagLibraryEditorClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}

LRESULT CALLBACK RagLibraryEditorDialog::WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<RagLibraryEditorDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<RagLibraryEditorDialog*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    if (!self) {
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    switch (message) {
    case WM_CREATE:
        self->OnCreate();
        return 0;
    case WM_COMMAND:
        self->OnCommand(LOWORD(w_param));
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

void RagLibraryEditorDialog::OnCreate() {
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    name_label_ = CreateWindowExW(0, L"STATIC", L"Name", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorNameLabel), nullptr, nullptr);
    name_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(config_.name).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorNameEdit), nullptr, nullptr);
    description_label_ = CreateWindowExW(0, L"STATIC", L"Description", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorDescriptionLabel), nullptr, nullptr);
    description_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(config_.description).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorDescriptionEdit), nullptr, nullptr);
    storage_label_ = CreateWindowExW(0, L"STATIC", editing_ ? L"Storage Folder (moving existing RAGs will be a separate migration step)" : L"Storage Parent Folder", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorStorageLabel), nullptr, nullptr);
    storage_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(config_.storage_path).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (editing_ ? ES_READONLY : 0), 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorStorageEdit), nullptr, nullptr);
    browse_button_ = CreateWindowExW(0, L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorBrowseStorage), nullptr, nullptr);
    chunk_size_label_ = CreateWindowExW(0, L"STATIC", L"Chunk Size (characters)", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorChunkSizeLabel), nullptr, nullptr);
    chunk_size_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(config_.chunk_size_chars).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorChunkSizeEdit), nullptr, nullptr);
    overlap_label_ = CreateWindowExW(0, L"STATIC", L"Chunk Overlap (characters)", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorOverlapLabel), nullptr, nullptr);
    overlap_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(config_.chunk_overlap_chars).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorOverlapEdit), nullptr, nullptr);
    max_chunks_label_ = CreateWindowExW(0, L"STATIC", L"Default Max Retrieved Chunks", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorMaxChunksLabel), nullptr, nullptr);
    max_chunks_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(config_.default_max_chunks).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorMaxChunksEdit), nullptr, nullptr);
    max_file_size_label_ = CreateWindowExW(0, L"STATIC", L"Max File Size (MB)", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorMaxFileSizeLabel), nullptr, nullptr);
    max_file_size_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(config_.max_file_size_mb).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorMaxFileSizeEdit), nullptr, nullptr);
    embedding_provider_label_ = CreateWindowExW(0, L"STATIC", L"Embedding Provider", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEmbeddingProviderLabel), nullptr, nullptr);
    embedding_provider_edit_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEmbeddingProviderEdit), nullptr, nullptr);
    embedding_model_label_ = CreateWindowExW(0, L"STATIC", L"Embedding Model", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEmbeddingModelLabel), nullptr, nullptr);
    embedding_model_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(config_.embedding_model).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEmbeddingModelEdit), nullptr, nullptr);
    embedding_dimensions_label_ = CreateWindowExW(0, L"STATIC", L"Embedding Dimensions", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEmbeddingDimensionsLabel), nullptr, nullptr);
    embedding_dimensions_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(config_.embedding_dimensions).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEmbeddingDimensionsEdit), nullptr, nullptr);
    vector_backend_label_ = CreateWindowExW(0, L"STATIC", L"Vector Backend", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorVectorBackendLabel), nullptr, nullptr);
    vector_backend_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(config_.vector_backend).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorVectorBackendEdit), nullptr, nullptr);
    embedding_base_url_label_ = CreateWindowExW(0, L"STATIC", L"Embedding Base URL", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEmbeddingBaseUrlLabel), nullptr, nullptr);
    embedding_base_url_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(config_.embedding_base_url).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEmbeddingBaseUrlEdit), nullptr, nullptr);
    segment_enabled_checkbox_ = CreateWindowExW(0, L"BUTTON", L"Split large extracted Markdown/text into segment files", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorSegmentEnabled), nullptr, nullptr);
    segment_threshold_label_ = CreateWindowExW(0, L"STATIC", L"Split Threshold (MB)", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorSegmentThresholdLabel), nullptr, nullptr);
    segment_threshold_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(config_.extracted_segment_threshold_mb).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorSegmentThresholdEdit), nullptr, nullptr);
    segment_size_label_ = CreateWindowExW(0, L"STATIC", L"Segment Size (MB)", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorSegmentSizeLabel), nullptr, nullptr);
    segment_size_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(config_.extracted_segment_size_mb).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorSegmentSizeEdit), nullptr, nullptr);
    segment_overlap_label_ = CreateWindowExW(0, L"STATIC", L"Segment Overlap (chars)", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorSegmentOverlapLabel), nullptr, nullptr);
    segment_overlap_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(config_.extracted_segment_overlap_chars).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorSegmentOverlapEdit), nullptr, nullptr);
    runtime_status_ = CreateWindowExW(0, L"STATIC", L"Embedding runtime status: not checked.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorRuntimeStatus), nullptr, nullptr);
    runtime_check_button_ = CreateWindowExW(0, L"BUTTON", L"Check", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorRuntimeCheck), nullptr, nullptr);
    runtime_test_button_ = CreateWindowExW(0, L"BUTTON", L"Test", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorRuntimeTestEmbedding), nullptr, nullptr);
    runtime_start_button_ = CreateWindowExW(0, L"BUTTON", L"Start", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorRuntimeStart), nullptr, nullptr);
    runtime_stop_button_ = CreateWindowExW(0, L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorRuntimeStop), nullptr, nullptr);
    runtime_install_button_ = CreateWindowExW(0, L"BUTTON", L"Install", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorRuntimeInstall), nullptr, nullptr);
    runtime_install_model_button_ = CreateWindowExW(0, L"BUTTON", L"Install Embed Text Model", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorRuntimeInstallModel), nullptr, nullptr);
    runtime_log_label_ = CreateWindowExW(0, L"STATIC", L"Runtime Diagnostics Log", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorRuntimeLogLabel), nullptr, nullptr);
    runtime_log_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorRuntimeLog), nullptr, nullptr);
    enabled_checkbox_ = CreateWindowExW(0, L"BUTTON", L"Enabled", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEnabled), nullptr, nullptr);
    save_button_ = CreateWindowExW(0, L"BUTTON", editing_ ? L"Save" : L"Add", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorSaveButton), nullptr, nullptr);
    cancel_button_ = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorCancelButton), nullptr, nullptr);

    for (HWND control : {name_label_, name_edit_, description_label_, description_edit_, storage_label_, storage_edit_, browse_button_, chunk_size_label_, chunk_size_edit_, overlap_label_, overlap_edit_, max_chunks_label_, max_chunks_edit_, max_file_size_label_, max_file_size_edit_, embedding_provider_label_, embedding_provider_edit_, embedding_model_label_, embedding_model_edit_, embedding_dimensions_label_, embedding_dimensions_edit_, vector_backend_label_, vector_backend_edit_, embedding_base_url_label_, embedding_base_url_edit_, segment_enabled_checkbox_, segment_threshold_label_, segment_threshold_edit_, segment_size_label_, segment_size_edit_, segment_overlap_label_, segment_overlap_edit_, runtime_status_, runtime_check_button_, runtime_test_button_, runtime_start_button_, runtime_stop_button_, runtime_install_button_, runtime_install_model_button_, runtime_log_label_, runtime_log_edit_, enabled_checkbox_, save_button_, cancel_button_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    ComboBox_AddString(embedding_provider_edit_, L"None");
    ComboBox_AddString(embedding_provider_edit_, L"Ollama");
    ComboBox_AddString(embedding_provider_edit_, L"LM Studio");
    ComboBox_SetCurSel(embedding_provider_edit_, ProviderComboIndex(config_.embedding_provider));
    ApplyEmbeddingProviderDefaults(false);
    RefreshRuntimeStatus();
    Button_SetCheck(segment_enabled_checkbox_, config_.split_large_extracted_documents ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(enabled_checkbox_, config_.enabled ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(browse_button_, editing_ ? FALSE : TRUE);

    CenterWindowToOwner(hwnd_, owner_);
    LayoutControls();
    SetFocus(name_edit_);
}

void RagLibraryEditorDialog::LayoutControls() const {
    const int margin = Scale(hwnd_, 12);
    const int gutter = Scale(hwnd_, 8);
    const int label_height = Scale(hwnd_, 18);
    const int edit_height = Scale(hwnd_, 24);
    const int button_height = Scale(hwnd_, 30);
    const int button_width = Scale(hwnd_, 96);
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    int y = margin;
    MoveWindow(name_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(name_edit_, margin, y, width - margin * 2, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(description_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(description_edit_, margin, y, width - margin * 2, Scale(hwnd_, 86), TRUE);
    y += Scale(hwnd_, 86) + gutter;

    MoveWindow(storage_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    const int browse_width = Scale(hwnd_, 94);
    MoveWindow(storage_edit_, margin, y, width - margin * 2 - browse_width - gutter, edit_height, TRUE);
    MoveWindow(browse_button_, width - margin - browse_width, y, browse_width, edit_height, TRUE);
    y += edit_height + gutter;

    const int column_width = (width - margin * 2 - gutter * 2) / 3;
    MoveWindow(chunk_size_label_, margin, y, column_width, label_height, TRUE);
    MoveWindow(overlap_label_, margin + column_width + gutter, y, column_width, label_height, TRUE);
    MoveWindow(max_chunks_label_, margin + (column_width + gutter) * 2, y, column_width, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(chunk_size_edit_, margin, y, column_width, edit_height, TRUE);
    MoveWindow(overlap_edit_, margin + column_width + gutter, y, column_width, edit_height, TRUE);
    MoveWindow(max_chunks_edit_, margin + (column_width + gutter) * 2, y, column_width, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(max_file_size_label_, margin, y, column_width, label_height, TRUE);
    MoveWindow(embedding_provider_label_, margin + column_width + gutter, y, column_width, label_height, TRUE);
    MoveWindow(embedding_dimensions_label_, margin + (column_width + gutter) * 2, y, column_width, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(max_file_size_edit_, margin, y, column_width, edit_height, TRUE);
    MoveWindow(embedding_provider_edit_, margin + column_width + gutter, y, column_width, Scale(hwnd_, 120), TRUE);
    MoveWindow(embedding_dimensions_edit_, margin + (column_width + gutter) * 2, y, column_width, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(vector_backend_label_, margin, y, column_width, label_height, TRUE);
    MoveWindow(embedding_model_label_, margin + column_width + gutter, y, width - margin * 2 - column_width - gutter, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(vector_backend_edit_, margin, y, column_width, edit_height, TRUE);
    MoveWindow(embedding_model_edit_, margin + column_width + gutter, y, width - margin * 2 - column_width - gutter, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(embedding_base_url_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(embedding_base_url_edit_, margin, y, width - margin * 2, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(segment_enabled_checkbox_, margin, y, width - margin * 2, Scale(hwnd_, 22), TRUE);
    y += Scale(hwnd_, 22) + gutter;
    MoveWindow(segment_threshold_label_, margin, y, column_width, label_height, TRUE);
    MoveWindow(segment_size_label_, margin + column_width + gutter, y, column_width, label_height, TRUE);
    MoveWindow(segment_overlap_label_, margin + (column_width + gutter) * 2, y, column_width, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(segment_threshold_edit_, margin, y, column_width, edit_height, TRUE);
    MoveWindow(segment_size_edit_, margin + column_width + gutter, y, column_width, edit_height, TRUE);
    MoveWindow(segment_overlap_edit_, margin + (column_width + gutter) * 2, y, column_width, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(runtime_status_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    const int runtime_button_width = Scale(hwnd_, 82);
    const int model_button_width = Scale(hwnd_, 174);
    MoveWindow(runtime_check_button_, margin, y, runtime_button_width, button_height, TRUE);
    MoveWindow(runtime_test_button_, margin + runtime_button_width + gutter, y, runtime_button_width, button_height, TRUE);
    MoveWindow(runtime_start_button_, margin + (runtime_button_width + gutter) * 2, y, runtime_button_width, button_height, TRUE);
    MoveWindow(runtime_stop_button_, margin + (runtime_button_width + gutter) * 3, y, runtime_button_width, button_height, TRUE);
    MoveWindow(runtime_install_button_, margin + (runtime_button_width + gutter) * 4, y, runtime_button_width, button_height, TRUE);
    MoveWindow(runtime_install_model_button_, margin + (runtime_button_width + gutter) * 5, y, model_button_width, button_height, TRUE);
    y += button_height + gutter;

    MoveWindow(runtime_log_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(runtime_log_edit_, margin, y, width - margin * 2, Scale(hwnd_, 100), TRUE);
    y += Scale(hwnd_, 100) + gutter;

    MoveWindow(enabled_checkbox_, margin, y, width - margin * 2, Scale(hwnd_, 22), TRUE);

    const int buttons_y = height - margin - button_height;
    MoveWindow(cancel_button_, width - margin - button_width, buttons_y, button_width, button_height, TRUE);
    MoveWindow(save_button_, width - margin - button_width * 2 - gutter, buttons_y, button_width, button_height, TRUE);
}

void RagLibraryEditorDialog::OnCommand(int control_id) {
    switch (control_id) {
    case kEditorEmbeddingProviderEdit:
        ApplyEmbeddingProviderDefaults(true);
        RefreshRuntimeStatus();
        break;
    case kEditorRuntimeCheck:
        RefreshRuntimeStatus();
        break;
    case kEditorRuntimeTestEmbedding:
        TestEmbedding();
        break;
    case kEditorRuntimeStart:
        StartRuntime();
        break;
    case kEditorRuntimeStop:
        StopRuntime();
        break;
    case kEditorRuntimeInstall:
        InstallRuntime();
        break;
    case kEditorRuntimeInstallModel:
        InstallRuntimeModel();
        break;
    case kEditorBrowseStorage:
        if (!editing_) {
            const auto folder = PickFolder(hwnd_, L"Select the parent folder that will hold this RAG library. A dedicated RAG subfolder will be created inside it.");
            if (folder) {
                SetWindowTextW(storage_edit_, std::filesystem::absolute(*folder).wstring().c_str());
            }
        }
        break;
    case kEditorSaveButton:
        ValidateAndSave();
        break;
    case kEditorCancelButton:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

std::string RagLibraryEditorDialog::SelectedEmbeddingProvider() const {
    return ProviderFromComboIndex(static_cast<int>(ComboBox_GetCurSel(embedding_provider_edit_)));
}

void RagLibraryEditorDialog::ApplyEmbeddingProviderDefaults(bool force) {
    const std::string provider = SelectedEmbeddingProvider();
    if (provider == "none") {
        if (force) {
            SetWindowTextW(embedding_model_edit_, L"");
            SetWindowTextW(embedding_base_url_edit_, L"");
            SetWindowTextW(embedding_dimensions_edit_, L"0");
        }
        return;
    }

    if (provider == "ollama") {
        if (force || TrimWide(GetWindowTextString(embedding_model_edit_)).empty()) {
            SetWindowTextW(embedding_model_edit_, L"nomic-embed-text");
        }
        if (force || TrimWide(GetWindowTextString(embedding_base_url_edit_)).empty()) {
            SetWindowTextW(embedding_base_url_edit_, L"http://localhost:11434");
        }
        if (TrimWide(GetWindowTextString(vector_backend_edit_)).empty()) {
            SetWindowTextW(vector_backend_edit_, L"sqlite_vector_scan");
        }
        return;
    }

    if (provider == "lmstudio") {
        if (force || TrimWide(GetWindowTextString(embedding_model_edit_)).empty()) {
            SetWindowTextW(embedding_model_edit_, L"nomic-embed-text-v1.5");
        }
        if (force || TrimWide(GetWindowTextString(embedding_base_url_edit_)).empty()) {
            SetWindowTextW(embedding_base_url_edit_, L"http://localhost:1234/v1");
        }
        if (TrimWide(GetWindowTextString(vector_backend_edit_)).empty()) {
            SetWindowTextW(vector_backend_edit_, L"sqlite_vector_scan");
        }
    }
}

RagLibraryConfig RagLibraryEditorDialog::BuildRuntimeConfigFromFields() const {
    RagLibraryConfig config = config_;
    config.embedding_provider = SelectedEmbeddingProvider();
    config.embedding_model = WideToUtf8(TrimWide(GetWindowTextString(embedding_model_edit_)));
    config.embedding_base_url = WideToUtf8(TrimWide(GetWindowTextString(embedding_base_url_edit_)));
    if (config.embedding_base_url.empty()) {
        config.embedding_base_url = config.embedding_provider == "lmstudio" ? "http://localhost:1234/v1" : "http://localhost:11434";
    }
    return config;
}

void RagLibraryEditorDialog::SetRuntimeDiagnostics(const RagEmbeddingRuntimeStatus& status) {
    std::wstring status_text = L"Embedding runtime status: " + Utf8ToWide(status.message);
    if (!status.log_path.empty()) {
        status_text += L" | Log: " + Utf8ToWide(status.log_path);
    }
    SetWindowTextW(runtime_status_, status_text.c_str());
    SetWindowTextW(runtime_log_edit_, Utf8ToWide(status.recent_log).c_str());
    SendMessageW(runtime_log_edit_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(runtime_log_edit_, EM_SCROLLCARET, 0, 0);

    const bool ollama = status.provider == "ollama";
    EnableWindow(runtime_check_button_, ollama ? TRUE : FALSE);
    EnableWindow(runtime_test_button_, status.provider != "none" ? TRUE : FALSE);
    EnableWindow(runtime_start_button_, ollama && status.installed && !status.running ? TRUE : FALSE);
    EnableWindow(runtime_stop_button_, ollama && status.running && status.managed_by_app ? TRUE : FALSE);
    EnableWindow(runtime_install_button_, ollama && !status.installed ? TRUE : FALSE);
    EnableWindow(runtime_install_model_button_, ollama && status.installed && status.running ? TRUE : FALSE);
}

void RagLibraryEditorDialog::SetEmbeddingTestDiagnostics(const RagEmbeddingTestResult& result) {
    SetRuntimeDiagnostics(result.runtime_status);
    SetWindowTextW(runtime_status_, (L"Embedding test: " + Utf8ToWide(result.message)).c_str());

    std::wstring log = Utf8ToWide(result.runtime_status.recent_log);
    if (!log.empty()) {
        log += L"\r\n";
    }
    log += L"Test result: " + Utf8ToWide(result.message) + L"\r\n";
    if (result.success) {
        log += L"Provider: " + Utf8ToWide(result.provider) + L"\r\n";
        log += L"Model: " + Utf8ToWide(result.model) + L"\r\n";
        log += L"Dimensions: " + std::to_wstring(result.dimensions) + L"\r\n";
        log += L"Elapsed: " + std::to_wstring(static_cast<int>(result.elapsed_ms)) + L" ms\r\n";
    }
    SetWindowTextW(runtime_log_edit_, log.c_str());
    SendMessageW(runtime_log_edit_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(runtime_log_edit_, EM_SCROLLCARET, 0, 0);
}

void RagLibraryEditorDialog::RefreshRuntimeStatus() {
    if (!rag_service_) {
        return;
    }
    SetRuntimeDiagnostics(rag_service_->GetEmbeddingRuntimeStatus(BuildRuntimeConfigFromFields()));
}

void RagLibraryEditorDialog::StartRuntime() {
    if (!rag_service_) {
        return;
    }
    SetRuntimeDiagnostics(rag_service_->StartEmbeddingRuntime(BuildRuntimeConfigFromFields()));
}

void RagLibraryEditorDialog::StopRuntime() {
    if (!rag_service_) {
        return;
    }
    SetRuntimeDiagnostics(rag_service_->StopEmbeddingRuntime(BuildRuntimeConfigFromFields()));
}

void RagLibraryEditorDialog::InstallRuntime() {
    if (!rag_service_) {
        return;
    }
    SetRuntimeDiagnostics(rag_service_->LaunchEmbeddingRuntimeInstaller(BuildRuntimeConfigFromFields()));
}

void RagLibraryEditorDialog::InstallRuntimeModel() {
    if (!rag_service_) {
        return;
    }
    SetRuntimeDiagnostics(rag_service_->LaunchEmbeddingModelInstaller(BuildRuntimeConfigFromFields()));
}

void RagLibraryEditorDialog::TestEmbedding() {
    if (!rag_service_) {
        return;
    }
    SetEmbeddingTestDiagnostics(rag_service_->TestEmbeddingProvider(BuildRuntimeConfigFromFields()));
}

bool RagLibraryEditorDialog::ValidateAndSave() {
    RagLibraryConfig edited = config_;
    edited.name = WideToUtf8(TrimWide(GetWindowTextString(name_edit_)));
    edited.description = WideToUtf8(TrimWide(GetWindowTextString(description_edit_)));
    edited.storage_path = WideToUtf8(TrimWide(GetWindowTextString(storage_edit_)));
    edited.enabled = Button_GetCheck(enabled_checkbox_) == BST_CHECKED;

    const auto chunk_size = ParseInt(GetWindowTextString(chunk_size_edit_));
    const auto overlap = ParseInt(GetWindowTextString(overlap_edit_));
    const auto max_chunks = ParseInt(GetWindowTextString(max_chunks_edit_));
    const auto max_file_size = ParseInt(GetWindowTextString(max_file_size_edit_));
    const auto embedding_dimensions = ParseInt(GetWindowTextString(embedding_dimensions_edit_));
    const auto segment_threshold = ParseInt(GetWindowTextString(segment_threshold_edit_));
    const auto segment_size = ParseInt(GetWindowTextString(segment_size_edit_));
    const auto segment_overlap = ParseInt(GetWindowTextString(segment_overlap_edit_));
    if (edited.name.empty()) {
        MessageBoxW(hwnd_, L"RAG library name is required.", L"Missing Name", MB_OK | MB_ICONERROR);
        SetFocus(name_edit_);
        return false;
    }
    if (edited.storage_path.empty()) {
        MessageBoxW(hwnd_, L"Storage folder is required.", L"Missing Storage Folder", MB_OK | MB_ICONERROR);
        SetFocus(storage_edit_);
        return false;
    }
    if (!chunk_size || *chunk_size < 500) {
        MessageBoxW(hwnd_, L"Chunk size must be at least 500 characters.", L"Invalid Chunk Size", MB_OK | MB_ICONERROR);
        SetFocus(chunk_size_edit_);
        return false;
    }
    if (!overlap || *overlap < 0 || *overlap >= *chunk_size) {
        MessageBoxW(hwnd_, L"Chunk overlap must be 0 or greater and smaller than the chunk size.", L"Invalid Chunk Overlap", MB_OK | MB_ICONERROR);
        SetFocus(overlap_edit_);
        return false;
    }
    if (!max_chunks || *max_chunks < 1) {
        MessageBoxW(hwnd_, L"Default max retrieved chunks must be at least 1.", L"Invalid Max Chunks", MB_OK | MB_ICONERROR);
        SetFocus(max_chunks_edit_);
        return false;
    }
    if (!max_file_size || *max_file_size < 1) {
        MessageBoxW(hwnd_, L"Max file size must be at least 1 MB.", L"Invalid Max File Size", MB_OK | MB_ICONERROR);
        SetFocus(max_file_size_edit_);
        return false;
    }
    if (!embedding_dimensions || *embedding_dimensions < 0) {
        MessageBoxW(hwnd_, L"Embedding dimensions must be 0 or greater.", L"Invalid Embedding Dimensions", MB_OK | MB_ICONERROR);
        SetFocus(embedding_dimensions_edit_);
        return false;
    }
    if (!segment_threshold || *segment_threshold < 1) {
        MessageBoxW(hwnd_, L"Segment split threshold must be at least 1 MB.", L"Invalid Segment Threshold", MB_OK | MB_ICONERROR);
        SetFocus(segment_threshold_edit_);
        return false;
    }
    if (!segment_size || *segment_size < 1) {
        MessageBoxW(hwnd_, L"Segment size must be at least 1 MB.", L"Invalid Segment Size", MB_OK | MB_ICONERROR);
        SetFocus(segment_size_edit_);
        return false;
    }
    if (!segment_overlap || *segment_overlap < 0) {
        MessageBoxW(hwnd_, L"Segment overlap must be 0 or greater.", L"Invalid Segment Overlap", MB_OK | MB_ICONERROR);
        SetFocus(segment_overlap_edit_);
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(Utf8ToWide(edited.storage_path)), ec);
    if (ec || !std::filesystem::exists(std::filesystem::path(Utf8ToWide(edited.storage_path)))) {
        MessageBoxW(hwnd_, L"The storage folder could not be created or opened.", L"Invalid Storage Folder", MB_OK | MB_ICONERROR);
        SetFocus(storage_edit_);
        return false;
    }

    edited.chunk_size_chars = *chunk_size;
    edited.chunk_overlap_chars = *overlap;
    edited.default_max_chunks = *max_chunks;
    edited.max_file_size_mb = *max_file_size;
    edited.split_large_extracted_documents = Button_GetCheck(segment_enabled_checkbox_) == BST_CHECKED;
    edited.extracted_segment_threshold_mb = *segment_threshold;
    edited.extracted_segment_size_mb = *segment_size;
    edited.extracted_segment_overlap_chars = *segment_overlap;
    edited.embedding_provider = SelectedEmbeddingProvider();
    if (edited.embedding_provider.empty()) {
        edited.embedding_provider = "none";
    }
    edited.embedding_model = WideToUtf8(TrimWide(GetWindowTextString(embedding_model_edit_)));
    edited.embedding_base_url = WideToUtf8(TrimWide(GetWindowTextString(embedding_base_url_edit_)));
    if (edited.embedding_base_url.empty()) {
        edited.embedding_base_url = "http://localhost:11434";
    }
    edited.embedding_dimensions = *embedding_dimensions;
    edited.vector_backend = WideToUtf8(TrimWide(GetWindowTextString(vector_backend_edit_)));
    if (edited.vector_backend.empty()) {
        edited.vector_backend = "sqlite_vector_scan";
    }
    std::string provider_lower = edited.embedding_provider;
    std::transform(provider_lower.begin(), provider_lower.end(), provider_lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if ((provider_lower == "ollama" || provider_lower == "lmstudio") && edited.embedding_model.empty()) {
        MessageBoxW(hwnd_, L"The selected embedding provider requires an embedding model.", L"Missing Embedding Model", MB_OK | MB_ICONERROR);
        SetFocus(embedding_model_edit_);
        return false;
    }
    result_ = std::move(edited);
    DestroyWindow(hwnd_);
    return true;
}

HWND RagServiceManagerWindow::Create(HINSTANCE instance) {
    RegisterWindowClass(instance);
    hwnd_ = CreateWindowExW(
        0,
        kRagManagerClassName,
        L"RAG Service Manager",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1120,
        760,
        owner_,
        nullptr,
        instance,
        this);
    return hwnd_;
}

void RagServiceManagerWindow::RegisterWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &RagServiceManagerWindow::WindowProc;
    wc.lpszClassName = kRagManagerClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}

LRESULT CALLBACK RagServiceManagerWindow::WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<RagServiceManagerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<RagServiceManagerWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    if (!self) {
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    switch (message) {
    case WM_CREATE:
        self->OnCreate();
        return 0;
    case WM_SIZE:
        self->LayoutControls(LOWORD(l_param), HIWORD(l_param));
        return 0;
    case WM_COMMAND:
        self->OnCommand(LOWORD(w_param), HIWORD(w_param));
        return 0;
    case kIngestionFinishedMessage:
        self->OnIngestionFinished(reinterpret_cast<IngestionPayload*>(l_param));
        return 0;
    case kRebuildProgressMessage:
        self->OnRebuildProgress(reinterpret_cast<ProgressPayload*>(l_param));
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        delete self;
        return 0;
    default:
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

void RagServiceManagerWindow::OnCreate() {
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&controls);

    libraries_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kLibrariesList), nullptr, nullptr);
    details_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDetailsEdit), nullptr, nullptr);
    add_button_ = CreateWindowExW(0, L"BUTTON", L"Add RAG", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAddLibrary), nullptr, nullptr);
    edit_button_ = CreateWindowExW(0, L"BUTTON", L"Edit", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditLibrary), nullptr, nullptr);
    remove_button_ = CreateWindowExW(0, L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRemoveLibrary), nullptr, nullptr);
    attach_read_button_ = CreateWindowExW(0, L"BUTTON", L"Attach Read", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAttachReadOnly), nullptr, nullptr);
    attach_write_button_ = CreateWindowExW(0, L"BUTTON", L"Attach RW", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAttachReadWrite), nullptr, nullptr);
    detach_button_ = CreateWindowExW(0, L"BUTTON", L"Detach", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDetachProject), nullptr, nullptr);
    install_tools_button_ = CreateWindowExW(0, L"BUTTON", L"Install Tools", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kInstallTools), nullptr, nullptr);
    ingest_button_ = CreateWindowExW(0, L"BUTTON", L"Ingest Files", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kIngestFiles), nullptr, nullptr);
    ingest_folder_button_ = CreateWindowExW(0, L"BUTTON", L"Ingest Folder", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kIngestFolder), nullptr, nullptr);
    rebuild_button_ = CreateWindowExW(0, L"BUTTON", L"Rebuild DB", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRebuildLibrary), nullptr, nullptr);
    documents_button_ = CreateWindowExW(0, L"BUTTON", L"Browse Docs", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kBrowseDocuments), nullptr, nullptr);
    reindex_document_button_ = CreateWindowExW(0, L"BUTTON", L"Reindex Doc", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kReindexDocument), nullptr, nullptr);
    delete_document_button_ = CreateWindowExW(0, L"BUTTON", L"Delete Doc", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDeleteDocument), nullptr, nullptr);
    progress_bar_ = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProgressBar), nullptr, nullptr);
    search_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSearchEdit), nullptr, nullptr);
    search_button_ = CreateWindowExW(0, L"BUTTON", L"Search", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSearchButton), nullptr, nullptr);
    results_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kResultsEdit), nullptr, nullptr);
    status_label_ = CreateWindowExW(0, L"STATIC", L"Manage reusable RAG libraries and attach them to the active project.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStatusLabel), nullptr, nullptr);
    close_button_ = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCloseButton), nullptr, nullptr);

    for (HWND control : {libraries_list_, details_edit_, add_button_, edit_button_, remove_button_, attach_read_button_, attach_write_button_, detach_button_, install_tools_button_, ingest_button_, ingest_folder_button_, rebuild_button_, documents_button_, reindex_document_button_, delete_document_button_, search_edit_, search_button_, results_edit_, status_label_, close_button_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, 0, 0);

    rag_service_->EnsureInitialized();
    RefreshLibraries();
    CenterWindowToOwner(hwnd_, owner_);
}

void RagServiceManagerWindow::LayoutControls(int width, int height) const {
    const int margin = Scale(hwnd_, 10);
    const int gutter = Scale(hwnd_, 10);
    const int button_height = Scale(hwnd_, 28);
    const int status_height = Scale(hwnd_, 20);
    const int left_width = std::max(Scale(hwnd_, 300), width / 3);
    const int button_width = Scale(hwnd_, 106);
    const int footer_y = height - margin - button_height;

    int y = margin;
    MoveWindow(add_button_, margin, y, button_width, button_height, TRUE);
    MoveWindow(edit_button_, margin + button_width + gutter, y, button_width, button_height, TRUE);
    MoveWindow(remove_button_, margin + (button_width + gutter) * 2, y, button_width, button_height, TRUE);
    y += button_height + gutter;

    const int project_row_width = (left_width - gutter * 2) / 3;
    MoveWindow(attach_read_button_, margin, y, project_row_width, button_height, TRUE);
    MoveWindow(attach_write_button_, margin + project_row_width + gutter, y, project_row_width, button_height, TRUE);
    MoveWindow(detach_button_, margin + (project_row_width + gutter) * 2, y, project_row_width, button_height, TRUE);
    y += button_height + gutter;

    const int tools_y = footer_y - button_height - gutter;
    MoveWindow(libraries_list_, margin, y, left_width, tools_y - y - gutter, TRUE);
    MoveWindow(install_tools_button_, margin, tools_y, left_width, button_height, TRUE);

    const int right_x = margin + left_width + gutter;
    const int right_width = width - right_x - margin;
    const int search_button_width = Scale(hwnd_, 90);
    const int ingest_width = Scale(hwnd_, 106);
    const int folder_width = Scale(hwnd_, 116);
    const int rebuild_width = Scale(hwnd_, 104);
    const int documents_width = Scale(hwnd_, 112);
    const int reindex_width = Scale(hwnd_, 106);
    const int delete_width = Scale(hwnd_, 96);
    const int progress_height = Scale(hwnd_, 16);
    const int details_height = std::max(Scale(hwnd_, 170), height / 4);

    MoveWindow(details_edit_, right_x, margin, right_width, details_height, TRUE);
    const int action_y = margin + details_height + gutter;
    MoveWindow(ingest_button_, right_x, action_y, ingest_width, button_height, TRUE);
    MoveWindow(ingest_folder_button_, right_x + ingest_width + gutter, action_y, folder_width, button_height, TRUE);
    MoveWindow(rebuild_button_, right_x + ingest_width + folder_width + gutter * 2, action_y, rebuild_width, button_height, TRUE);
    MoveWindow(documents_button_, right_x + ingest_width + folder_width + rebuild_width + gutter * 3, action_y, documents_width, button_height, TRUE);
    MoveWindow(reindex_document_button_, right_x + ingest_width + folder_width + rebuild_width + documents_width + gutter * 4, action_y, reindex_width, button_height, TRUE);
    MoveWindow(delete_document_button_, right_x + ingest_width + folder_width + rebuild_width + documents_width + reindex_width + gutter * 5, action_y, delete_width, button_height, TRUE);

    const int search_y = action_y + button_height + gutter;
    MoveWindow(search_edit_, right_x, search_y, std::max(Scale(hwnd_, 80), right_width - search_button_width - gutter), button_height, TRUE);
    MoveWindow(search_button_, right_x + right_width - search_button_width, search_y, search_button_width, button_height, TRUE);

    const int progress_y = search_y + button_height + Scale(hwnd_, 6);
    MoveWindow(progress_bar_, right_x, progress_y, right_width, progress_height, TRUE);

    const int results_y = progress_y + progress_height + gutter;
    MoveWindow(results_edit_, right_x, results_y, right_width, footer_y - results_y - gutter, TRUE);
    MoveWindow(status_label_, margin, height - margin - status_height, width - margin * 2 - Scale(hwnd_, 120), status_height, TRUE);
    MoveWindow(close_button_, width - margin - Scale(hwnd_, 100), footer_y, Scale(hwnd_, 100), button_height, TRUE);
}

int RagServiceManagerWindow::SelectedIndex() const {
    const LRESULT selection = SendMessageW(libraries_list_, LB_GETCURSEL, 0, 0);
    return selection == LB_ERR ? -1 : static_cast<int>(selection);
}

const RagLibraryConfig* RagServiceManagerWindow::SelectedLibrary() const {
    const int index = SelectedIndex();
    if (index < 0 || static_cast<size_t>(index) >= libraries_.size()) {
        return nullptr;
    }
    return &libraries_[static_cast<size_t>(index)];
}

std::string RagServiceManagerWindow::ActiveProjectId() const {
    return active_project_id_provider_ ? active_project_id_provider_() : std::string();
}

void RagServiceManagerWindow::RefreshLibraries() {
    const int previous = SelectedIndex();
    libraries_ = rag_service_->ListLibraries();
    const std::string project_id = ActiveProjectId();
    const auto bindings = project_id.empty() ? std::vector<ProjectRagBinding>{} : rag_service_->LoadProjectBindings(project_id);

    SendMessageW(libraries_list_, LB_RESETCONTENT, 0, 0);
    for (const auto& library : libraries_) {
        std::wstring label = Utf8ToWide(library.name);
        if (!library.enabled) {
            label += L" [disabled]";
        }
        if (!project_id.empty()) {
            const auto binding = FindBinding(bindings, library.id);
            if (binding) {
                label += binding->can_write ? L" [project RW]" : L" [project RO]";
            } else {
                label += L" [not attached]";
            }
        }
        SendMessageW(libraries_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!libraries_.empty()) {
        const int restore = previous >= 0 && static_cast<size_t>(previous) < libraries_.size() ? previous : 0;
        SendMessageW(libraries_list_, LB_SETCURSEL, restore, 0);
    }
    RefreshDetails();
}

void RagServiceManagerWindow::RefreshDetails() {
    const RagLibraryConfig* library = SelectedLibrary();
    if (!library) {
        SetWindowTextW(details_edit_, L"No RAG library selected.");
        return;
    }

    const RagLibraryStats stats = rag_service_->GetStats(library->id);
    const std::string project_id = ActiveProjectId();
    const auto bindings = project_id.empty() ? std::vector<ProjectRagBinding>{} : rag_service_->LoadProjectBindings(project_id);
    const auto binding = FindBinding(bindings, library->id);

    std::wstring details;
    details += L"Name: " + Utf8ToWide(library->name) + L"\r\n";
    details += L"ID: " + Utf8ToWide(library->id) + L"\r\n";
    details += L"Description: " + Utf8ToWide(library->description) + L"\r\n";
    details += L"Enabled: ";
    details += library->enabled ? L"yes" : L"no";
    details += L"\r\n";
    details += L"Storage mode: " + StorageModeLabel(library->storage_mode) + L"\r\n";
    details += L"Chunk size: " + std::to_wstring(library->chunk_size_chars) + L" chars\r\n";
    details += L"Chunk overlap: " + std::to_wstring(library->chunk_overlap_chars) + L" chars\r\n";
    details += L"Default max chunks: " + std::to_wstring(library->default_max_chunks) + L"\r\n";
    details += L"Max file size: " + std::to_wstring(library->max_file_size_mb) + L" MB\r\n";
    details += L"Split large extracted docs: ";
    details += library->split_large_extracted_documents ? L"yes" : L"no";
    details += L"\r\n";
    details += L"Extracted segment threshold: " + std::to_wstring(library->extracted_segment_threshold_mb) + L" MB\r\n";
    details += L"Extracted segment size: " + std::to_wstring(library->extracted_segment_size_mb) + L" MB\r\n";
    details += L"Extracted segment overlap: " + std::to_wstring(library->extracted_segment_overlap_chars) + L" chars\r\n";
    details += L"Embedding provider: " + Utf8ToWide(library->embedding_provider) + L"\r\n";
    details += L"Embedding base URL: " + Utf8ToWide(library->embedding_base_url) + L"\r\n";
    details += L"Embedding model: " + Utf8ToWide(library->embedding_model) + L"\r\n";
    details += L"Embedding dimensions: " + std::to_wstring(library->embedding_dimensions) + L"\r\n";
    details += L"Vector backend: " + Utf8ToWide(library->vector_backend) + L"\r\n";
    if (library->rebuild_required) {
        details += L"Rebuild required: yes\r\n";
        details += L"Reason: " + Utf8ToWide(library->rebuild_reason) + L"\r\n";
    } else {
        details += L"Rebuild required: no\r\n";
    }
    details += L"Documents: " + std::to_wstring(stats.document_count) + L"\r\n";
    details += L"Chunks: " + std::to_wstring(stats.chunk_count) + L"\r\n";
    details += L"Embeddings: " + std::to_wstring(stats.embedding_count) + L"\r\n";
    details += L"Original bytes: " + std::to_wstring(stats.original_bytes) + L"\r\n";
    details += L"Path: " + Utf8ToWide(library->storage_path) + L"\r\n";
    if (!project_id.empty()) {
        details += L"\r\nActive project binding: ";
        if (binding) {
            details += binding->enabled ? L"enabled" : L"disabled";
            details += binding->can_read ? L", read" : L", no read";
            details += binding->can_write ? L", write" : L", no write";
            details += binding->can_delete ? L", delete" : L", no delete";
            details += L", priority " + std::to_wstring(binding->retrieval_priority);
            details += L", max chunks " + std::to_wstring(binding->max_chunks);
        } else {
            details += L"not attached";
        }
    }
    SetWindowTextW(details_edit_, details.c_str());
}

void RagServiceManagerWindow::UpdateStatus(const std::wstring& text) const {
    SetWindowTextW(status_label_, text.c_str());
}

void RagServiceManagerWindow::AddLibrary() {
    RagLibraryConfig config;
    config.name = "New RAG Library";
    config.storage_path = WideToUtf8(std::filesystem::absolute(rag_service_->RagRoot()).wstring());
    const auto edited = RagLibraryEditorDialog::Show(hwnd_, rag_service_, config, false);
    if (!edited) {
        return;
    }
    const RagLibraryConfig created = rag_service_->CreateLibrary(*edited);
    RefreshLibraries();
    UpdateStatus(L"Created RAG library: " + Utf8ToWide(created.name) + L" at " + Utf8ToWide(created.storage_path));
}

void RagServiceManagerWindow::EditLibrary() {
    const RagLibraryConfig* selected = SelectedLibrary();
    if (!selected) {
        return;
    }
    const auto edited = RagLibraryEditorDialog::Show(hwnd_, rag_service_, *selected, true);
    if (!edited) {
        return;
    }

    std::string error;
    if (!rag_service_->UpdateLibrary(*edited, &error)) {
        MessageBoxW(hwnd_, Utf8ToWide(error).c_str(), L"Save Failed", MB_OK | MB_ICONERROR);
        return;
    }
    RefreshLibraries();
    UpdateStatus(L"RAG library saved.");
}

void RagServiceManagerWindow::RemoveLibrary() {
    const RagLibraryConfig* selected = SelectedLibrary();
    if (!selected) {
        return;
    }
    const std::wstring prompt = L"Remove RAG library \"" + Utf8ToWide(selected->name) + L"\" and its managed documents/index data?";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"Remove RAG Library", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    std::string error;
    if (!rag_service_->DeleteLibrary(selected->id, &error)) {
        MessageBoxW(hwnd_, Utf8ToWide(error).c_str(), L"Remove Failed", MB_OK | MB_ICONERROR);
        return;
    }
    RefreshLibraries();
    UpdateStatus(L"RAG library removed.");
}

void RagServiceManagerWindow::AttachSelected(bool read_write) {
    const RagLibraryConfig* selected = SelectedLibrary();
    if (!selected) {
        return;
    }
    const std::string project_id = ActiveProjectId();
    if (project_id.empty()) {
        MessageBoxW(hwnd_, L"Select a project first.", L"No Active Project", MB_OK | MB_ICONINFORMATION);
        return;
    }

    ProjectRagBinding binding;
    binding.rag_id = selected->id;
    binding.enabled = true;
    binding.can_read = true;
    binding.can_write = read_write;
    binding.can_delete = false;
    binding.default_ingest_target = read_write;
    binding.retrieval_priority = read_write ? 20 : 10;
    binding.max_chunks = selected->default_max_chunks;

    std::string error;
    if (!rag_service_->UpsertProjectBinding(project_id, binding, &error)) {
        MessageBoxW(hwnd_, Utf8ToWide(error).c_str(), L"Attach Failed", MB_OK | MB_ICONERROR);
        return;
    }
    RefreshLibraries();
    UpdateStatus(read_write ? L"Attached RAG to project as read/write." : L"Attached RAG to project as read-only.");
}

void RagServiceManagerWindow::DetachSelected() {
    const RagLibraryConfig* selected = SelectedLibrary();
    if (!selected) {
        return;
    }
    std::string error;
    if (!rag_service_->RemoveProjectBinding(ActiveProjectId(), selected->id, &error)) {
        MessageBoxW(hwnd_, Utf8ToWide(error).c_str(), L"Detach Failed", MB_OK | MB_ICONERROR);
        return;
    }
    RefreshLibraries();
    UpdateStatus(L"Detached RAG from active project.");
}

bool RagServiceManagerWindow::ActiveProjectCanWrite(const std::string& rag_id) const {
    const std::string project_id = ActiveProjectId();
    if (project_id.empty()) {
        return true;
    }
    const auto bindings = rag_service_->LoadProjectBindings(project_id);
    const auto binding = FindBinding(bindings, rag_id);
    return binding && binding->enabled && binding->can_write;
}

void RagServiceManagerWindow::IngestFiles() {
    const RagLibraryConfig* selected = SelectedLibrary();
    if (!selected) {
        return;
    }
    if (!ActiveProjectCanWrite(selected->id)) {
        MessageBoxW(hwnd_, L"The active project does not have write access to this RAG. Attach it as read/write first.", L"Read-Only RAG", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto files = PickFiles(hwnd_);
    if (files.empty()) {
        return;
    }

    const RagImportPreview preview = rag_service_->PreviewFiles(selected->id, files);
    SetWindowTextW(results_edit_, ImportPreviewText(preview).c_str());
    if (preview.supported_files <= 0) {
        UpdateStatus(L"Import preview found no supported files to ingest.");
        return;
    }
    const std::wstring prompt = L"Import preview found " + std::to_wstring(preview.supported_files) +
        L" supported file(s) and " + std::to_wstring(preview.skipped_files) +
        L" skipped file(s).\r\n\r\nStart ingestion now?";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"Start RAG Ingestion", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        UpdateStatus(L"Ingestion cancelled after preview.");
        return;
    }

    const std::string rag_id = selected->id;
    EnableWindow(ingest_button_, FALSE);
    EnableWindow(ingest_folder_button_, FALSE);
    EnableWindow(rebuild_button_, FALSE);
    EnableWindow(documents_button_, FALSE);
    EnableWindow(reindex_document_button_, FALSE);
    EnableWindow(delete_document_button_, FALSE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, 0, 0);
    UpdateStatus(L"Ingesting files...");
    std::thread([hwnd = hwnd_, service = rag_service_, rag_id, files]() {
        auto* payload = new IngestionPayload;
        payload->result = service->IngestFiles(rag_id, files);
        if (!PostMessageW(hwnd, kIngestionFinishedMessage, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

void RagServiceManagerWindow::IngestFolder() {
    const RagLibraryConfig* selected = SelectedLibrary();
    if (!selected) {
        return;
    }
    if (!ActiveProjectCanWrite(selected->id)) {
        MessageBoxW(hwnd_, L"The active project does not have write access to this RAG. Attach it as read/write first.", L"Read-Only RAG", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto folder = PickFolder(hwnd_, L"Select a folder to ingest into this RAG library.");
    if (!folder) {
        return;
    }
    const int recursive_choice = MessageBoxW(hwnd_, L"Include files in subfolders?", L"Ingest Folder", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (recursive_choice == IDCANCEL) {
        return;
    }
    const bool recursive = recursive_choice == IDYES;

    const RagImportPreview preview = rag_service_->PreviewFolder(selected->id, *folder, recursive);
    SetWindowTextW(results_edit_, ImportPreviewText(preview).c_str());
    if (preview.supported_files <= 0) {
        UpdateStatus(L"Import preview found no supported files to ingest.");
        return;
    }
    const std::wstring prompt = L"Import preview found " + std::to_wstring(preview.supported_files) +
        L" supported file(s) and " + std::to_wstring(preview.skipped_files) +
        L" skipped file(s).\r\n\r\nStart ingestion now?";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"Start RAG Folder Ingestion", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        UpdateStatus(L"Ingestion cancelled after preview.");
        return;
    }

    const std::string rag_id = selected->id;
    EnableWindow(ingest_button_, FALSE);
    EnableWindow(ingest_folder_button_, FALSE);
    EnableWindow(rebuild_button_, FALSE);
    EnableWindow(documents_button_, FALSE);
    EnableWindow(reindex_document_button_, FALSE);
    EnableWindow(delete_document_button_, FALSE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, 0, 0);
    UpdateStatus(recursive ? L"Ingesting folder recursively..." : L"Ingesting folder...");
    std::thread([hwnd = hwnd_, service = rag_service_, rag_id, folder = *folder, recursive]() {
        auto* payload = new IngestionPayload;
        payload->result = service->IngestFolder(rag_id, folder, recursive);
        if (!PostMessageW(hwnd, kIngestionFinishedMessage, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

void RagServiceManagerWindow::RebuildSelected() {
    const RagLibraryConfig* selected = SelectedLibrary();
    if (!selected) {
        return;
    }
    if (!ActiveProjectCanWrite(selected->id)) {
        MessageBoxW(hwnd_, L"The active project does not have write access to this RAG. Attach it as read/write first.", L"Read-Only RAG", MB_OK | MB_ICONWARNING);
        return;
    }

    const std::wstring prompt = L"Rebuild the database for \"" + Utf8ToWide(selected->name) + L"\" from its saved original documents?\r\n\r\nThis clears the RAG document/chunk tables first, then re-ingests the saved originals as a fresh database. This can take a while.";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"Rebuild RAG", MB_YESNO | MB_ICONINFORMATION) != IDYES) {
        return;
    }

    const std::string rag_id = selected->id;
    EnableWindow(ingest_button_, FALSE);
    EnableWindow(ingest_folder_button_, FALSE);
    EnableWindow(rebuild_button_, FALSE);
    EnableWindow(documents_button_, FALSE);
    EnableWindow(reindex_document_button_, FALSE);
    EnableWindow(delete_document_button_, FALSE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, 0, 0);
    UpdateStatus(L"Rebuilding RAG index...");

    std::thread([hwnd = hwnd_, service = rag_service_, rag_id]() {
        auto* payload = new IngestionPayload;
        payload->rebuild = true;
        payload->result = service->RebuildLibrary(rag_id, [hwnd](const RagProgressUpdate& progress) {
            auto* progress_payload = new ProgressPayload;
            progress_payload->progress = progress;
            if (!PostMessageW(hwnd, kRebuildProgressMessage, 0, reinterpret_cast<LPARAM>(progress_payload))) {
                delete progress_payload;
            }
        });
        if (!PostMessageW(hwnd, kIngestionFinishedMessage, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

void RagServiceManagerWindow::BrowseDocuments() {
    const RagLibraryConfig* selected = SelectedLibrary();
    if (!selected) {
        SetWindowTextW(results_edit_, L"Select a RAG library first.");
        return;
    }

    const std::vector<RagDocumentSummary> documents = rag_service_->ListDocuments(selected->id);
    SetWindowTextW(results_edit_, DocumentBrowserText(*selected, documents).c_str());
    SendMessageW(results_edit_, EM_SETSEL, 0, 0);
    SendMessageW(results_edit_, WM_VSCROLL, SB_TOP, 0);
    UpdateStatus(L"Loaded " + std::to_wstring(documents.size()) + L" RAG documents.");
}

void RagServiceManagerWindow::ReindexDocument() {
    const RagLibraryConfig* selected = SelectedLibrary();
    if (!selected) {
        return;
    }
    if (!ActiveProjectCanWrite(selected->id)) {
        MessageBoxW(hwnd_, L"The active project does not have write access to this RAG. Attach it as read/write first.", L"Read-Only RAG", MB_OK | MB_ICONWARNING);
        return;
    }

    PromptOptions options;
    options.title = L"Reindex RAG Document";
    options.label = L"Paste the Document ID to reindex. Use Browse Docs to copy the ID.";
    options.width = 560;
    const auto document_id = ShowPromptDialog(hwnd_, options);
    if (!document_id || TrimWide(*document_id).empty()) {
        return;
    }

    const std::string rag_id = selected->id;
    const std::string doc_id = WideToUtf8(TrimWide(*document_id));
    EnableWindow(ingest_button_, FALSE);
    EnableWindow(ingest_folder_button_, FALSE);
    EnableWindow(rebuild_button_, FALSE);
    EnableWindow(documents_button_, FALSE);
    EnableWindow(reindex_document_button_, FALSE);
    EnableWindow(delete_document_button_, FALSE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, 0, 0);
    UpdateStatus(L"Reindexing document...");

    std::thread([hwnd = hwnd_, service = rag_service_, rag_id, doc_id]() {
        auto* payload = new IngestionPayload;
        payload->result = service->ReindexDocument(rag_id, doc_id);
        if (!PostMessageW(hwnd, kIngestionFinishedMessage, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

void RagServiceManagerWindow::DeleteDocument() {
    const RagLibraryConfig* selected = SelectedLibrary();
    if (!selected) {
        return;
    }
    if (!ActiveProjectCanWrite(selected->id)) {
        MessageBoxW(hwnd_, L"The active project does not have write access to this RAG. Attach it as read/write first.", L"Read-Only RAG", MB_OK | MB_ICONWARNING);
        return;
    }

    PromptOptions options;
    options.title = L"Delete RAG Document";
    options.label = L"Paste the Document ID to delete from this RAG. Use Browse Docs to copy the ID.";
    options.width = 560;
    const auto document_id = ShowPromptDialog(hwnd_, options);
    if (!document_id || TrimWide(*document_id).empty()) {
        return;
    }

    const int file_choice = MessageBoxW(
        hwnd_,
        L"Delete this document from the RAG database?\r\n\r\nYes: also remove the managed original/extracted copies.\r\nNo: remove the database entries but keep managed files on disk.\r\nCancel: do nothing.",
        L"Delete RAG Document",
        MB_YESNOCANCEL | MB_ICONWARNING);
    if (file_choice == IDCANCEL) {
        return;
    }

    std::string error;
    const bool delete_files = file_choice == IDYES;
    if (!rag_service_->DeleteDocument(selected->id, WideToUtf8(TrimWide(*document_id)), delete_files, &error)) {
        MessageBoxW(hwnd_, Utf8ToWide(error).c_str(), L"Delete Document Failed", MB_OK | MB_ICONERROR);
        return;
    }
    RefreshDetails();
    BrowseDocuments();
    UpdateStatus(delete_files ? L"Document deleted and managed files removed." : L"Document deleted from the RAG database.");
}

void RagServiceManagerWindow::ShowExtractionTools() {
    const auto tools = rag_service_->GetExtractionToolStatus();
    SetWindowTextW(results_edit_, ExtractionToolsText(tools).c_str());
    SendMessageW(results_edit_, EM_SETSEL, 0, 0);
    SendMessageW(results_edit_, WM_VSCROLL, SB_TOP, 0);

    bool missing_recommended = false;
    for (const auto& tool : tools) {
        if (!tool.installed && tool.recommended && tool.installable) {
            missing_recommended = true;
            break;
        }
    }
    if (!missing_recommended) {
        UpdateStatus(L"RAG extraction tools checked. No missing recommended installable tools.");
        return;
    }

    const int choice = MessageBoxW(
        hwnd_,
        L"One or more recommended RAG extraction tools are missing.\r\n\r\nInstall missing recommended tools now?\r\n\r\nThis opens a visible command window and uses winget where available.",
        L"Install RAG Tools",
        MB_YESNO | MB_ICONQUESTION);
    if (choice != IDYES) {
        UpdateStatus(L"RAG extraction tools checked.");
        return;
    }

    const RagExtractionToolInstallResult result = rag_service_->LaunchExtractionToolInstaller(true);
    std::wstring text = ExtractionToolsText(tools);
    text += L"\r\nInstaller launch result:\r\n";
    text += Utf8ToWide(result.message) + L"\r\n";
    if (!result.command.empty()) {
        text += L"Command: " + Utf8ToWide(result.command) + L"\r\n";
    }
    SetWindowTextW(results_edit_, text.c_str());
    UpdateStatus(result.launched ? L"RAG tool installer launched." : L"No RAG tool installer was launched.");
}

void RagServiceManagerWindow::OnRebuildProgress(ProgressPayload* payload) {
    std::unique_ptr<ProgressPayload> guard(payload);
    const int total = std::max(1, payload->progress.total_items);
    const int processed = std::clamp(payload->progress.processed_items, 0, total);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, total);
    SendMessageW(progress_bar_, PBM_SETPOS, processed, 0);

    std::wstring status = Utf8ToWide(payload->progress.stage);
    status += L" ";
    status += std::to_wstring(processed) + L"/" + std::to_wstring(total);
    if (!payload->progress.current_item.empty()) {
        status += L": " + Utf8ToWide(payload->progress.current_item);
    }
    UpdateStatus(status);
}

void RagServiceManagerWindow::OnIngestionFinished(IngestionPayload* payload) {
    std::unique_ptr<IngestionPayload> guard(payload);
    EnableWindow(ingest_button_, TRUE);
    EnableWindow(ingest_folder_button_, TRUE);
    EnableWindow(rebuild_button_, TRUE);
    EnableWindow(documents_button_, TRUE);
    EnableWindow(reindex_document_button_, TRUE);
    EnableWindow(delete_document_button_, TRUE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, payload->rebuild ? 100 : 0, 0);
    SetWindowTextW(results_edit_, IngestionSummary(payload->result, payload->rebuild).c_str());
    RefreshLibraries();
    if (payload->rebuild) {
        UpdateStatus(payload->result.errors.empty() ? L"Rebuild complete." : L"Rebuild finished with warnings.");
    } else {
        UpdateStatus(payload->result.errors.empty() ? L"Ingestion complete." : L"Ingestion finished with warnings.");
    }
}

void RagServiceManagerWindow::Search() {
    const std::string query = WideToUtf8(TrimWide(GetWindowTextString(search_edit_)));
    if (query.empty()) {
        return;
    }

    std::vector<RagQueryResult> results;
    const std::string project_id = ActiveProjectId();
    if (!project_id.empty()) {
        results = rag_service_->QueryProject(project_id, query, 20);
    } else if (const RagLibraryConfig* selected = SelectedLibrary()) {
        results = rag_service_->QueryRag(selected->id, query, 20);
    }

    std::wstring text;
    if (results.empty()) {
        text = L"No RAG results found.";
    } else {
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& result = results[i];
            text += L"Result " + std::to_wstring(i + 1) + L"\r\n";
            text += L"RAG: " + Utf8ToWide(result.rag_name) + L"\r\n";
            text += L"Source: " + Utf8ToWide(result.document_title) + L"\r\n";
            text += L"Method: " + Utf8ToWide(result.retrieval_method) + L"\r\n";
            text += L"Score: " + Utf8ToWide(std::to_string(result.score)) + L"\r\n";
            if (!result.last_indexed_at.empty()) {
                text += L"Last indexed: " + Utf8ToWide(result.last_indexed_at) + L"\r\n";
            }
            if (!result.source_path.empty()) {
                text += L"Path: " + Utf8ToWide(result.source_path) + L"\r\n";
            }
            if (!result.metadata_json.empty()) {
                text += L"Metadata: " + Utf8ToWide(result.metadata_json) + L"\r\n";
            }
            text += Utf8ToWide(result.text) + L"\r\n\r\n";
        }
    }
    SetWindowTextW(results_edit_, text.c_str());
    SendMessageW(results_edit_, EM_SETSEL, 0, 0);
    SendMessageW(results_edit_, WM_VSCROLL, SB_TOP, 0);
    UpdateStatus(L"Search complete.");
}

void RagServiceManagerWindow::OnCommand(int control_id, int notification_code) {
    switch (control_id) {
    case kLibrariesList:
        if (notification_code == LBN_SELCHANGE) {
            RefreshDetails();
        }
        break;
    case kAddLibrary:
        AddLibrary();
        break;
    case kEditLibrary:
        EditLibrary();
        break;
    case kRemoveLibrary:
        RemoveLibrary();
        break;
    case kAttachReadOnly:
        AttachSelected(false);
        break;
    case kAttachReadWrite:
        AttachSelected(true);
        break;
    case kDetachProject:
        DetachSelected();
        break;
    case kInstallTools:
        ShowExtractionTools();
        break;
    case kIngestFiles:
        IngestFiles();
        break;
    case kIngestFolder:
        IngestFolder();
        break;
    case kRebuildLibrary:
        RebuildSelected();
        break;
    case kBrowseDocuments:
        BrowseDocuments();
        break;
    case kReindexDocument:
        ReindexDocument();
        break;
    case kDeleteDocument:
        DeleteDocument();
        break;
    case kSearchButton:
        Search();
        break;
    case kCloseButton:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

}  // namespace

HWND CreateRagServiceManagerWindow(HWND owner, RagService* rag_service, std::function<std::string()> active_project_id_provider) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    auto* window = new RagServiceManagerWindow(owner, rag_service, std::move(active_project_id_provider));
    return window->Create(instance);
}
