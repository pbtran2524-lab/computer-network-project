#include "OverlayDot.h"

// ============================================================
// Singleton toàn cục – được khởi tạo từ WinMain
// ============================================================
static PrivacyIndicator* g_indicator = nullptr;

void InitializeIndicator(HINSTANCE hInstance) {
    if (!g_indicator) {
        g_indicator = new PrivacyIndicator(hInstance);
    }
}

// SetIndicatorState() – thread-safe vì dùng PostMessage
// Các module background chỉ cần gọi SetIndicatorState(STATE_FLASHING)
void SetIndicatorState(int state) {
    if (g_indicator && g_indicator->GetHwnd()) {
        // PostMessage là thread-safe (khác SendMessage)
        PostMessage(g_indicator->GetHwnd(), WM_SET_INDICATOR_STATE, (WPARAM)state, 0);
    }
}

// ============================================================
// Constructor – Đăng ký Window Class và tạo cửa sổ overlay
// ============================================================
PrivacyIndicator::PrivacyIndicator(HINSTANCE hInstance)
    : m_hInstance(hInstance), m_hwnd(NULL),
      m_currentState(STATE_OFF), m_isRedVisible(false) {

    WNDCLASS wc = {};
    wc.lpfnWndProc   = PrivacyIndicator::StaticWndProc;
    wc.hInstance     = m_hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    int width   = 20;
    int height  = 20;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int posX    = screenW - width - 30;
    int posY    = 30;

    // WS_EX_LAYERED + WS_EX_TRANSPARENT + WS_EX_TOPMOST + WS_EX_TOOLWINDOW:
    // Cửa sổ luôn nổi, trong suốt với chuột, ẩn khỏi taskbar
    m_hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        CLASS_NAME, "Privacy Indicator",
        WS_POPUP,
        posX, posY, width, height,
        NULL, NULL, m_hInstance, this
    );
}

PrivacyIndicator::~PrivacyIndicator() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

// ============================================================
// SetState – chỉ gọi từ UI thread (luồng chính)
// Nếu cần gọi từ thread khác → dùng hàm global SetIndicatorState()
// ============================================================
void PrivacyIndicator::SetState(IndicatorState newState) {
    if (!m_hwnd) return;

    m_currentState = newState;

    switch (newState) {
        case STATE_FLASHING:
            m_isRedVisible = true;
            SetTimer(m_hwnd, ID_BLINK_TIMER, 500, NULL);
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
            break;

        case STATE_SOLID:
            KillTimer(m_hwnd, ID_BLINK_TIMER);
            m_isRedVisible = true;
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
            InvalidateRect(m_hwnd, NULL, TRUE);
            break;

        case STATE_OFF:
        default:
            KillTimer(m_hwnd, ID_BLINK_TIMER);
            ShowWindow(m_hwnd, SW_HIDE);
            break;
    }
}

// ============================================================
// Static Window Procedure (cầu nối)
// ============================================================
LRESULT CALLBACK PrivacyIndicator::StaticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    PrivacyIndicator* pThis = nullptr;

    if (message == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<PrivacyIndicator*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hWnd;
    } else {
        pThis = reinterpret_cast<PrivacyIndicator*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(hWnd, message, wParam, lParam);
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

// ============================================================
// HandleMessage – xử lý message của overlay window
// ============================================================
LRESULT PrivacyIndicator::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            // Dùng Magenta làm Color Key (màu bị làm trong suốt)
            SetLayeredWindowAttributes(hWnd, RGB(255, 0, 255), 0, LWA_COLORKEY);
            break;
        }

        // [THREAD-SAFE] Nhận lệnh SetState từ thread khác qua PostMessage
        case WM_SET_INDICATOR_STATE: {
            IndicatorState newState = static_cast<IndicatorState>(wParam);
            SetState(newState);
            break;
        }

        case WM_TIMER: {
            if (wParam == ID_BLINK_TIMER) {
                m_isRedVisible = !m_isRedVisible;
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            // Tô nền Magenta (sẽ bị làm trong suốt nhờ LWA_COLORKEY)
            HBRUSH bgBrush = CreateSolidBrush(RGB(255, 0, 255));
            FillRect(hdc, &ps.rcPaint, bgBrush);
            DeleteObject(bgBrush);

            // Vẽ chấm đỏ khi STATE_SOLID hoặc khi STATE_FLASHING & m_isRedVisible
            if (m_currentState == STATE_SOLID ||
               (m_currentState == STATE_FLASHING && m_isRedVisible)) {
                HBRUSH redBrush = CreateSolidBrush(RGB(220, 30, 30));
                SelectObject(hdc, redBrush);

                HPEN nullPen = (HPEN)GetStockObject(NULL_PEN);
                SelectObject(hdc, nullPen);

                Ellipse(hdc, 0, 0, 20, 20);
                DeleteObject(redBrush);
            }

            EndPaint(hWnd, &ps);
            break;
        }

        case WM_NCHITTEST:
            // Toàn bộ cửa sổ là trong suốt với chuột (click-through)
            return HTTRANSPARENT;

        case WM_DESTROY:
            KillTimer(hWnd, ID_BLINK_TIMER);
            m_hwnd = NULL;
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}