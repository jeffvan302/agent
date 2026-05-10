#include "queue_test_dialog.h"
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <math.h>

#pragma comment(lib, "comctl32.lib")

// ── Engine / LogBuffer implementations ───────────────────────────────────

void QueueTestLogBuffer::Append(QueueTestLogEntry entry) {
    std::lock_guard<std::mutex> lock(mtx_);
    buffer_.push_back(std::move(entry));
    while (buffer_.size() > kMaxEntries)
        buffer_.pop_front();
}

std::vector<QueueTestLogEntry> QueueTestLogBuffer::ReadAll() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return std::vector<QueueTestLogEntry>(buffer_.begin(), buffer_.end());
}

size_t QueueTestLogBuffer::Size() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return buffer_.size();
}

void QueueTestLogBuffer::Clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    buffer_.clear();
}

QueueTestEngine::QueueTestEngine(QueueTestConfig config, QueueTestLogBuffer* log)
    : config_(std::move(config)), log_(log) {}

void QueueTestEngine::Start() {
    if (running_) return;
    running_ = true;
    completed_ = 0;
    rejected_ = 0;
    int n = std::max(1, config_.thread_count);
    workers_.reserve(n);
    for (int i = 0; i < n; ++i) {
        workers_.emplace_back(&QueueTestEngine::WorkerLoop, this, i);
    }
}

void QueueTestEngine::Stop() {
    running_ = false;
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

bool QueueTestEngine::IsRunning() const {
    return running_;
}

int QueueTestEngine::CompletedRequests() const {
    return completed_;
}

int QueueTestEngine::RejectedRequests() const {
    return rejected_;
}

std::string QueueTestEngine::CurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm tm;
    gmtime_s(&tm, &t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

void QueueTestEngine::Log(const std::string& level, const std::string& event,
                          const GateKey& key, const std::string& details) {
    if (!log_) return;
    QueueTestLogEntry e;
    e.timestamp = CurrentTimestamp();
    e.level = level;
    e.thread_id = std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    e.event = event;
    e.gate_key = ProviderRequestGate::GateKeyToString(key);
    e.details = details;
    log_->Append(std::move(e));
}

void QueueTestEngine::LogSnapshot(const GateKey& key) {
    if (!log_) return;
    auto snap = ProviderRequestGate::GetGateState(key);
    if (!snap) return;
    std::ostringstream d;
    d << "active=" << snap->currently_active << " waiters=" << snap->waiters_depth
      << " max_active=" << snap->effective_max_active << " max_queue=" << snap->effective_max_queue;
    Log("INFO", "SNAPSHOT", key, d.str());
}

void QueueTestEngine::WorkerLoop(int worker_id) {
    GateKey key{config_.provider_id, config_.model.id, config_.domain};
    int max_active = config_.model.max_active_requests;
    int max_queue = config_.model.max_queue_size;
    if (max_active <= 0) max_active = 1;
    if (max_queue < 0) max_queue = 0;

    for (int i = 0; i < config_.requests_per_thread && running_; ++i) {
        if (config_.delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.delay_ms));
        }
        bool acquired = false;
        if (config_.use_try_acquire) {
            acquired = ProviderRequestGate::TryAcquire(key, max_active, max_queue, nullptr);
            if (acquired) {
                Log("INFO", "TRY_ACQUIRED", key, "worker=" + std::to_string(worker_id) + " request=" + std::to_string(i));
            } else {
                Log("WARN", "TRY_REJECTED", key, "worker=" + std::to_string(worker_id) + " request=" + std::to_string(i));
                ++rejected_;
            }
        } else {
            Log("INFO", "WILL_ACQUIRE", key, "worker=" + std::to_string(worker_id) + " request=" + std::to_string(i));
            acquired = ProviderRequestGate::Acquire(key, max_active, max_queue, nullptr);
            if (acquired) {
                Log("INFO", "ACQUIRED", key, "worker=" + std::to_string(worker_id) + " request=" + std::to_string(i));
            } else {
                Log("WARN", "REJECTED", key, "worker=" + std::to_string(worker_id) + " request=" + std::to_string(i));
                ++rejected_;
            }
        }

        if (acquired) {
            if (config_.hold_ms > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(config_.hold_ms));
            ProviderRequestGate::Release(key);
            Log("INFO", "RELEASED", key, "worker=" + std::to_string(worker_id) + " request=" + std::to_string(i));
            ++completed_;
        }
        if (i % 5 == 0) LogSnapshot(key);
    }
}

// ── Helpers (assumed available from main binary) ─────────────────────────
extern std::wstring Utf8ToWide(const std::string& text);
extern void CenterWindowToOwner(HWND hwnd, HWND owner);

// ── QueueTestDialog Win32 UI ─────────────────────────────────────────────

namespace {

enum QueueTestDialogId {
    kThreadCountEdit   = 2400,
    kRequestCountEdit  = 2401,
    kDelayMsEdit       = 2402,
    kHoldMsEdit        = 2403,
    kDomainCombo       = 2404,
    kTryAcquireCheck   = 2405,
    kStartButton       = 2406,
    kStopButton        = 2407,
    kLogEdit           = 2408,
    kModelLabel        = 2409,
};

class QueueTestDialog {
public:
    static void Show(HWND owner, const ModelConfig& model, const std::vector<ProviderConfig>& providers) {
        QueueTestDialog dlg(owner, model, providers);
        dlg.Run();
    }

private:
    QueueTestDialog(HWND owner, const ModelConfig& model, const std::vector<ProviderConfig>& providers)
        : owner_(owner), model_(model), providers_(providers) {}

    static void RegisterWindowClass(HINSTANCE instance) {
        static bool registered = false;
        if (registered) return;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &QueueTestDialog::WindowProc;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        registered = true;
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        auto* self = reinterpret_cast<QueueTestDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            self = reinterpret_cast<QueueTestDialog*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        if (!self) return DefWindowProcW(hwnd, message, w_param, l_param);
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
        case WM_TIMER:
            self->PollLog();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
    }

    void Run() {
        RegisterWindowClass(reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)));
        hwnd_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kClassName,
            L"Queue Test",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            kWidth, kHeight,
            owner_, nullptr,
            reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)), this);
        if (!hwnd_) return;
        CenterWindowToOwner(hwnd_, owner_);
        EnableWindow(owner_, FALSE);
        MSG msg{};
        while (IsWindow(hwnd_) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd_, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (engine_) { engine_->Stop(); engine_.reset(); }
        EnableWindow(owner_, TRUE);
        SetForegroundWindow(owner_);
    }

    void OnCreate() {
        font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        model_label_ = CreateWindowExW(0, L"STATIC", nullptr,
            WS_CHILD | WS_VISIBLE, 0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kModelLabel), nullptr, nullptr);
        SetWindowTextW(model_label_, (L"Model: " + Utf8ToWide(model_.display_name.empty() ? model_.id : model_.display_name)).c_str());

        thread_count_label_ = CreateLabelHelper(L"Threads:", 0);
        thread_count_edit_ = CreateEditHelper(L"4", kThreadCountEdit, ES_NUMBER);
        request_count_label_ = CreateLabelHelper(L"Reqs/thread:", 0);
        request_count_edit_ = CreateEditHelper(L"10", kRequestCountEdit, ES_NUMBER);
        delay_label_ = CreateLabelHelper(L"Delay (ms):", 0);
        delay_edit_ = CreateEditHelper(L"100", kDelayMsEdit, ES_NUMBER);
        hold_label_ = CreateLabelHelper(L"Hold (ms):", 0);
        hold_edit_ = CreateEditHelper(L"500", kHoldMsEdit, ES_NUMBER);
        domain_label_ = CreateLabelHelper(L"Domain:", 0);
        domain_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kDomainCombo), nullptr, nullptr);
        ComboBox_AddString(domain_combo_, L"Chat");
        ComboBox_AddString(domain_combo_, L"Embedding");
        ComboBox_SetCurSel(domain_combo_, 0);
        try_acquire_check_ = CreateWindowExW(0, L"BUTTON", L"Use TryAcquire (non-blocking)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kTryAcquireCheck), nullptr, nullptr);
        start_button_ = CreateButtonHelper(L"Start", kStartButton, 0);
        stop_button_ = CreateButtonHelper(L"Stop", kStopButton, 0);
        log_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            0,0,0,0, hwnd_, reinterpret_cast<HMENU>(kLogEdit), nullptr, nullptr);

        for (HWND h : {model_label_, thread_count_label_, thread_count_edit_, request_count_label_, request_count_edit_,
                        delay_label_, delay_edit_, hold_label_, hold_edit_, domain_label_, domain_combo_,
                        try_acquire_check_, start_button_, stop_button_, log_edit_}) {
            if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }

        SetTimer(hwnd_, 1, 300, nullptr);
        LayoutControls(kWidth, kHeight);
    }

    HWND CreateLabelHelper(const wchar_t* text, int id) {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
            0,0,0,0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    }
    HWND CreateEditHelper(const wchar_t* text, int id, DWORD style) {
        return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
            0,0,0,0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    }
    HWND CreateButtonHelper(const wchar_t* text, int id, DWORD style) {
        return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
            0,0,0,0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    }

    void LayoutControls(int width, int height) {
        const int margin = 14;
        const int gutter = 8;
        const int lbl_w = 90;
        const int edit_w = 70;
        const int row_h = 24;
        const int btn_h = 28;
        int y = margin;
        MoveWindow(model_label_, margin, y, width - margin*2, row_h, TRUE);
        y += row_h + gutter;
        int x = margin;
        MoveWindow(thread_count_label_, x, y+3, lbl_w, row_h, TRUE); x += lbl_w + gutter;
        MoveWindow(thread_count_edit_, x, y, edit_w, row_h, TRUE); x += edit_w + gutter*2;
        MoveWindow(request_count_label_, x, y+3, lbl_w, row_h, TRUE); x += lbl_w + gutter;
        MoveWindow(request_count_edit_, x, y, edit_w, row_h, TRUE); x += edit_w + gutter*2;
        MoveWindow(delay_label_, x, y+3, lbl_w, row_h, TRUE); x += lbl_w + gutter;
        MoveWindow(delay_edit_, x, y, edit_w, row_h, TRUE); x += edit_w + gutter*2;
        MoveWindow(hold_label_, x, y+3, lbl_w, row_h, TRUE); x += lbl_w + gutter;
        MoveWindow(hold_edit_, x, y, edit_w, row_h, TRUE);
        y += row_h + gutter;
        x = margin;
        MoveWindow(domain_label_, x, y+3, lbl_w, row_h, TRUE); x += lbl_w + gutter;
        MoveWindow(domain_combo_, x, y, 120, row_h, TRUE); x += 120 + gutter*2;
        MoveWindow(try_acquire_check_, x, y+2, 220, row_h, TRUE); x += 220 + gutter*2;
        MoveWindow(start_button_, x, y, 70, btn_h, TRUE); x += 70 + gutter;
        MoveWindow(stop_button_, x, y, 70, btn_h, TRUE);
        y += row_h + gutter;
        MoveWindow(log_edit_, margin, y, width - margin*2, height - y - margin - btn_h - gutter, TRUE);
    }

    void OnCommand(int id, int /*notify*/) {
        if (id == kStartButton) {
            OnStart();
        } else if (id == kStopButton) {
            OnStop();
        }
    }

    void OnStart() {
        if (engine_ && engine_->IsRunning()) return;
        if (log_buffer_) log_buffer_->Clear();
        QueueTestConfig cfg;
        cfg.provider_id = SelectedProviderId();
        cfg.model = model_;
        cfg.providers = providers_;
        cfg.thread_count = GetEditInt(thread_count_edit_, 4);
        cfg.requests_per_thread = GetEditInt(request_count_edit_, 10);
        cfg.delay_ms = GetEditInt(delay_edit_, 100);
        cfg.hold_ms = GetEditInt(hold_edit_, 500);
        cfg.use_try_acquire = Button_GetCheck(try_acquire_check_) == BST_CHECKED;
        cfg.domain = (ComboBox_GetCurSel(domain_combo_) == 1) ? GateDomain::Embedding : GateDomain::Chat;
        log_buffer_ = std::make_unique<QueueTestLogBuffer>();
        engine_ = std::make_unique<QueueTestEngine>(cfg, log_buffer_.get());
        engine_->Start();
        AppendLogLine(L"Started queue test.");
    }

    void OnStop() {
        if (engine_) engine_->Stop();
        AppendLogLine(L"Stopped queue test.");
    }

    void PollLog() {
        if (!log_buffer_) return;
        auto entries = log_buffer_->ReadAll();
        if (!entries.empty()) {
            std::wstring full;
            for (const auto& e : entries) {
                full += Utf8ToWide(e.timestamp + " [" + e.level + "] [" + e.thread_id + "] " + e.event +
                                   " " + e.gate_key + " | " + e.details) + L"\r\n";
            }
            int len = GetWindowTextLengthW(log_edit_);
            SendMessageW(log_edit_, EM_SETSEL, len, len);
            SendMessageW(log_edit_, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(full.c_str()));
        }
    }

    void AppendLogLine(const std::wstring& line) {
        int len = GetWindowTextLengthW(log_edit_);
        SendMessageW(log_edit_, EM_SETSEL, len, len);
        SendMessageW(log_edit_, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>((line + L"\r\n").c_str()));
    }

    int GetEditInt(HWND edit, int fallback) const {
        wchar_t buf[64]{};
        GetWindowTextW(edit, buf, 63);
        int v = _wtoi(buf);
        return v > 0 ? v : fallback;
    }

    std::string SelectedProviderId() const {
        for (const auto& p : providers_) {
            if (p.id == model_.id) return p.id;
        }
        if (!providers_.empty()) return providers_[0].id;
        return std::string{};
    }

    static constexpr int kWidth = 640;
    static constexpr int kHeight = 520;
    static constexpr const wchar_t* kClassName = L"QueueTestDialog";

    HWND owner_ = nullptr;
    ModelConfig model_;
    std::vector<ProviderConfig> providers_;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HWND model_label_ = nullptr;
    HWND thread_count_label_ = nullptr;
    HWND thread_count_edit_ = nullptr;
    HWND request_count_label_ = nullptr;
    HWND request_count_edit_ = nullptr;
    HWND delay_label_ = nullptr;
    HWND delay_edit_ = nullptr;
    HWND hold_label_ = nullptr;
    HWND hold_edit_ = nullptr;
    HWND domain_label_ = nullptr;
    HWND domain_combo_ = nullptr;
    HWND try_acquire_check_ = nullptr;
    HWND start_button_ = nullptr;
    HWND stop_button_ = nullptr;
    HWND log_edit_ = nullptr;
    std::unique_ptr<QueueTestLogBuffer> log_buffer_;
    std::unique_ptr<QueueTestEngine> engine_;
};

} // namespace

void ShowQueueTestDialog(HWND owner, const ModelConfig& model,
                         const std::vector<ProviderConfig>& providers) {
    QueueTestDialog::Show(owner, model, providers);
}
