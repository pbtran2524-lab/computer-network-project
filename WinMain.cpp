#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <atomic>
#include <string>

#include "NetworkRouting.h"
#include "Consent.h"
#include "OverlayDot.h"

#pragma comment(lib, "shell32.lib")

// Sử dụng atomic để luồng chính có thể ra hiệu cho luồng mạng dừng lại an toàn
std::atomic<bool> isRunning(true);
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT     2001

using json = nlohmann::json;

// ---------------------------------------------------------
// [Luồng chạy ngầm] Xử lý Network & Socket
// Thay thế NetworkWorker cũ bằng việc gọi RouteMessage()
// ---------------------------------------------------------
void NetworkWorker() {
    OutputDebugStringA("[Network] Luồng mạng đã khởi động.\n");

    // Khởi động luồng Heartbeat (Ping/Pong)
    StartHeartbeatThread();

    while (isRunning) {
        OutputDebugStringA("[Network] Đang chờ lệnh từ Server...\n");

        // Mô phỏng nhận JSON từ Server
        // Trong thực tế, bạn sẽ dùng recv() từ Socket hoặc WebSocket client
        std::string mockReceivedPayload = R"({
          "agent_id": "PC-001",
          "module": "processes",
          "action": "list",
          "payload": {}
        })";

        // Định tuyến qua Router trung tâm (thay vì xử lý inline)
        RouteMessage(mockReceivedPayload);

        // Giả lập delay chờ gói tin để không tốn CPU
        // Chia nhỏ để thoát nhanh khi isRunning = false
        for (int i = 0; i < 50 && isRunning; ++i) {
            Sleep(100);
        }
    }

    OutputDebugStringA("[Network] Luồng mạng đã dừng an toàn.\n");
}

// ---------------------------------------------------------
// [Luồng chính] Xử lý sự kiện (Message Loop) cho UI ngầm
// ---------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_DESTROY:
            // Khi cửa sổ ẩn bị đóng (ví dụ lúc tắt máy hoặc người dùng chọn Exit từ Tray Icon)
            isRunning = false;      // Báo cho luồng mạng dừng lại
            PostQuitMessage(0);     // Thoát Message Loop
            break;
            
        case WM_TRAYICON: {
            // lParam chứa loại sự kiện chuột
            if (lParam == WM_RBUTTONDOWN) {
                OutputDebugStringA("[UI] Nguoi dung nhan chuot phai vao Tray Icon.\n");
                
                // Tạo menu popup Exit
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Thoát Agent");
                
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hWnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
                DestroyMenu(hMenu);
            }
            else if (lParam == WM_LBUTTONDBLCLK) {
                OutputDebugStringA("[UI] Nguoi dung nhan dup chuot vao Tray Icon.\n");
                MessageBoxA(NULL, "Remote Agent đang chạy ngầm bảo vệ hệ thống.", "Thông tin", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                DestroyWindow(hWnd);
            }
            break;
        }

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Entry point cho Windows Desktop Application (Không mở Console)
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    
    // 1. Đăng ký một lớp cửa sổ (Window Class)
    const char CLASS_NAME[] = "RemoteAgentHiddenWindowClass";
    WNDCLASS wc = { };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClass(&wc)) {
        return -1;
    }

    // 2. Tạo một cửa sổ ẨN (Không truyền vào cờ WS_VISIBLE và không gọi ShowWindow)
    // Cửa sổ này dùng để duy trì Message Loop và là nơi neo giữ System Tray Icon.
    HWND hwnd = CreateWindowEx(
        0,                              // Tùy chọn kiểu mở rộng
        CLASS_NAME,                     // Tên lớp cửa sổ
        "Remote Agent Hidden Window",   // Tiêu đề (không quan trọng vì bị ẩn)
        WS_OVERLAPPEDWINDOW,            // Kiểu cửa sổ chuẩn
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL,                           // Không có cửa sổ cha
        NULL,                           // Không có Menu
        hInstance,                      // Instance hiện tại
        NULL                            // Không truyền thêm dữ liệu
    );

    if (hwnd == NULL) {
        return -1;
    }

    // 3. [MINH BẠCH - BẢO MẬT] Tạo System Tray Icon
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; 
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_SHIELD);
    strcpy_s(nid.szTip, "Remote Agent Đang Hoạt Động");
    Shell_NotifyIcon(NIM_ADD, &nid);

    // 4. [KHỞI TẠO HỆ THỐNG CON] 
    // Khởi tạo ConsentManager (Singleton) — popup xin quyền người dùng
    ConsentManager::Initialize(hInstance, "PC-001");
    
    // Khởi tạo PrivacyIndicator (Singleton) — chấm đỏ overlay
    InitializeIndicator(hInstance);

    // 5. Khởi chạy luồng Mạng (Network Thread) song song
    std::thread networkThread(NetworkWorker);

    // 6. Vòng lặp sự kiện chuẩn (Message Loop) ở luồng chính
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 7. Dọn dẹp tài nguyên trước khi thoát
    isRunning = false;

    // Xóa System Tray Icon
    Shell_NotifyIcon(NIM_DELETE, &nid);
    
    // Đợi luồng mạng kết thúc hoàn toàn mới đóng ứng dụng
    if (networkThread.joinable()) {
        networkThread.join(); 
    }

    return (int) msg.wParam;
}