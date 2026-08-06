#include "Consent.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

// ============================================================
// ConsentDialog — Implementation
// ============================================================

ConsentDialog::ConsentDialog(HINSTANCE hInstance, const std::string& agentId)
    : m_hInstance(hInstance), m_agentId(agentId), m_hwndPopup(NULL), m_hwndLabel(NULL), m_countdown(15) {
    
    // Đăng ký Window Class
    WNDCLASS wc = { };
    wc.lpfnWndProc   = ConsentDialog::StaticPopupWndProc; // Trỏ tới hàm tĩnh
    wc.hInstance     = m_hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);

    RegisterClass(&wc);
}

ConsentDialog::~ConsentDialog() {
    if (m_hwndPopup) {
        DestroyWindow(m_hwndPopup);
    }
}

// --- Show() — khớp signature 3 tham số trong Consent.h ---
void ConsentDialog::Show(const std::string& moduleName,
                         std::function<void()> onAccept,
                         std::function<void()> onReject) {
    if (m_hwndPopup != NULL) return; // Nếu đang có popup khác thì không mở thêm

    m_currentModule = moduleName;
    m_countdown = 15; // Reset thời gian
    m_onAccept = onAccept;
    m_onReject = onReject;

    // Tính toán để hiển thị ở giữa màn hình
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int windowW = 420;
    int windowH = 200;

    // Tạo cửa sổ và truyền con trỏ 'this' qua tham số cuối cùng (lpParam)
    m_hwndPopup = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 
        CLASS_NAME,
        "Yêu cầu quyền truy cập!",
        WS_POPUPWINDOW | WS_CAPTION, 
        (screenW - windowW) / 2, (screenH - windowH) / 2, windowW, windowH,
        NULL, NULL, m_hInstance, this
    );

    if (m_hwndPopup) {
        ShowWindow(m_hwndPopup, SW_SHOW);
        UpdateWindow(m_hwndPopup);
    }
}

void ConsentDialog::SendResponseToServer(const std::string& status) {
    json j;
    
    j["agent_id"] = m_agentId;
    j["module"] = m_currentModule; 
    j["action"] = "consent_response";
    j["payload"]["status"] = status;

    std::string jsonStr = j.dump(); 
    OutputDebugStringA(("[Consent Send] " + jsonStr + "\n").c_str());
    // Gửi jsonStr qua WebSocket/TCP Socket ở đây...
}

// Cầu nối tĩnh (Static Window Procedure)
LRESULT CALLBACK ConsentDialog::StaticPopupWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    ConsentDialog* pThis = nullptr;

    if (message == WM_NCCREATE) {
        // Trích xuất con trỏ 'this' được truyền từ CreateWindowEx
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<ConsentDialog*>(pCreate->lpCreateParams);
        // Lưu con trỏ 'this' vào bộ nhớ của cửa sổ
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        
        pThis->m_hwndPopup = hWnd; // Lưu hwnd vào instance
    } else {
        // Lấy lại con trỏ 'this' từ bộ nhớ cửa sổ cho các message sau
        pThis = reinterpret_cast<ConsentDialog*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        // Gọi hàm xử lý thực tế của đối tượng
        return pThis->HandleMessage(hWnd, message, wParam, lParam);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// Hàm xử lý sự kiện (đã trở thành non-static)
LRESULT ConsentDialog::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            // Hiển thị nội dung module đang yêu cầu
            std::string labelText = m_currentModule + "\nTự động từ chối sau: " + std::to_string(m_countdown) + "s";
            m_hwndLabel = CreateWindowA("STATIC", labelText.c_str(),
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 15, 370, 60, hWnd, NULL, NULL, NULL);

            CreateWindowA("BUTTON", "Đồng ý",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                60, 100, 120, 35, hWnd, (HMENU)ID_BTN_ACCEPT, NULL, NULL);

            CreateWindowA("BUTTON", "Từ chối",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                230, 100, 120, 35, hWnd, (HMENU)ID_BTN_REJECT, NULL, NULL);

            SetTimer(hWnd, ID_TIMER, 1000, NULL);
            break;
        }

        case WM_TIMER: {
            m_countdown--;
            if (m_countdown > 0) {
                std::string text = m_currentModule + "\nTự động từ chối sau: " + std::to_string(m_countdown) + "s";
                SetWindowTextA(m_hwndLabel, text.c_str());
            } else {
                // Hết giờ → tự động từ chối
                KillTimer(hWnd, ID_TIMER);
                SendResponseToServer("timeout");
                if (m_onReject) m_onReject();
                DestroyWindow(hWnd);
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == ID_BTN_ACCEPT) {
                KillTimer(hWnd, ID_TIMER);
                SendResponseToServer("accepted");
                if (m_onAccept) m_onAccept();
                DestroyWindow(hWnd);
            } 
            else if (wmId == ID_BTN_REJECT) {
                KillTimer(hWnd, ID_TIMER);
                SendResponseToServer("rejected");
                if (m_onReject) m_onReject();
                DestroyWindow(hWnd);
            }
            break;
        }

        case WM_DESTROY:
            m_hwndPopup = NULL; 
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ============================================================
// ConsentManager — Singleton Implementation
// ============================================================

// Khởi tạo static member
ConsentDialog* ConsentManager::s_dialog = nullptr;

void ConsentManager::Initialize(HINSTANCE hInstance, const std::string& agentId) {
    if (!s_dialog) {
        s_dialog = new ConsentDialog(hInstance, agentId);
    }
}

void ConsentManager::CountdownPrompt(const std::string& message,
                                     int timeoutSeconds,
                                     std::function<void()> onAccept,
                                     std::function<void()> onReject) {
    if (s_dialog) {
        s_dialog->Show(message, onAccept, onReject);
    } else {
        // Fallback: nếu chưa Initialize, dùng MessageBox blocking
        OutputDebugStringA("[Consent] CẢNH BÁO: ConsentManager chưa được Initialize! Dùng MessageBox fallback.\n");
        int result = MessageBoxA(
            NULL, message.c_str(), "Yêu cầu quyền truy cập",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SYSTEMMODAL | MB_SETFOREGROUND
        );
        if (result == IDYES && onAccept) {
            onAccept();
        } else if (onReject) {
            onReject();
        }
    }
}

// Alias tương thích cho ModuleWebcam
void ConsentManager::AskPermission(const std::string& message,
                                   int timeoutSeconds,
                                   std::function<void()> onAccept,
                                   std::function<void()> onReject) {
    // Gọi lại CountdownPrompt — cùng cơ chế
    CountdownPrompt(message, timeoutSeconds, onAccept, onReject);
}