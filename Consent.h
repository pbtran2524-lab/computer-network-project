#ifndef CONSENT_H
#define CONSENT_H

#pragma once
#include <windows.h>
#include <string>
#include <functional>

// --- Các ID cho các thành phần UI ---
#define ID_BTN_ACCEPT 101
#define ID_BTN_REJECT 102
#define ID_TIMER      103

// ============================================================
// ConsentDialog - Hiển thị popup hỏi ý kiến người dùng
// ============================================================
class ConsentDialog {
public:
    ConsentDialog(HINSTANCE hInstance, const std::string& agentId = "PC-001");
    ~ConsentDialog();

    // Hiển thị popup với nội dung mô tả module đang yêu cầu
    // onAccept/onReject sẽ được gọi khi người dùng phản hồi hoặc hết giờ
    void Show(const std::string& moduleName,
              std::function<void()> onAccept = nullptr,
              std::function<void()> onReject = nullptr);

private:
    HWND        m_hwndPopup;
    HWND        m_hwndLabel;
    HINSTANCE   m_hInstance;
    int         m_countdown;
    std::string m_currentModule;
    std::string m_agentId;

    std::function<void()> m_onAccept;
    std::function<void()> m_onReject;

    const char* CLASS_NAME = "ConsentPopupClass";

    static LRESULT CALLBACK StaticPopupWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    void SendResponseToServer(const std::string& status);
};

// ============================================================
// ConsentManager - Singleton quản lý popup, dùng xuyên suốt project
// Tất cả module đều dùng class này thay vì tự khai báo ConsentManager
// ============================================================
class ConsentManager {
public:
    // Khởi tạo với HINSTANCE của ứng dụng (gọi 1 lần từ WinMain)
    static void Initialize(HINSTANCE hInstance, const std::string& agentId = "PC-001");

    // Hiển thị popup đếm ngược. Gọi onAccept khi user đồng ý.
    // Gọi onReject khi user từ chối hoặc hết giờ.
    static void CountdownPrompt(const std::string& message,
                                int timeoutSeconds,
                                std::function<void()> onAccept,
                                std::function<void()> onReject);

    // Alias tương thích cho ModuleWebcam (dùng AskPermission thay vì CountdownPrompt)
    static void AskPermission(const std::string& message,
                              int timeoutSeconds,
                              std::function<void()> onAccept,
                              std::function<void()> onReject);

private:
    static ConsentDialog* s_dialog;
};

#endif // CONSENT_H