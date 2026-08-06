#include "ModulePower.h"
#include <iostream>
#include <powrprof.h> // Cần thiết cho hàm SetSuspendState (Sleep)

#pragma comment(lib, "powrprof.lib")

// Cấp quyền SE_SHUTDOWN_NAME cho Process hiện tại
bool ModulePower::EnableShutdownPrivilege() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;

    // Mở Access Token của tiến trình hiện tại
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }

    // Lấy LUID (Locally Unique Identifier) cho quyền Shutdown
    if (!LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        return false;
    }

    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Cập nhật Token
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);

    // Kiểm tra xem việc cấp quyền có thành công không
    bool result = (GetLastError() == ERROR_SUCCESS);
    CloseHandle(hToken);
    
    return result;
}

// ---------------------------------------------------------
// Các hàm thực thi Power cốt lõi
// ---------------------------------------------------------
void ModulePower::ExecuteShutdown() {
    if (EnableShutdownPrivilege()) {
        // EWX_SHUTDOWN: Tắt máy
        // EWX_FORCEIFHUNG: Ép đóng các ứng dụng bị treo (Not responding)
        ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER);
    }
}

void ModulePower::ExecuteRestart() {
    if (EnableShutdownPrivilege()) {
        // EWX_REBOOT: Khởi động lại
        ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER);
    }
}

void ModulePower::ExecuteSleep() {
    // Lệnh Sleep không yêu cầu SE_SHUTDOWN_NAME
    // Tham số 1: FALSE = Sleep (TRUE = Hibernate)
    // Tham số 2: FALSE = Cho phép Wake events (như gõ phím) đánh thức máy
    // Tham số 3: FALSE = Không vô hiệu hóa các luồng đánh thức
    SetSuspendState(FALSE, FALSE, FALSE);
}

// ---------------------------------------------------------
// Xử lý logic và Đếm ngược
// ---------------------------------------------------------
void ModulePower::HandlePowerCommand(const std::string& action, std::function<void(const std::string&)> sendResponseCallback) {
    
    std::string promptMessage = "";
    
    if (action == "shutdown") {
        promptMessage = "Hệ thống sẽ TẮT MÁY sau 10 giây. Bạn có muốn hủy?";
    } else if (action == "restart") {
        promptMessage = "Hệ thống sẽ KHỞI ĐỘNG LẠI sau 10 giây. Bạn có muốn hủy?";
    } else if (action == "sleep") {
        promptMessage = "Hệ thống sẽ ĐƯA VÀO CHẾ ĐỘ NGỦ sau 10 giây. Bạn có muốn hủy?";
    } else {
        // Action không hợp lệ
        json errorRes = {
            {"module", "power"},
            {"action", action},
            {"status", "error"},
            {"message", "Lệnh power không hợp lệ"}
        };
        sendResponseCallback(errorRes.dump());
        return;
    }

    // Báo cho Server biết đang chờ User (để Server hiển thị Loading hoặc Countdown trên Web)
    json pendingRes = {
        {"module", "power"},
        {"action", action},
        {"status", "waiting_user_consent"}
    };
    sendResponseCallback(pendingRes.dump());

    // Gọi hàm hiển thị Pop-up đếm ngược từ ConsentManager
    // Lưu ý: Hàm này cần cơ chế bất đồng bộ hoặc gọi từ luồng UI để không block luồng mạng
    ConsentManager::CountdownPrompt(
        promptMessage, 
        10, // 10 giây đếm ngược
        
        // Callback: Khi user Đồng ý hoặc HẾT GIỜ (Timeout)
        [this, action, sendResponseCallback]() {
            // Gửi báo cáo thành công về Web
            json successRes = {
                {"module", "power"},
                {"action", action},
                {"status", "accepted"}
            };
            sendResponseCallback(successRes.dump());

            // Thực thi lệnh (Có thể gây mất kết nối socket ngay lập tức)
            if (action == "shutdown") this->ExecuteShutdown();
            else if (action == "restart") this->ExecuteRestart();
            else if (action == "sleep") this->ExecuteSleep();
        },

        // Callback: Khi user Hủy bỏ (Cancel)
        [action, sendResponseCallback]() {
            // Gửi báo cáo từ chối về Web
            json rejectRes = {
                {"module", "power"},
                {"action", action},
                {"status", "rejected"}
            };
            sendResponseCallback(rejectRes.dump());
        }
    );
}