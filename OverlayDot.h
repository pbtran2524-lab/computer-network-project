#ifndef PRIVACY_INDICATOR_H
#define PRIVACY_INDICATOR_H

#include <windows.h>

// --- Định nghĩa ID ---
#define ID_BLINK_TIMER 201

// --- Message tùy chỉnh để SetState từ thread khác (thread-safe) ---
#define WM_SET_INDICATOR_STATE (WM_USER + 10)

// --- Định nghĩa các trạng thái của đèn ---
enum IndicatorState {
    STATE_OFF      = 0,
    STATE_FLASHING = 1, // Nhấp nháy – dùng cho Webcam, LiveScreen, Keylogger
    STATE_SOLID    = 2  // Đứng yên – dùng cho Screenshot
};

// ============================================================
// PrivacyIndicator - Chấm đỏ overlay góc màn hình
// Thread-safe: dùng PostMessage(WM_SET_INDICATOR_STATE) để
// gọi SetState từ các luồng background.
// ============================================================
class PrivacyIndicator {
public:
    PrivacyIndicator(HINSTANCE hInstance);
    ~PrivacyIndicator();

    // Thread-safe: gửi message vào queue của UI thread
    void SetState(IndicatorState newState);

    // Trả về HWND để PostMessage từ bên ngoài
    HWND GetHwnd() const { return m_hwnd; }

private:
    HWND            m_hwnd;
    HINSTANCE       m_hInstance;
    IndicatorState  m_currentState;
    bool            m_isRedVisible;

    const char* CLASS_NAME = "PrivacyIndicatorClass";

    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};

// ============================================================
// Global helper — các module gọi hàm này để bật/tắt chấm đỏ
// (thread-safe vì dùng PostMessage bên dưới)
// ============================================================
void SetIndicatorState(int state);

// Khởi tạo global indicator (gọi 1 lần từ WinMain)
void InitializeIndicator(HINSTANCE hInstance);

#endif // PRIVACY_INDICATOR_H