#include "rag_service_manager.h"

#include "prompt_dialog.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr wchar_t kRagManagerClassName[] = L"AgentRagServiceManagerWindow";
constexpr wchar_t kRagLibraryEditorClassName[] = L"AgentRagLibraryEditorWindow";
constexpr wchar_t kRagImageIngestSettingsClassName[] = L"AgentRagImageIngestSettingsWindow";
constexpr UINT kIngestionFinishedMessage = WM_APP + 70;
constexpr UINT kRebuildProgressMessage = WM_APP + 71;
constexpr UINT kExportFinishedMessage = WM_APP + 72;
constexpr UINT kImportFinishedMessage = WM_APP + 73;

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
    kImageIngestSettings = 6121,
    kViewJobs = 6122,
    kReattachRag = 6123,
    kExportRag = 6124,
    kImportRag = 6125,
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

enum ImageIngestSettingsControlId : int {
    kImageEnabled = 6301,
    kImageCpuMode = 6302,
    kImagePaddleMode = 6303,
    kImageVisionMode = 6304,
    kImageRemoteMode = 6334,
    kImageTesseractLanguageLabel = 6305,
    kImageTesseractLanguageEdit = 6306,
    kImagePaddlePythonLabel = 6307,
    kImagePaddlePythonEdit = 6308,
    kImagePaddleLanguageLabel = 6309,
    kImagePaddleLanguageEdit = 6310,
    kImageVisionProviderLabel = 6311,
    kImageVisionProviderCombo = 6312,
    kImageProviderVisionModelLabel = 6341,
    kImageProviderVisionModelCombo = 6342,
    kImageVisionBaseUrlLabel = 6313,
    kImageVisionBaseUrlEdit = 6314,
    kImageVisionModelLabel = 6315,
    kImageVisionModelEdit = 6316,
    kImageVisionPromptLabel = 6317,
    kImageVisionPromptEdit = 6318,
    kImageOllamaInstanceCountLabel = 6319,
    kImageOllamaInstanceCountEdit = 6320,
    kImageOllamaStartPortLabel = 6321,
    kImageOllamaStartPortEdit = 6322,
    kImageIncludeOcr = 6323,
    kImageIncludeVisualDescription = 6324,
    kImageStatus = 6325,
    kImageCheckStatus = 6326,
    kImageInstallTesseract = 6327,
    kImageInstallPaddle = 6328,
    kImageInstallOllama = 6329,
    kImagePullVisionModel = 6330,
    kImageDiagnosticsLog = 6331,
    kImageVisionModelHelp = 6332,
    kImageOllamaStartLocally = 6333,
    kImageRemoteAgentUrlLabel = 6335,
    kImageRemoteAgentUrlEdit = 6336,
    kImageRemoteAgentPortLabel = 6337,
    kImageRemoteAgentPortEdit = 6338,
    kImageRemoteAgentLoadJson = 6339,
    kImageRemoteAgentJsonStatus = 6340,
    kImageSaveButton = IDOK,
    kImageCancelButton = IDCANCEL,
};

struct IngestionPayload {
    RagIngestionResult result;
    bool rebuild = false;
    std::string job_id; // for persistent job tracking
};

struct ProgressPayload {
    RagProgressUpdate progress;
};

struct ExportPayload {
    RagExportResult result;
    std::string rag_name;
};

struct ImportPayload {
    RagImportResult result;
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
    if (!result.warnings.empty()) {
        text += L"\r\nWarnings:\r\n";
        for (const auto& warning : result.warnings) {
            text += L"- " + Utf8ToWide(warning) + L"\r\n";
        }
    }
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
    dialog.lpstrFilter = L"RAG Supported Files\0*.txt;*.md;*.json;*.csv;*.log;*.xml;*.cpp;*.c;*.h;*.hpp;*.cs;*.js;*.ts;*.tsx;*.jsx;*.py;*.ps1;*.bat;*.cmd;*.ini;*.toml;*.yaml;*.yml;*.html;*.htm;*.docx;*.docm;*.xlsx;*.xlsm;*.pdf;*.css;*.sql;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp\0All Files\0*.*\0";

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

std::optional<std::filesystem::path> PickRemoteImageAgentJson(HWND owner) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrTitle = L"Load Remote Agent Worker JSON";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    dialog.lpstrFilter = L"Remote Agent Worker JSON\0*.json\0All Files\0*.*\0";
    if (!GetOpenFileNameW(&dialog)) {
        return std::nullopt;
    }
    return std::filesystem::path(path);
}

struct RemoteImageAgentConfigInfo {
    std::string raw_json;
    std::string worker_name = "Remote Ollama Worker";
    std::string model_name = "qwen2.5vl:7b";
    std::string shared_secret;
    std::string certificate_fingerprint;
    int https_port = 8765;
    int ollama_start_port = 11434;
    int ollama_instance_count = 1;
};

int ClampImageRemotePort(int value, int fallback) {
    return std::clamp(value <= 0 ? fallback : value, 1, 65535);
}

int ClampImageRemoteInstanceCount(int value) {
    return std::clamp(value <= 0 ? 1 : value, 1, 32);
}

std::optional<RemoteImageAgentConfigInfo> ReadRemoteImageAgentConfig(
    const std::filesystem::path& path,
    std::string* error) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            if (error) {
                *error = "Could not open the selected remote worker JSON file.";
            }
            return std::nullopt;
        }
        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto data = nlohmann::json::parse(text);

        RemoteImageAgentConfigInfo info;
        info.raw_json = data.dump(2);
        info.worker_name = data.value("worker_name", info.worker_name);
        if (data.contains("agent_server") && data["agent_server"].is_object()) {
            const auto& server = data["agent_server"];
            info.https_port = ClampImageRemotePort(server.value("https_port", info.https_port), 8765);
            info.shared_secret = server.value("shared_secret", "");
            info.certificate_fingerprint = server.value("certificate_fingerprint", "");
        }
        if (data.contains("model") && data["model"].is_object()) {
            info.model_name = data["model"].value("name", info.model_name);
        } else {
            info.model_name = data.value("vision_model", info.model_name);
        }
        if (data.contains("ollama") && data["ollama"].is_object()) {
            info.ollama_start_port = ClampImageRemotePort(data["ollama"].value("start_port", info.ollama_start_port), 11434);
            info.ollama_instance_count = ClampImageRemoteInstanceCount(data["ollama"].value("instance_count", info.ollama_instance_count));
        } else {
            info.ollama_start_port = ClampImageRemotePort(data.value("ollama_start_port", info.ollama_start_port), 11434);
            info.ollama_instance_count = ClampImageRemoteInstanceCount(data.value("ollama_instance_count", info.ollama_instance_count));
        }
        return info;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
    }
    return std::nullopt;
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

std::string NormalizeImageMode(std::string mode) {
    mode = Trim(mode);
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (mode == "paddle" || mode == "paddleocr" || mode == "paddle_ocr" || mode == "paddle_ocr_gpu") {
        return "paddle_ocr_gpu";
    }
    if (mode == "vision" || mode == "vlm" || mode == "vision_language" || mode == "vision_language_gpu") {
        return "vision_language_gpu";
    }
    if (mode == "remote" || mode == "remote_agent" || mode == "agent_remote" || mode == "agent_https") {
        return "vision_language_gpu";
    }
    return "tesseract_cpu";
}

int ImageVisionProviderComboIndex(const std::string& provider) {
    std::string normalized = Trim(provider);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (normalized == "ollama") {
        return 1;
    }
    if (normalized == "provider" || normalized == "providers" || normalized == "provider_model" ||
        normalized == "remote" || normalized == "remote_agent" || normalized == "agent_remote" || normalized == "agent_https") {
        return 2;
    }
    return 0;
}

std::string ImageVisionProviderFromComboIndex(int index) {
    if (index == 1) {
        return "ollama";
    }
    if (index == 2) {
        return "provider";
    }
    return "none";
}

std::wstring ImageStatusText(const RagImageIngestRuntimeStatus& status) {
    std::wstring text = L"Image ingest status: " + Utf8ToWide(status.message);
    if (!status.log_path.empty()) {
        text += L" | Log: " + Utf8ToWide(status.log_path);
    }
    return text;
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

class RagImageIngestSettingsDialog {
public:
    static bool Show(HWND owner, RagService* rag_service);

private:
    RagImageIngestSettingsDialog(HWND owner, RagService* rag_service, RagImageIngestSettings settings)
        : owner_(owner), rag_service_(rag_service), settings_(std::move(settings)) {}

    static void RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void OnCreate();
    void LayoutControls() const;
    void OnCommand(int control_id, int notification_code);
    void CommitCurrentModelEditToSettings();
    void RefreshVisionModelField(bool force_reload);
    void RefreshProviderVisionModelChoices();
    std::optional<RagVisionModelOption> SelectedProviderVisionModelOption() const;
    RagImageIngestSettings BuildSettingsFromFields() const;
    void LoadSettingsIntoFields();
    void LoadRemoteAgentJson();
    void UpdateModeControlStates() const;
    void RefreshStatus();
    void InstallTool(const std::string& tool_id);
    void PullVisionModel();
    bool ValidateAndSave();

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    RagService* rag_service_ = nullptr;
    HFONT font_ = nullptr;
    RagImageIngestSettings settings_;
    bool saved_ = false;

    HWND enabled_checkbox_ = nullptr;
    HWND cpu_radio_ = nullptr;
    HWND paddle_radio_ = nullptr;
    HWND vision_radio_ = nullptr;
    HWND tesseract_language_label_ = nullptr;
    HWND tesseract_language_edit_ = nullptr;
    HWND paddle_python_label_ = nullptr;
    HWND paddle_python_edit_ = nullptr;
    HWND paddle_language_label_ = nullptr;
    HWND paddle_language_edit_ = nullptr;
    HWND vision_provider_label_ = nullptr;
    HWND vision_provider_combo_ = nullptr;
    HWND provider_vision_model_label_ = nullptr;
    HWND provider_vision_model_combo_ = nullptr;
    HWND vision_base_url_label_ = nullptr;
    HWND vision_base_url_edit_ = nullptr;
    HWND vision_model_label_ = nullptr;
    HWND vision_model_edit_ = nullptr;
    HWND ollama_instance_count_label_ = nullptr;
    HWND ollama_instance_count_edit_ = nullptr;
    HWND ollama_start_port_label_ = nullptr;
    HWND ollama_start_port_edit_ = nullptr;
    HWND ollama_start_locally_checkbox_ = nullptr;
    HWND remote_agent_url_label_ = nullptr;
    HWND remote_agent_url_edit_ = nullptr;
    HWND remote_agent_port_label_ = nullptr;
    HWND remote_agent_port_edit_ = nullptr;
    HWND remote_agent_load_json_button_ = nullptr;
    HWND remote_agent_json_status_label_ = nullptr;
    HWND vision_prompt_label_ = nullptr;
    HWND vision_prompt_edit_ = nullptr;
    HWND include_ocr_checkbox_ = nullptr;
    HWND include_visual_description_checkbox_ = nullptr;
    HWND status_label_ = nullptr;
    HWND check_status_button_ = nullptr;
    HWND install_tesseract_button_ = nullptr;
    HWND install_paddle_button_ = nullptr;
    HWND install_ollama_button_ = nullptr;
    HWND pull_vision_model_button_ = nullptr;
    HWND diagnostics_log_edit_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;
    std::vector<RagVisionModelOption> provider_vision_models_;
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
    void ShowImageIngestSettings();
    void ShowJobs();
    void ReattachLibrary();
    void ExportLibrary();
    void ImportLibrary();
    void OnExportFinished(ExportPayload* payload);
    void OnImportFinished(ImportPayload* payload);
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
    HWND image_ingest_settings_button_ = nullptr;
    HWND view_jobs_button_ = nullptr;
    HWND reattach_button_ = nullptr;
    HWND export_rag_button_ = nullptr;
    HWND import_rag_button_ = nullptr;
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

bool RagImageIngestSettingsDialog::Show(HWND owner, RagService* rag_service) {
    if (!rag_service) {
        return false;
    }

    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    RegisterWindowClass(instance);
    auto* dialog = new RagImageIngestSettingsDialog(owner, rag_service, rag_service->LoadImageIngestSettings());
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE;
    const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

    if (owner) {
        EnableWindow(owner, FALSE);
    }

    dialog->hwnd_ = CreateWindowExW(
        ex_style,
        kRagImageIngestSettingsClassName,
        L"Image Ingest Settings",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        780,
        880,
        owner,
        nullptr,
        instance,
        dialog);

    if (!dialog->hwnd_) {
        if (owner) {
            EnableWindow(owner, TRUE);
        }
        delete dialog;
        return false;
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

    const bool saved = dialog->saved_;
    delete dialog;
    return saved;
}

void RagImageIngestSettingsDialog::RegisterWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &RagImageIngestSettingsDialog::WindowProc;
    wc.lpszClassName = kRagImageIngestSettingsClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}

LRESULT CALLBACK RagImageIngestSettingsDialog::WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<RagImageIngestSettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<RagImageIngestSettingsDialog*>(create->lpCreateParams);
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
        self->OnCommand(LOWORD(w_param), HIWORD(w_param));
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

void RagImageIngestSettingsDialog::OnCreate() {
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    enabled_checkbox_ = CreateWindowExW(0, L"BUTTON", L"Enable image ingestion for all RAG libraries", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageEnabled), nullptr, nullptr);
    cpu_radio_ = CreateWindowExW(0, L"BUTTON", L"CPU default: Tesseract OCR only", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageCpuMode), nullptr, nullptr);
    paddle_radio_ = CreateWindowExW(0, L"BUTTON", L"GPU OCR: PaddleOCR, with Tesseract fallback", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImagePaddleMode), nullptr, nullptr);
    vision_radio_ = CreateWindowExW(0, L"BUTTON", L"Full vision: OCR plus model-generated image description", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageVisionMode), nullptr, nullptr);
    tesseract_language_label_ = CreateWindowExW(0, L"STATIC", L"Tesseract language", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageTesseractLanguageLabel), nullptr, nullptr);
    tesseract_language_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageTesseractLanguageEdit), nullptr, nullptr);
    paddle_python_label_ = CreateWindowExW(0, L"STATIC", L"PaddleOCR Python command", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImagePaddlePythonLabel), nullptr, nullptr);
    paddle_python_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImagePaddlePythonEdit), nullptr, nullptr);
    paddle_language_label_ = CreateWindowExW(0, L"STATIC", L"PaddleOCR language", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImagePaddleLanguageLabel), nullptr, nullptr);
    paddle_language_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImagePaddleLanguageEdit), nullptr, nullptr);
    vision_provider_label_ = CreateWindowExW(0, L"STATIC", L"Vision provider", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageVisionProviderLabel), nullptr, nullptr);
    vision_provider_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageVisionProviderCombo), nullptr, nullptr);
    provider_vision_model_label_ = CreateWindowExW(0, L"STATIC", L"Provider vision model", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageProviderVisionModelLabel), nullptr, nullptr);
    provider_vision_model_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageProviderVisionModelCombo), nullptr, nullptr);
    vision_base_url_label_ = CreateWindowExW(0, L"STATIC", L"Vision host / base URL", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageVisionBaseUrlLabel), nullptr, nullptr);
    vision_base_url_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageVisionBaseUrlEdit), nullptr, nullptr);
    vision_model_label_ = CreateWindowExW(0, L"BUTTON", L"Vision model", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageVisionModelLabel), nullptr, nullptr);
    vision_model_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageVisionModelEdit), nullptr, nullptr);
    ollama_instance_count_label_ = CreateWindowExW(0, L"STATIC", L"Ollama worker instances", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageOllamaInstanceCountLabel), nullptr, nullptr);
    ollama_instance_count_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageOllamaInstanceCountEdit), nullptr, nullptr);
    ollama_start_port_label_ = CreateWindowExW(0, L"STATIC", L"Ollama starting port", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageOllamaStartPortLabel), nullptr, nullptr);
    ollama_start_port_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageOllamaStartPortEdit), nullptr, nullptr);
    ollama_start_locally_checkbox_ = CreateWindowExW(0, L"BUTTON", L"Start Ollama locally when needed", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageOllamaStartLocally), nullptr, nullptr);
    vision_prompt_label_ = CreateWindowExW(0, L"STATIC", L"Vision description prompt", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageVisionPromptLabel), nullptr, nullptr);
    vision_prompt_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageVisionPromptEdit), nullptr, nullptr);
    include_ocr_checkbox_ = CreateWindowExW(0, L"BUTTON", L"Include OCR text in extracted Markdown", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageIncludeOcr), nullptr, nullptr);
    include_visual_description_checkbox_ = CreateWindowExW(0, L"BUTTON", L"Include visual description in extracted Markdown when full vision mode is selected", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageIncludeVisualDescription), nullptr, nullptr);
    status_label_ = CreateWindowExW(0, L"STATIC", L"Image ingest status: not checked.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageStatus), nullptr, nullptr);
    check_status_button_ = CreateWindowExW(0, L"BUTTON", L"Check Status", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageCheckStatus), nullptr, nullptr);
    install_tesseract_button_ = CreateWindowExW(0, L"BUTTON", L"Install Tesseract", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageInstallTesseract), nullptr, nullptr);
    install_paddle_button_ = CreateWindowExW(0, L"BUTTON", L"Install PaddleOCR", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageInstallPaddle), nullptr, nullptr);
    install_ollama_button_ = CreateWindowExW(0, L"BUTTON", L"Install Ollama", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageInstallOllama), nullptr, nullptr);
    pull_vision_model_button_ = CreateWindowExW(0, L"BUTTON", L"Pull Vision Model", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImagePullVisionModel), nullptr, nullptr);
    diagnostics_log_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageDiagnosticsLog), nullptr, nullptr);
    save_button_ = CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageSaveButton), nullptr, nullptr);
    cancel_button_ = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageCancelButton), nullptr, nullptr);

    for (HWND control : {enabled_checkbox_, cpu_radio_, paddle_radio_, vision_radio_, tesseract_language_label_, tesseract_language_edit_, paddle_python_label_, paddle_python_edit_, paddle_language_label_, paddle_language_edit_, vision_provider_label_, vision_provider_combo_, provider_vision_model_label_, provider_vision_model_combo_, vision_base_url_label_, vision_base_url_edit_, vision_model_label_, vision_model_edit_, ollama_instance_count_label_, ollama_instance_count_edit_, ollama_start_port_label_, ollama_start_port_edit_, ollama_start_locally_checkbox_, vision_prompt_label_, vision_prompt_edit_, include_ocr_checkbox_, include_visual_description_checkbox_, status_label_, check_status_button_, install_tesseract_button_, install_paddle_button_, install_ollama_button_, pull_vision_model_button_, diagnostics_log_edit_, save_button_, cancel_button_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    ComboBox_AddString(vision_provider_combo_, L"None");
    ComboBox_AddString(vision_provider_combo_, L"Ollama");
    ComboBox_AddString(vision_provider_combo_, L"Provider");

    LoadSettingsIntoFields();
    CenterWindowToOwner(hwnd_, owner_);
    LayoutControls();
    RefreshStatus();
    SetFocus(cpu_radio_);
}

void RagImageIngestSettingsDialog::LayoutControls() const {
    const int margin = Scale(hwnd_, 14);
    const int gutter = Scale(hwnd_, 8);
    const int label_height = Scale(hwnd_, 18);
    const int edit_height = Scale(hwnd_, 24);
    const int button_height = Scale(hwnd_, 30);
    const int footer_button_width = Scale(hwnd_, 96);
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int column_width = (width - margin * 2 - gutter * 2) / 3;

    int y = margin;
    MoveWindow(enabled_checkbox_, margin, y, width - margin * 2, Scale(hwnd_, 22), TRUE);
    y += Scale(hwnd_, 28);
    MoveWindow(cpu_radio_, margin, y, width - margin * 2, Scale(hwnd_, 22), TRUE);
    y += Scale(hwnd_, 24);
    MoveWindow(paddle_radio_, margin, y, width - margin * 2, Scale(hwnd_, 22), TRUE);
    y += Scale(hwnd_, 24);
    MoveWindow(vision_radio_, margin, y, width - margin * 2, Scale(hwnd_, 22), TRUE);
    y += Scale(hwnd_, 30);

    MoveWindow(tesseract_language_label_, margin, y, column_width, label_height, TRUE);
    MoveWindow(paddle_python_label_, margin + column_width + gutter, y, column_width, label_height, TRUE);
    MoveWindow(paddle_language_label_, margin + (column_width + gutter) * 2, y, column_width, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(tesseract_language_edit_, margin, y, column_width, edit_height, TRUE);
    MoveWindow(paddle_python_edit_, margin + column_width + gutter, y, column_width, edit_height, TRUE);
    MoveWindow(paddle_language_edit_, margin + (column_width + gutter) * 2, y, column_width, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(vision_provider_label_, margin, y, column_width, label_height, TRUE);
    MoveWindow(vision_base_url_label_, margin + column_width + gutter, y, column_width, label_height, TRUE);
    MoveWindow(vision_model_label_, margin + (column_width + gutter) * 2, y, column_width, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(vision_provider_combo_, margin, y, column_width, Scale(hwnd_, 120), TRUE);
    MoveWindow(vision_base_url_edit_, margin + column_width + gutter, y, column_width, edit_height, TRUE);
    MoveWindow(vision_model_edit_, margin + (column_width + gutter) * 2, y, column_width, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(provider_vision_model_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(provider_vision_model_combo_, margin, y, width - margin * 2, Scale(hwnd_, 140), TRUE);
    y += edit_height + gutter;

    MoveWindow(ollama_instance_count_label_, margin, y, column_width, label_height, TRUE);
    MoveWindow(ollama_start_port_label_, margin + column_width + gutter, y, column_width, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(ollama_instance_count_edit_, margin, y, column_width, edit_height, TRUE);
    MoveWindow(ollama_start_port_edit_, margin + column_width + gutter, y, column_width, edit_height, TRUE);
    MoveWindow(ollama_start_locally_checkbox_, margin + (column_width + gutter) * 2, y, column_width, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(vision_prompt_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    MoveWindow(vision_prompt_edit_, margin, y, width - margin * 2, Scale(hwnd_, 86), TRUE);
    y += Scale(hwnd_, 86) + gutter;

    MoveWindow(include_ocr_checkbox_, margin, y, width - margin * 2, Scale(hwnd_, 22), TRUE);
    y += Scale(hwnd_, 24);
    MoveWindow(include_visual_description_checkbox_, margin, y, width - margin * 2, Scale(hwnd_, 22), TRUE);
    y += Scale(hwnd_, 30);

    MoveWindow(status_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + Scale(hwnd_, 4);
    const int action_width = (width - margin * 2 - gutter * 4) / 5;
    MoveWindow(check_status_button_, margin, y, action_width, button_height, TRUE);
    MoveWindow(install_tesseract_button_, margin + (action_width + gutter), y, action_width, button_height, TRUE);
    MoveWindow(install_paddle_button_, margin + (action_width + gutter) * 2, y, action_width, button_height, TRUE);
    MoveWindow(install_ollama_button_, margin + (action_width + gutter) * 3, y, action_width, button_height, TRUE);
    MoveWindow(pull_vision_model_button_, margin + (action_width + gutter) * 4, y, action_width, button_height, TRUE);
    y += button_height + gutter;

    const int buttons_y = height - margin - button_height;
    MoveWindow(diagnostics_log_edit_, margin, y, width - margin * 2, buttons_y - y - gutter, TRUE);
    MoveWindow(cancel_button_, width - margin - footer_button_width, buttons_y, footer_button_width, button_height, TRUE);
    MoveWindow(save_button_, width - margin - footer_button_width * 2 - gutter, buttons_y, footer_button_width, button_height, TRUE);
}

void RagImageIngestSettingsDialog::OnCommand(int control_id, int notification_code) {
    switch (control_id) {
    case kImageCpuMode:
    case kImagePaddleMode:
    case kImageVisionMode:
        RefreshVisionModelField(false);
        UpdateModeControlStates();
        RefreshStatus();
        break;
    case kImageVisionProviderCombo:
        // Combo boxes send notifications for opening, closing, focus changes,
        // and selection changes. Only refresh runtime state after the selection
        // actually changes; refreshing while the dropdown window is opening can
        // re-enter combo layout/enabling code in debug builds.
        if (notification_code == CBN_SELCHANGE) {
            RefreshProviderVisionModelChoices();
            UpdateModeControlStates();
            RefreshStatus();
        }
        break;
    case kImageProviderVisionModelCombo:
        if (notification_code == CBN_SELCHANGE) {
            RefreshStatus();
        }
        break;
    case kImageCheckStatus:
    case kImageOllamaStartLocally:
        UpdateModeControlStates();
        RefreshStatus();
        break;
    case kImageInstallTesseract:
        InstallTool("tesseract");
        break;
    case kImageInstallPaddle:
        InstallTool("paddleocr");
        break;
    case kImageInstallOllama:
        InstallTool("ollama");
        break;
    case kImagePullVisionModel:
        PullVisionModel();
        break;
    case kImageVisionModelLabel:
        ShellExecuteW(nullptr, L"open", L"https://ollama.com/search?q=vision", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case kImageSaveButton:
        ValidateAndSave();
        break;
    case kImageCancelButton:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

void RagImageIngestSettingsDialog::CommitCurrentModelEditToSettings() {
    const std::string current_model = WideToUtf8(TrimWide(GetWindowTextString(vision_model_edit_)));
    settings_.vision_model = current_model;
}

void RagImageIngestSettingsDialog::RefreshVisionModelField(bool force_reload) {
    if (!force_reload) {
        return;
    }
    const std::string model = Trim(settings_.vision_model).empty() ? "qwen2.5vl:7b" : Trim(settings_.vision_model);
    SetWindowTextW(vision_model_edit_, Utf8ToWide(model).c_str());
}

std::optional<RagVisionModelOption> RagImageIngestSettingsDialog::SelectedProviderVisionModelOption() const {
    const int selected = static_cast<int>(ComboBox_GetCurSel(provider_vision_model_combo_));
    if (selected < 0 || selected >= static_cast<int>(provider_vision_models_.size())) {
        return std::nullopt;
    }
    return provider_vision_models_[static_cast<size_t>(selected)];
}

void RagImageIngestSettingsDialog::RefreshProviderVisionModelChoices() {
    if (!rag_service_ || !provider_vision_model_combo_) {
        return;
    }

    const auto previous = SelectedProviderVisionModelOption();
    std::string desired_provider = previous ? previous->provider_id : Trim(settings_.provider_vision_provider_id);
    std::string desired_model = previous ? previous->model_id : Trim(settings_.provider_vision_model_id);

    provider_vision_models_ = rag_service_->ListVisionModelOptions();
    ComboBox_ResetContent(provider_vision_model_combo_);

    int selected_index = CB_ERR;
    for (size_t index = 0; index < provider_vision_models_.size(); ++index) {
        const auto& option = provider_vision_models_[index];
        std::wstring label = Utf8ToWide(option.provider_name + " / " + option.model_display_name);
        if (!option.provider_type.empty()) {
            label += L" (" + Utf8ToWide(option.provider_type) + L")";
        }
        ComboBox_AddString(provider_vision_model_combo_, label.c_str());
        if (option.provider_id == desired_provider && option.model_id == desired_model) {
            selected_index = static_cast<int>(index);
        }
    }

    if (provider_vision_models_.empty()) {
        ComboBox_AddString(provider_vision_model_combo_, L"No vision-capable provider models configured");
        ComboBox_SetCurSel(provider_vision_model_combo_, 0);
        return;
    }

    if (selected_index == CB_ERR) {
        selected_index = 0;
    }
    ComboBox_SetCurSel(provider_vision_model_combo_, selected_index);
}

RagImageIngestSettings RagImageIngestSettingsDialog::BuildSettingsFromFields() const {
    RagImageIngestSettings settings = settings_;
    settings.enabled = Button_GetCheck(enabled_checkbox_) == BST_CHECKED;
    if (Button_GetCheck(vision_radio_) == BST_CHECKED) {
        settings.mode = "vision_language_gpu";
    } else if (Button_GetCheck(paddle_radio_) == BST_CHECKED) {
        settings.mode = "paddle_ocr_gpu";
    } else {
        settings.mode = "tesseract_cpu";
    }
    settings.tesseract_language = WideToUtf8(TrimWide(GetWindowTextString(tesseract_language_edit_)));
    settings.paddle_python_command = WideToUtf8(TrimWide(GetWindowTextString(paddle_python_edit_)));
    settings.paddle_language = WideToUtf8(TrimWide(GetWindowTextString(paddle_language_edit_)));
    int selected_vision_provider = static_cast<int>(ComboBox_GetCurSel(vision_provider_combo_));
    if (selected_vision_provider == CB_ERR) {
        selected_vision_provider = ImageVisionProviderComboIndex(settings_.vision_provider);
    }
    settings.vision_provider = ImageVisionProviderFromComboIndex(selected_vision_provider);
    settings.vision_base_url = WideToUtf8(TrimWide(GetWindowTextString(vision_base_url_edit_)));
    settings.vision_model = WideToUtf8(TrimWide(GetWindowTextString(vision_model_edit_)));
    if (settings.vision_provider == "provider") {
        if (const auto option = SelectedProviderVisionModelOption()) {
            settings.provider_vision_provider_id = option->provider_id;
            settings.provider_vision_model_id = option->model_id;
        } else {
            settings.provider_vision_provider_id.clear();
            settings.provider_vision_model_id.clear();
        }
    } else {
        settings.provider_vision_provider_id.clear();
        settings.provider_vision_model_id.clear();
    }
    if (const auto value = ParseInt(TrimWide(GetWindowTextString(ollama_instance_count_edit_)))) {
        settings.ollama_instance_count = *value;
    }
    if (const auto value = ParseInt(TrimWide(GetWindowTextString(ollama_start_port_edit_)))) {
        settings.ollama_start_port = *value;
    }
    settings.ollama_start_locally = Button_GetCheck(ollama_start_locally_checkbox_) == BST_CHECKED;
    settings.vision_prompt = WideToUtf8(TrimWide(GetWindowTextString(vision_prompt_edit_)));
    settings.include_ocr_text = Button_GetCheck(include_ocr_checkbox_) == BST_CHECKED;
    settings.include_visual_description = Button_GetCheck(include_visual_description_checkbox_) == BST_CHECKED;
    return settings;
}

void RagImageIngestSettingsDialog::LoadSettingsIntoFields() {
    Button_SetCheck(enabled_checkbox_, settings_.enabled ? BST_CHECKED : BST_UNCHECKED);
    const std::string mode = NormalizeImageMode(settings_.mode);
    Button_SetCheck(cpu_radio_, mode == "tesseract_cpu" ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(paddle_radio_, mode == "paddle_ocr_gpu" ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(vision_radio_, mode == "vision_language_gpu" ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(tesseract_language_edit_, Utf8ToWide(settings_.tesseract_language.empty() ? "eng" : settings_.tesseract_language).c_str());
    SetWindowTextW(paddle_python_edit_, Utf8ToWide(settings_.paddle_python_command.empty() ? "python" : settings_.paddle_python_command).c_str());
    SetWindowTextW(paddle_language_edit_, Utf8ToWide(settings_.paddle_language.empty() ? "en" : settings_.paddle_language).c_str());
    ComboBox_SetCurSel(vision_provider_combo_, ImageVisionProviderComboIndex(settings_.vision_provider));
    RefreshProviderVisionModelChoices();
    SetWindowTextW(vision_base_url_edit_, Utf8ToWide(settings_.vision_base_url.empty() ? "http://localhost" : settings_.vision_base_url).c_str());
    SetWindowTextW(ollama_instance_count_edit_, std::to_wstring(std::max(1, settings_.ollama_instance_count)).c_str());
    SetWindowTextW(ollama_start_port_edit_, std::to_wstring(settings_.ollama_start_port <= 0 ? 11434 : settings_.ollama_start_port).c_str());
    Button_SetCheck(ollama_start_locally_checkbox_, settings_.ollama_start_locally ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(vision_prompt_edit_, Utf8ToWide(settings_.vision_prompt).c_str());
    Button_SetCheck(include_ocr_checkbox_, settings_.include_ocr_text ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(include_visual_description_checkbox_, settings_.include_visual_description ? BST_CHECKED : BST_UNCHECKED);
    RefreshVisionModelField(true);
    UpdateModeControlStates();
}

void RagImageIngestSettingsDialog::LoadRemoteAgentJson() {
    MessageBoxW(hwnd_,
        L"Remote Agent JSON is no longer configured in Image Ingest Settings. Add the remote worker as a Provider, mark its vision model as vision-capable, then choose Vision provider: Provider.",
        L"Use Providers For Vision",
        MB_OK | MB_ICONINFORMATION);
}

void RagImageIngestSettingsDialog::UpdateModeControlStates() const {
    const int selected = static_cast<int>(ComboBox_GetCurSel(vision_provider_combo_));
    const std::string provider = ImageVisionProviderFromComboIndex(selected == CB_ERR ? 0 : selected);
    const bool provider_mode = provider == "provider";
    const bool ollama_mode = provider == "ollama";
    const bool full_vision_mode = Button_GetCheck(vision_radio_) == BST_CHECKED;

    EnableWindow(vision_provider_combo_, TRUE);
    EnableWindow(provider_vision_model_combo_, provider_mode ? TRUE : FALSE);
    EnableWindow(provider_vision_model_label_, provider_mode ? TRUE : FALSE);
    EnableWindow(vision_base_url_edit_, ollama_mode ? TRUE : FALSE);
    EnableWindow(vision_model_label_, ollama_mode ? TRUE : FALSE);
    EnableWindow(vision_model_edit_, ollama_mode ? TRUE : FALSE);
    EnableWindow(ollama_instance_count_edit_, ollama_mode ? TRUE : FALSE);
    EnableWindow(ollama_start_port_edit_, ollama_mode ? TRUE : FALSE);
    EnableWindow(ollama_start_locally_checkbox_, ollama_mode ? TRUE : FALSE);
    EnableWindow(install_ollama_button_, ollama_mode ? TRUE : FALSE);
    EnableWindow(pull_vision_model_button_, ollama_mode ? TRUE : FALSE);
    EnableWindow(install_paddle_button_, full_vision_mode || Button_GetCheck(paddle_radio_) == BST_CHECKED ? TRUE : FALSE);
}

void RagImageIngestSettingsDialog::RefreshStatus() {
    if (!rag_service_) {
        return;
    }
    const RagImageIngestSettings settings = BuildSettingsFromFields();
    const RagImageIngestRuntimeStatus status = rag_service_->GetImageIngestRuntimeStatus(settings);
    SetWindowTextW(status_label_, ImageStatusText(status).c_str());

    std::wstring log = Utf8ToWide(status.recent_log);
    if (!log.empty()) {
        log += L"\r\n";
    }
    log += L"Current diagnostics\r\n";
    log += L"- Enabled: " + std::wstring(status.enabled ? L"yes" : L"no") + L"\r\n";
    log += L"- Mode: " + Utf8ToWide(status.mode) + L"\r\n";
    log += L"- Tesseract installed: " + std::wstring(status.tesseract_installed ? L"yes" : L"no") + L"\r\n";
    log += L"- Python installed: " + std::wstring(status.python_installed ? L"yes" : L"no") + L"\r\n";
    log += L"- PaddleOCR installed: " + std::wstring(status.paddleocr_installed ? L"yes" : L"no") + L"\r\n";
    log += L"- Ollama installed: " + std::wstring(status.ollama_installed ? L"yes" : L"no") + L"\r\n";
    log += L"- Start local Ollama: " + std::wstring(status.vision_ollama_start_locally ? L"yes" : L"no") + L"\r\n";
    log += L"- App-managed Ollama endpoints: " + std::to_wstring(status.vision_ollama_managed_count) + L"\r\n";
    log += L"- Vision endpoint running: " + std::wstring(status.vision_endpoint_running ? L"yes" : L"no") + L"\r\n";
    log += L"- Vision endpoint(s): " + Utf8ToWide(status.vision_endpoint_summary.empty() ? std::string("(none)") : status.vision_endpoint_summary) + L"\r\n";
    log += L"- Vision endpoints responding: " + std::to_wstring(status.vision_ollama_running_count) + L"/" + std::to_wstring(status.vision_ollama_instance_count) + L"\r\n";
    log += L"- Provider vision configured: " + std::wstring(status.provider_vision_configured ? L"yes" : L"no") + L"\r\n";
    log += L"- Provider vision provider: " + Utf8ToWide(status.provider_vision_provider_name.empty() ? std::string("(none)") : status.provider_vision_provider_name) + L"\r\n";
    log += L"- Provider vision model: " + Utf8ToWide(status.provider_vision_model_name.empty() ? std::string("(none)") : status.provider_vision_model_name) + L"\r\n";
    log += L"- Vision queue: " + std::to_wstring(status.vision_queue_active) + L" active, " + std::to_wstring(status.vision_queue_pending) + L" queued, " + std::to_wstring(status.vision_queue_workers) + L" worker(s)\r\n";
    log += L"- Document extraction queue: " + std::to_wstring(status.document_queue_active) + L" active, " + std::to_wstring(status.document_queue_pending) + L" queued, " + std::to_wstring(status.document_queue_workers) + L" worker(s)\r\n";
    log += L"- Message: " + Utf8ToWide(status.message) + L"\r\n";
    SetWindowTextW(diagnostics_log_edit_, log.c_str());
    SendMessageW(diagnostics_log_edit_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(diagnostics_log_edit_, EM_SCROLLCARET, 0, 0);

    UpdateModeControlStates();
    const int selected = static_cast<int>(ComboBox_GetCurSel(vision_provider_combo_));
    const std::string provider = ImageVisionProviderFromComboIndex(selected == CB_ERR ? 0 : selected);
    const bool ollama_mode = provider == "ollama";
    EnableWindow(install_tesseract_button_, status.tesseract_installed ? FALSE : TRUE);
    EnableWindow(install_paddle_button_, status.paddleocr_installed ? FALSE : TRUE);
    EnableWindow(install_ollama_button_, (!ollama_mode || status.ollama_installed) ? FALSE : TRUE);
    EnableWindow(pull_vision_model_button_, (ollama_mode && status.ollama_installed) ? TRUE : FALSE);
}

void RagImageIngestSettingsDialog::InstallTool(const std::string& tool_id) {
    if (!rag_service_) {
        return;
    }
    const RagExtractionToolInstallResult result = rag_service_->LaunchImageIngestToolInstaller(BuildSettingsFromFields(), tool_id);
    std::wstring log = GetWindowTextString(diagnostics_log_edit_);
    if (!log.empty()) {
        log += L"\r\n";
    }
    log += L"Installer result: " + Utf8ToWide(result.message) + L"\r\n";
    if (!result.command.empty()) {
        log += L"Command: " + Utf8ToWide(result.command) + L"\r\n";
    }
    SetWindowTextW(diagnostics_log_edit_, log.c_str());
    RefreshStatus();
}

void RagImageIngestSettingsDialog::PullVisionModel() {
    if (!rag_service_) {
        return;
    }
    const RagExtractionToolInstallResult result = rag_service_->LaunchImageVisionModelInstaller(BuildSettingsFromFields());
    std::wstring log = GetWindowTextString(diagnostics_log_edit_);
    if (!log.empty()) {
        log += L"\r\n";
    }
    log += L"Vision model pull result: " + Utf8ToWide(result.message) + L"\r\n";
    if (!result.command.empty()) {
        log += L"Command: " + Utf8ToWide(result.command) + L"\r\n";
    }
    SetWindowTextW(diagnostics_log_edit_, log.c_str());
    RefreshStatus();
}

bool RagImageIngestSettingsDialog::ValidateAndSave() {
    RagImageIngestSettings settings = BuildSettingsFromFields();
    if (Trim(settings.tesseract_language).empty()) {
        MessageBoxW(hwnd_, L"Tesseract language is required. Use eng for English.", L"Missing Tesseract Language", MB_OK | MB_ICONERROR);
        SetFocus(tesseract_language_edit_);
        return false;
    }
    if (Trim(settings.paddle_python_command).empty()) {
        MessageBoxW(hwnd_, L"PaddleOCR Python command is required. Use python unless you have a custom Python executable.", L"Missing PaddleOCR Command", MB_OK | MB_ICONERROR);
        SetFocus(paddle_python_edit_);
        return false;
    }
    if (Trim(settings.paddle_language).empty()) {
        MessageBoxW(hwnd_, L"PaddleOCR language is required. Use en for English.", L"Missing PaddleOCR Language", MB_OK | MB_ICONERROR);
        SetFocus(paddle_language_edit_);
        return false;
    }
    const std::string mode = NormalizeImageMode(settings.mode);
    if (mode == "vision_language_gpu" && settings.include_visual_description) {
        if (settings.vision_provider != "ollama" && settings.vision_provider != "provider") {
            MessageBoxW(hwnd_, L"Full vision mode requires either Ollama or Provider as the vision provider.", L"Vision Provider Required", MB_OK | MB_ICONERROR);
            SetFocus(vision_provider_combo_);
            return false;
        }
        if (settings.vision_provider == "provider") {
            if (Trim(settings.provider_vision_provider_id).empty() || Trim(settings.provider_vision_model_id).empty()) {
                MessageBoxW(hwnd_, L"Select a vision-capable provider model. Mark models as vision-capable in Providers first, then return here.", L"Provider Vision Model Required", MB_OK | MB_ICONERROR);
                SetFocus(provider_vision_model_combo_);
                return false;
            }
            rag_service_->SaveImageIngestSettings(settings);
            saved_ = true;
            DestroyWindow(hwnd_);
            return true;
        }
        if (Trim(settings.vision_base_url).empty()) {
            MessageBoxW(hwnd_, L"Vision base URL is required for full vision mode.", L"Missing Vision Base URL", MB_OK | MB_ICONERROR);
            SetFocus(vision_base_url_edit_);
            return false;
        }
        if (Trim(settings.vision_model).empty()) {
            MessageBoxW(hwnd_, L"Vision model is required for full vision mode.", L"Missing Vision Model", MB_OK | MB_ICONERROR);
            SetFocus(vision_model_edit_);
            return false;
        }
        const auto instance_count = ParseInt(TrimWide(GetWindowTextString(ollama_instance_count_edit_)));
        if (!instance_count || *instance_count < 1 || *instance_count > 32) {
            MessageBoxW(hwnd_, L"Ollama worker instances must be a number from 1 to 32.", L"Invalid Ollama Worker Count", MB_OK | MB_ICONERROR);
            SetFocus(ollama_instance_count_edit_);
            return false;
        }
        const auto start_port = ParseInt(TrimWide(GetWindowTextString(ollama_start_port_edit_)));
        if (!start_port || *start_port < 1 || *start_port > 65535) {
            MessageBoxW(hwnd_, L"Ollama starting port must be a number from 1 to 65535.", L"Invalid Ollama Starting Port", MB_OK | MB_ICONERROR);
            SetFocus(ollama_start_port_edit_);
            return false;
        }
        if (*start_port + *instance_count - 1 > 65535) {
            MessageBoxW(hwnd_, L"The Ollama port range cannot go above 65535.", L"Invalid Ollama Port Range", MB_OK | MB_ICONERROR);
            SetFocus(ollama_start_port_edit_);
            return false;
        }
    }

    rag_service_->SaveImageIngestSettings(settings);
    saved_ = true;
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
    case kExportFinishedMessage:
        self->OnExportFinished(reinterpret_cast<ExportPayload*>(l_param));
        return 0;
    case kImportFinishedMessage:
        self->OnImportFinished(reinterpret_cast<ImportPayload*>(l_param));
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
    image_ingest_settings_button_ = CreateWindowExW(0, L"BUTTON", L"Image Ingest Settings", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImageIngestSettings), nullptr, nullptr);
    view_jobs_button_ = CreateWindowExW(0, L"BUTTON", L"View Jobs", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kViewJobs), nullptr, nullptr);
    reattach_button_ = CreateWindowExW(0, L"BUTTON", L"Reattach", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kReattachRag), nullptr, nullptr);
    export_rag_button_ = CreateWindowExW(0, L"BUTTON", L"Export RAG", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kExportRag), nullptr, nullptr);
    import_rag_button_ = CreateWindowExW(0, L"BUTTON", L"Import RAG", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImportRag), nullptr, nullptr);
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

    for (HWND control : {libraries_list_, details_edit_, add_button_, edit_button_, remove_button_, attach_read_button_, attach_write_button_, detach_button_, install_tools_button_, image_ingest_settings_button_, view_jobs_button_, reattach_button_, export_rag_button_, import_rag_button_, ingest_button_, ingest_folder_button_, rebuild_button_, documents_button_, reindex_document_button_, delete_document_button_, search_edit_, search_button_, results_edit_, status_label_, close_button_}) {
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

    const int tools_y = footer_y - (button_height + gutter) * 3;
    MoveWindow(libraries_list_, margin, y, left_width, tools_y - y - gutter, TRUE);
    const int install_width = (left_width - gutter) / 2;
    MoveWindow(install_tools_button_, margin, tools_y, install_width, button_height, TRUE);
    MoveWindow(image_ingest_settings_button_, margin + install_width + gutter, tools_y, left_width - install_width - gutter, button_height, TRUE);
    MoveWindow(view_jobs_button_, margin, tools_y + button_height + gutter, left_width, button_height, TRUE);
    const int archive_col = (left_width - gutter * 2) / 3;
    MoveWindow(reattach_button_, margin, tools_y + (button_height + gutter) * 2, archive_col, button_height, TRUE);
    MoveWindow(export_rag_button_, margin + archive_col + gutter, tools_y + (button_height + gutter) * 2, archive_col, button_height, TRUE);
    MoveWindow(import_rag_button_, margin + (archive_col + gutter) * 2, tools_y + (button_height + gutter) * 2, left_width - (archive_col + gutter) * 2, button_height, TRUE);

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
                if (binding->expose_as_tool) {
                    label += L" [tool]";
                }
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
            details += binding->expose_as_tool ? L", tool" : L", no tool";
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
    std::string source_desc;
    for (size_t i = 0; i < files.size() && i < 3; ++i) {
        if (i > 0) source_desc += ", ";
        source_desc += files[i].filename().string();
    }
    if (files.size() > 3) source_desc += " (+" + std::to_string(files.size() - 3) + " more)";
    const IngestionJobRecord job = rag_service_->CreateIngestionJob(rag_id, "files", source_desc);
    EnableWindow(ingest_button_, FALSE);
    EnableWindow(ingest_folder_button_, FALSE);
    EnableWindow(rebuild_button_, FALSE);
    EnableWindow(documents_button_, FALSE);
    EnableWindow(reindex_document_button_, FALSE);
    EnableWindow(delete_document_button_, FALSE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, 0, 0);
    UpdateStatus(L"Ingesting files...");
    std::thread([hwnd = hwnd_, service = rag_service_, rag_id, files, job_id = job.id]() {
        auto* payload = new IngestionPayload;
        payload->job_id = job_id;
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
    const std::string source_desc = folder->filename().string() + (recursive ? " (recursive)" : "");
    const IngestionJobRecord job = rag_service_->CreateIngestionJob(rag_id, "folder", source_desc);
    EnableWindow(ingest_button_, FALSE);
    EnableWindow(ingest_folder_button_, FALSE);
    EnableWindow(rebuild_button_, FALSE);
    EnableWindow(documents_button_, FALSE);
    EnableWindow(reindex_document_button_, FALSE);
    EnableWindow(delete_document_button_, FALSE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, 0, 0);
    UpdateStatus(recursive ? L"Ingesting folder recursively..." : L"Ingesting folder...");
    std::thread([hwnd = hwnd_, service = rag_service_, rag_id, folder = *folder, recursive, job_id = job.id]() {
        auto* payload = new IngestionPayload;
        payload->job_id = job_id;
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
    const IngestionJobRecord job = rag_service_->CreateIngestionJob(rag_id, "rebuild", selected->name);
    EnableWindow(ingest_button_, FALSE);
    EnableWindow(ingest_folder_button_, FALSE);
    EnableWindow(rebuild_button_, FALSE);
    EnableWindow(documents_button_, FALSE);
    EnableWindow(reindex_document_button_, FALSE);
    EnableWindow(delete_document_button_, FALSE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, 0, 0);
    UpdateStatus(L"Rebuilding RAG index...");

    std::thread([hwnd = hwnd_, service = rag_service_, rag_id, job_id = job.id]() {
        auto* payload = new IngestionPayload;
        payload->rebuild = true;
        payload->job_id = job_id;
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
    const IngestionJobRecord job = rag_service_->CreateIngestionJob(rag_id, "reindex", doc_id);
    EnableWindow(ingest_button_, FALSE);
    EnableWindow(ingest_folder_button_, FALSE);
    EnableWindow(rebuild_button_, FALSE);
    EnableWindow(documents_button_, FALSE);
    EnableWindow(reindex_document_button_, FALSE);
    EnableWindow(delete_document_button_, FALSE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, 0, 0);
    UpdateStatus(L"Reindexing document...");

    std::thread([hwnd = hwnd_, service = rag_service_, rag_id, doc_id, job_id = job.id]() {
        auto* payload = new IngestionPayload;
        payload->job_id = job_id;
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

void RagServiceManagerWindow::ShowImageIngestSettings() {
    if (RagImageIngestSettingsDialog::Show(hwnd_, rag_service_)) {
        UpdateStatus(L"Image ingest settings saved. New image imports will use the updated system-wide pipeline.");
        SetWindowTextW(results_edit_, L"Image ingest settings saved.\r\n\r\nSupported image files are preserved as originals and converted into extracted Markdown during RAG ingestion. CPU mode uses Tesseract OCR; GPU OCR mode attempts PaddleOCR with Tesseract fallback; full vision mode adds either an Ollama or provider-backed vision-language description.");
    } else {
        UpdateStatus(L"Image ingest settings closed.");
    }
}

void RagServiceManagerWindow::ShowJobs() {
    const std::vector<IngestionJobRecord> jobs = rag_service_->ListIngestionJobs(100);
    if (jobs.empty()) {
        SetWindowTextW(results_edit_, L"No ingestion jobs recorded yet.");
        UpdateStatus(L"No ingestion jobs found.");
        return;
    }

    std::wstring text;
    text += L"Ingestion Job History (" + std::to_wstring(jobs.size()) + L" most recent)\r\n";
    text += L"========================================\r\n\r\n";

    for (const auto& job : jobs) {
        text += L"[" + Utf8ToWide(job.started_at) + L"]  ";
        if (job.status == IngestionJobStatus::Running) {
            text += L"RUNNING";
        } else if (job.status == IngestionJobStatus::Completed) {
            text += L"OK";
        } else {
            text += L"FAILED";
        }
        text += L"  " + Utf8ToWide(job.kind) + L"  ";
        if (!job.rag_name.empty()) text += Utf8ToWide(job.rag_name) + L"  ";
        if (!job.source_description.empty()) text += L"\"" + Utf8ToWide(job.source_description) + L"\"";
        text += L"\r\n";

        if (!job.finished_at.empty()) {
            text += L"  Finished: " + Utf8ToWide(job.finished_at) + L"\r\n";
        }
        text += L"  Files ingested: " + std::to_wstring(job.files_ingested);
        text += L"  Skipped: " + std::to_wstring(job.files_skipped);
        text += L"  Chunks added: " + std::to_wstring(job.chunks_added) + L"\r\n";
        if (!job.errors.empty()) {
            text += L"  Errors (" + std::to_wstring(job.errors.size()) + L"):\r\n";
            for (const auto& err : job.errors) {
                text += L"    - " + Utf8ToWide(err) + L"\r\n";
            }
        }
        text += L"\r\n";
    }

    SetWindowTextW(results_edit_, text.c_str());
    SendMessageW(results_edit_, EM_SETSEL, 0, 0);
    SendMessageW(results_edit_, WM_VSCROLL, SB_TOP, 0);
    UpdateStatus(L"Showing " + std::to_wstring(jobs.size()) + L" ingestion job(s).");
}

void RagServiceManagerWindow::ReattachLibrary() {
    // Ask user to pick the folder that contains an existing on-disk RAG library.
    const auto folder = PickFolder(hwnd_, L"Select the RAG library folder to reattach (the folder that contains rag.json).");
    if (!folder) {
        return;
    }
    std::string error;
    if (rag_service_->ReattachLibrary(*folder, &error)) {
        RefreshLibraries();
        UpdateStatus(L"RAG library reattached successfully.");
        SetWindowTextW(results_edit_, L"Library reattached successfully.\r\n\r\nIt is now listed in the RAG manager. You may need to rebuild it if embeddings are missing.");
    } else {
        const std::wstring msg = L"Failed to reattach library:\r\n" + Utf8ToWide(error);
        MessageBoxW(hwnd_, msg.c_str(), L"Reattach Failed", MB_OK | MB_ICONERROR);
        UpdateStatus(L"Reattach failed.");
    }
}

void RagServiceManagerWindow::ExportLibrary() {
    const RagLibraryConfig* selected = SelectedLibrary();
    if (!selected) {
        MessageBoxW(hwnd_, L"Please select a RAG library to export.", L"Export RAG", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Ask for output directory
    const auto output_dir = PickFolder(hwnd_, L"Select the folder where the exported .rag archive series will be saved.");
    if (!output_dir) {
        return;
    }

    // Build a safe base filename from the library name
    std::wstring safe_name = Utf8ToWide(selected->name);
    for (auto& ch : safe_name) {
        if (ch == L'/' || ch == L'\\' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            ch = L'_';
        }
    }
    if (safe_name.empty()) safe_name = Utf8ToWide(selected->id);
    const std::string base_filename = WideToUtf8(safe_name);

    const std::wstring prompt = L"Export library \"" + Utf8ToWide(selected->name) + L"\" to:\r\n" + output_dir->wstring() +
        L"\r\n\r\nBase filename: " + safe_name + L"-001.rag\r\n\r\nProceed?";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"Export RAG Library", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    const std::string rag_id = selected->id;
    const std::string rag_name = selected->name;
    EnableWindow(export_rag_button_, FALSE);
    EnableWindow(import_rag_button_, FALSE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, 0, 0);
    UpdateStatus(L"Exporting RAG library...");
    SetWindowTextW(results_edit_, L"Export in progress...");

    std::thread([hwnd = hwnd_, service = rag_service_, rag_id, rag_name, output_dir = *output_dir, base_filename]() {
        auto* payload = new ExportPayload;
        payload->rag_name = rag_name;
        payload->result = service->ExportLibrary(rag_id, output_dir, base_filename,
            [hwnd](int current, int total, const std::string&) {
                // Post a progress update via progress bar range message (reuse kRebuildProgressMessage)
                auto* p = new ProgressPayload;
                p->progress.processed_items = current;
                p->progress.total_items = total > 0 ? total : 1;
                p->progress.stage = "Exporting";
                if (!PostMessageW(hwnd, kRebuildProgressMessage, 0, reinterpret_cast<LPARAM>(p))) {
                    delete p;
                }
            });
        if (!PostMessageW(hwnd, kExportFinishedMessage, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

void RagServiceManagerWindow::ImportLibrary() {
    // Open a .rag file picker for the -001.rag file
    std::vector<wchar_t> buffer(MAX_PATH * 2, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrTitle = L"Select the first series file (*-001.rag) to import";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    dialog.lpstrFilter = L"RAG Archive (*.rag)\0*.rag\0All Files\0*.*\0";
    dialog.lpstrDefExt = L"rag";
    if (!GetOpenFileNameW(&dialog)) {
        return;
    }
    const std::filesystem::path first_rag_file(buffer.data());

    // Ask for target directory where the library will be unpacked
    const auto target_dir = PickFolder(hwnd_, L"Select the folder where the RAG library will be extracted (a subfolder will be created).");
    if (!target_dir) {
        return;
    }

    const std::wstring prompt = L"Import RAG archive:\r\n" + first_rag_file.wstring() +
        L"\r\n\r\nExtract to:\r\n" + target_dir->wstring() + L"\r\n\r\nProceed?";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"Import RAG Library", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    EnableWindow(export_rag_button_, FALSE);
    EnableWindow(import_rag_button_, FALSE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, 0, 0);
    UpdateStatus(L"Importing RAG library...");
    SetWindowTextW(results_edit_, L"Import in progress...");

    std::thread([hwnd = hwnd_, service = rag_service_, first_rag_file, target_dir = *target_dir]() {
        auto* payload = new ImportPayload;
        payload->result = service->ImportLibrary(first_rag_file, target_dir,
            [hwnd](int current, int total, const std::string&) {
                auto* p = new ProgressPayload;
                p->progress.processed_items = current;
                p->progress.total_items = total > 0 ? total : 1;
                p->progress.stage = "Importing";
                if (!PostMessageW(hwnd, kRebuildProgressMessage, 0, reinterpret_cast<LPARAM>(p))) {
                    delete p;
                }
            });
        if (!PostMessageW(hwnd, kImportFinishedMessage, 0, reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

void RagServiceManagerWindow::OnExportFinished(ExportPayload* payload) {
    std::unique_ptr<ExportPayload> guard(payload);
    EnableWindow(export_rag_button_, TRUE);
    EnableWindow(import_rag_button_, TRUE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, payload->result.success ? 100 : 0, 0);

    if (payload->result.success) {
        std::wstring text = L"Export complete.\r\n\r\n";
        text += L"Library: " + Utf8ToWide(payload->rag_name) + L"\r\n";
        text += L"Series files: " + std::to_wstring(payload->result.series_count) + L"\r\n";
        const auto mb_u = payload->result.total_uncompressed / (1024 * 1024);
        const auto mb_c = payload->result.total_compressed / (1024 * 1024);
        text += L"Uncompressed: " + std::to_wstring(mb_u) + L" MB\r\n";
        text += L"Compressed:   " + std::to_wstring(mb_c) + L" MB\r\n";
        if (payload->result.total_uncompressed > 0) {
            const int ratio = static_cast<int>(100.0 * payload->result.total_compressed / payload->result.total_uncompressed);
            text += L"Compression ratio: " + std::to_wstring(ratio) + L"%\r\n";
        }
        text += L"\r\nOutput files:\r\n";
        for (const auto& f : payload->result.output_files) {
            text += L"  " + Utf8ToWide(f) + L"\r\n";
        }
        SetWindowTextW(results_edit_, text.c_str());
        UpdateStatus(L"Export complete — " + std::to_wstring(payload->result.series_count) + L" series file(s).");
    } else {
        const std::wstring msg = L"Export failed:\r\n" + Utf8ToWide(payload->result.error);
        SetWindowTextW(results_edit_, msg.c_str());
        UpdateStatus(L"Export failed.");
    }
}

void RagServiceManagerWindow::OnImportFinished(ImportPayload* payload) {
    std::unique_ptr<ImportPayload> guard(payload);
    EnableWindow(export_rag_button_, TRUE);
    EnableWindow(import_rag_button_, TRUE);
    SendMessageW(progress_bar_, PBM_SETRANGE32, 0, 100);
    SendMessageW(progress_bar_, PBM_SETPOS, payload->result.success ? 100 : 0, 0);

    if (payload->result.success) {
        RefreshLibraries();
        std::wstring text = L"Import complete.\r\n\r\n";
        text += L"Library: " + Utf8ToWide(payload->result.library_name) + L"\r\n";
        text += L"RAG ID: " + Utf8ToWide(payload->result.rag_id) + L"\r\n";
        text += L"\r\nThe library is now registered and available in the list.\r\nAttach it to your project using the Attach buttons above.";
        SetWindowTextW(results_edit_, text.c_str());
        UpdateStatus(L"Import complete — library registered.");
    } else {
        const std::wstring msg = L"Import failed:\r\n" + Utf8ToWide(payload->result.error);
        SetWindowTextW(results_edit_, msg.c_str());
        UpdateStatus(L"Import failed.");
    }
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
    if (!payload->job_id.empty()) {
        rag_service_->CompleteIngestionJob(payload->job_id, payload->result);
    }
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
        if (!payload->result.errors.empty())
            UpdateStatus(L"Rebuild finished with errors.");
        else if (!payload->result.warnings.empty())
            UpdateStatus(L"Rebuild complete (with warnings).");
        else
            UpdateStatus(L"Rebuild complete.");
    } else {
        if (!payload->result.errors.empty())
            UpdateStatus(L"Ingestion finished with errors.");
        else if (!payload->result.warnings.empty())
            UpdateStatus(L"Ingestion complete (with warnings).");
        else
            UpdateStatus(L"Ingestion complete.");
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
    case kImageIngestSettings:
        ShowImageIngestSettings();
        break;
    case kViewJobs:
        ShowJobs();
        break;
    case kReattachRag:
        ReattachLibrary();
        break;
    case kExportRag:
        ExportLibrary();
        break;
    case kImportRag:
        ImportLibrary();
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
