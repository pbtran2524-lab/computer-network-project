#ifndef MODULE_APPLICATION_H
#define MODULE_APPLICATION_H

#include <windows.h>
#include <string>
#include <vector>
#include <set>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class ModuleApplication {
private:
    std::vector<std::string> whitelist;

    // Helper: Chuyển chuỗi về chữ thường để so sánh không phân biệt hoa/thường
    static std::string ToLowerCase(const std::string& str);
    
    // Helper: Kiểm tra app có nằm trong whitelist không
    bool IsWhitelisted(const std::string& appName);
    
    // Helper: Lấy tên file .exe từ Process ID
    static std::string GetProcessName(DWORD pid);

    // Cấu trúc để truyền dữ liệu vào callback của EnumWindows
    struct EnumData {
        json& payloadArray;
        std::set<DWORD> seenPids; // Tránh trùng lặp PID do 1 app có nhiều cửa sổ
        EnumData(json& arr) : payloadArray(arr) {}
    };

    // Callback của Win32 API để duyệt các cửa sổ
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);

public:
    ModuleApplication(); // Constructor khởi tạo Whitelist mặc định

    // Cập nhật whitelist (có thể đọc từ file config ở hàm main rồi truyền vào)
    void SetWhitelist(const std::vector<std::string>& allowedApps);

    // Trả về JSON danh sách các cửa sổ đang hiển thị
    json ListApplications();

    // Đóng ứng dụng (Kiểm tra Whitelist trước khi TerminateProcess)
    json CloseApplication(DWORD pid);

    // Mở ứng dụng (Kiểm tra Whitelist trước khi CreateProcess)
    json OpenApplication(const std::string& appName);
};

#endif // MODULE_APPLICATION_H