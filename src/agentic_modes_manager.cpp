#include "agentic_modes_manager.h"

#include "storage.h"
#include "util.h"

#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kClassName[] = L"AgentAgenticModesManager";

enum ControlId : int {
    kModeList       = 7501,
    kAddMode        = 7502,
    kDeleteMode     = 7503,

    kNameEdit       = 7510,
    kInstructionsEdit = 7511,
    kImportButton   = 7512,

    kCloseButton    = IDCANCEL,
};

static int Scale(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

static std::string MakeId() {
    return "mode_" + std::to_string(GetTickCount64());
}

class AgenticModesManager {
public:
    AgenticModesManager(HWND owner, AppStorage* storage)
        : owner_(owner), storage_(storage) {
        modes_ = storage_->LoadAgenticModes();
    }

    HWND Create() {
        RegisterClass();
        HWND hwnd = CreateWindowExW(
            WS_EX_CONTROLPARENT, kClassName, L"Agentic Modes",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(nullptr, 700), Scale(nullptr, 550),
            owner_, nullptr, GetModuleHandleW(nullptr), this);
        if (hwnd) {
            ShowWindow(hwnd, SW_SHOW);
        }
        return hwnd;
    }

private:
    static void RegisterClass() {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_CREATE) {
            auto* self = static_cast<AgenticModesManager*>(
                reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
            self->OnCreate();
            return 0;
        }
        auto* self = reinterpret_cast<AgenticModesManager*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        switch (msg) {
        case WM_SIZE:
            self->LayoutControls(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_COMMAND:
            self->OnCommand(LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            delete self;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void OnCreate() {
        font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        mode_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kModeList), nullptr, nullptr);
        add_mode_button_ = CreateWindowExW(0, L"BUTTON", L"New Mode",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kAddMode), nullptr, nullptr);
        delete_mode_button_ = CreateWindowExW(0, L"BUTTON", L"Delete Mode",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kDeleteMode), nullptr, nullptr);

        name_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kNameEdit), nullptr, nullptr);
        instructions_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kInstructionsEdit), nullptr, nullptr);
        import_button_ = CreateWindowExW(0, L"BUTTON", L"Import",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kImportButton), nullptr, nullptr);
        close_button_ = CreateWindowExW(0, L"BUTTON", L"Close",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(kCloseButton), nullptr, nullptr);

        for (HWND c : {mode_list_, add_mode_button_, delete_mode_button_,
                       name_edit_, instructions_edit_, import_button_, close_button_}) {
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }

        RefreshModeList();
    }

    void LayoutControls(int w, int h) {
        const int margin = Scale(hwnd_, 12);
        const int gutter = Scale(hwnd_, 8);
        const int left_w = Scale(hwnd_, 200);
        const int btn_h  = Scale(hwnd_, 26);
        const int edit_h = Scale(hwnd_, 24);
        const int right_x = margin + left_w + gutter * 2;
        const int right_w = w - right_x - margin;

        // Left panel: mode list + buttons
        MoveWindow(add_mode_button_, margin, margin, left_w, btn_h, TRUE);
        MoveWindow(delete_mode_button_, margin, margin + btn_h + gutter, left_w, btn_h, TRUE);
        MoveWindow(mode_list_, margin, margin + (btn_h + gutter) * 2,
                   left_w, h - margin * 2 - (btn_h + gutter) * 2 - btn_h, TRUE);

        // Footer: close button
        MoveWindow(close_button_, w - margin - Scale(hwnd_, 80), h - margin - btn_h,
                   Scale(hwnd_, 80), btn_h, TRUE);

        // Right panel: name edit + import button on same row, then instructions edit below
        const int import_w = Scale(hwnd_, 80);
        MoveWindow(name_edit_, right_x, margin, right_w - import_w - gutter, edit_h, TRUE);
        MoveWindow(import_button_, right_x + right_w - import_w, margin,
                   import_w, btn_h, TRUE);
        MoveWindow(instructions_edit_, right_x, margin + edit_h + gutter,
                   right_w, h - margin - (edit_h + gutter) - margin, TRUE);
    }

    void OnCommand(int id, int code) {
        if (code == EN_CHANGE && (id == kNameEdit || id == kInstructionsEdit)) {
            AutoSaveIfDirty();
            return;
        }
        switch (id) {
        case kAddMode:
            AddMode();
            break;
        case kDeleteMode:
            DeleteMode();
            break;
        case kImportButton:
            ImportMarkdown();
            break;
        case kModeList:
            if (code == LBN_SELCHANGE) OnModeSelectionChanged();
            break;
        case kCloseButton:
            SaveIfDirty();
            DestroyWindow(hwnd_);
            break;
        }
    }

    void AddMode() {
        AgenticModeConfig mode;
        mode.id = MakeId();
        mode.name = "New Mode";
        mode.instructions = "";
        modes_.push_back(std::move(mode));
        RefreshModeList();
        const int last = static_cast<int>(modes_.size()) - 1;
        SendMessageW(mode_list_, LB_SETCURSEL, last, 0);
        OnModeSelectionChanged();
        StorageSave();
    }

    void DeleteMode() {
        const int sel = static_cast<int>(SendMessageW(mode_list_, LB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= static_cast<int>(modes_.size())) return;
        modes_.erase(modes_.begin() + sel);
        RefreshModeList();
        if (sel < static_cast<int>(modes_.size())) {
            SendMessageW(mode_list_, LB_SETCURSEL, sel, 0);
            OnModeSelectionChanged();
        } else if (!modes_.empty()) {
            SendMessageW(mode_list_, LB_SETCURSEL, static_cast<WPARAM>(modes_.size()) - 1, 0);
            OnModeSelectionChanged();
        } else {
            SetWindowTextW(name_edit_, L"");
            SetWindowTextW(instructions_edit_, L"");
            EnableWindow(name_edit_, FALSE);
            EnableWindow(instructions_edit_, FALSE);
            EnableWindow(import_button_, FALSE);
        }
        StorageSave();
    }

    void OnModeSelectionChanged() {
        const int sel = static_cast<int>(SendMessageW(mode_list_, LB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= static_cast<int>(modes_.size())) return;
        suppress_autosave_ = true;
        SetWindowTextW(name_edit_, Utf8ToWide(modes_[sel].name).c_str());
        SetWindowTextW(instructions_edit_, Utf8ToWide(modes_[sel].instructions).c_str());
        EnableWindow(name_edit_, TRUE);
        EnableWindow(instructions_edit_, TRUE);
        EnableWindow(import_button_, TRUE);
        suppress_autosave_ = false;
    }

    void AutoSaveIfDirty() {
        if (suppress_autosave_) return;
        const int sel = static_cast<int>(SendMessageW(mode_list_, LB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= static_cast<int>(modes_.size())) return;
        const std::string new_name = WideToUtf8(TrimWide(GetWindowTextString(name_edit_)));
        const std::string new_instructions = WideToUtf8(TrimWide(GetWindowTextString(instructions_edit_)));
        if (modes_[sel].name == new_name && modes_[sel].instructions == new_instructions) return;
        modes_[sel].name = new_name;
        modes_[sel].instructions = new_instructions;
        // Update listbox label
        SendMessageW(mode_list_, LB_DELETESTRING, sel, 0);
        SendMessageW(mode_list_, LB_INSERTSTRING, sel,
                     reinterpret_cast<LPARAM>(Utf8ToWide(new_name).c_str()));
        SendMessageW(mode_list_, LB_SETCURSEL, sel, 0);
        StorageSave();
    }

    void SaveIfDirty() {
        const int sel = static_cast<int>(SendMessageW(mode_list_, LB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= static_cast<int>(modes_.size())) return;
        modes_[sel].name = WideToUtf8(TrimWide(GetWindowTextString(name_edit_)));
        modes_[sel].instructions = WideToUtf8(GetWindowTextString(instructions_edit_));
        StorageSave();
    }

    void ImportMarkdown() {
        wchar_t path[MAX_PATH] = {};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = hwnd_;
        ofn.lpstrFile   = path;
        ofn.nMaxFile    = static_cast<DWORD>(std::size(path));
        ofn.lpstrFilter = L"Markdown and Text Files\0*.md;*.markdown;*.txt\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
        if (!GetOpenFileNameW(&ofn)) {
            return;
        }
        try {
            std::ifstream in(std::filesystem::path(path), std::ios::binary);
            if (!in) return;
            std::string content{std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>()};
            if (content.size() >= 3 &&
                static_cast<unsigned char>(content[0]) == 0xef &&
                static_cast<unsigned char>(content[1]) == 0xbb &&
                static_cast<unsigned char>(content[2]) == 0xbf) {
                content.erase(0, 3);
            }
            SetWindowTextW(instructions_edit_, Utf8ToWide(content).c_str());
            AutoSaveIfDirty();
        } catch (...) {}
    }

    void RefreshModeList() {
        SendMessageW(mode_list_, LB_RESETCONTENT, 0, 0);
        for (const auto& mode : modes_) {
            SendMessageW(mode_list_, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(Utf8ToWide(mode.name).c_str()));
        }
    }

    void StorageSave() {
        storage_->SaveAgenticModes(modes_);
    }

    static std::wstring GetWindowTextString(HWND hwnd) {
        const int len = GetWindowTextLengthW(hwnd);
        std::wstring text(static_cast<size_t>(len) + 1, L'\0');
        GetWindowTextW(hwnd, text.data(), len + 1);
        text.resize(static_cast<size_t>(len));
        return text;
    }

    HWND owner_ = nullptr;
    HWND hwnd_  = nullptr;
    HFONT font_ = nullptr;
    AppStorage* storage_ = nullptr;
    std::vector<AgenticModeConfig> modes_;

    bool suppress_autosave_ = false;

    HWND mode_list_ = nullptr;
    HWND add_mode_button_ = nullptr;
    HWND delete_mode_button_ = nullptr;
    HWND name_edit_ = nullptr;
    HWND instructions_edit_ = nullptr;
    HWND import_button_ = nullptr;
    HWND close_button_ = nullptr;
};

} // namespace

HWND OpenAgenticModesManager(HWND owner, AppStorage* storage) {
    auto* mgr = new AgenticModesManager(owner, storage);
    HWND hwnd = mgr->Create();
    if (!hwnd) { delete mgr; return nullptr; }
    return hwnd;
}
