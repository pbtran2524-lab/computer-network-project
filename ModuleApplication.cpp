#include "ModuleApplication.h"
#include <psapi.h>
#include <algorithm>
#include <cctype>
#include <iostream>

#pragma comment(lib, "psapi.lib")

// ---------------------------------------------------------
// Constructor & Cấu hình
// ---------------------------------------------------------
ModuleApplication::ModuleApplication() {
    // Whitelist mặc định cho mục đích học tập/nghiên cứu
    whitelist = {"notepad.exe", "calc.exe", "mspaint.exe"};
}

void ModuleApplication::SetWhitelist(const std::vector<std::string>& allowedApps) {
    whitelist = allowedApps;
}

// ---------------------------------------------------------
// Các hàm Helpers nội bộ
// ---------------------------------------------------------
std::string ModuleApplication::ToLowerCase(const std::string& str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);
    return lowerStr;
}

bool ModuleApplication::IsWhitelisted(const std::string& appName) {
    std::string lowerApp = ToLowerCase(appName);
    for (const auto& allowed : whitelist) {
        if (ToLowerCase(allowed) == lowerApp) {
            return true;
        }
    }
    return false;
}

std::string ModuleApplication::GetProcessName(DWORD pid) {
    std::string processName = "";
    // Yêu cầu quyền đọc thông tin process để lấy tên
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProcess != NULL) {
        char szProcessName[MAX_PATH];
        HMODULE hMod;
        DWORD cbNeeded;
        
        // Lấy module đầu tiên của Process (chính là file thực thi .exe)
        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
            if (GetModuleBaseNameA(hProcess, hMod, szProcessName, sizeof(szProcessName))) {
                processName = szProcessName;
            }
        }
        CloseHandle(hProcess);
    }
    return processName;
}

// ---------------------------------------------------------
// Logic Lấy danh sách Ứng dụng (List)
// ---------------------------------------------------------
BOOL CALLBACK ModuleApplication::EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    // 1. Bỏ qua các cửa sổ ẩn (background processes của Windows)
    if (!IsWindowVisible(hwnd)) {
        return TRUE; 
    }

    // 2. Bỏ qua các cửa sổ không có tiêu đề (title) - thường không phải là app của người dùng
    int length = GetWindowTextLengthA(hwnd);
    if (length == 0) {
        return TRUE;
    }

    // Lấy PID từ Window Handle
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);

    EnumData* data = reinterpret_cast<EnumData*>(lParam);

    // Kiểm tra xem PID này đã được thêm vào JSON chưa (vì 1 app như Chrome có thể mở nhiều cửa sổ)
    if (data->seenPids.find(pid) == data->seenPids.end()) {
        std::string procName = GetProcessName(pid);
        
        if (!procName.empty()) {
            data->seenPids.insert(pid);
            
            // Push object vào mảng JSON
            data->payloadArray.push_back({
                {"pid", pid},
                {"name", procName}
            });
        }
    }
    return TRUE; // Trả về TRUE để tiếp tục duyệt cửa sổ tiếp theo
}

json ModuleApplication::ListApplications() {
    json response;
    response["module"] = "application";
    response["action"] = "list";
    
    json payloadArray = json::array();
    EnumData data(payloadArray);
    
    // Gọi API của Windows, truyền địa chỉ của struct data vào lParam
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
    
    response["payload"] = payloadArray;
    return response;
}

// ---------------------------------------------------------
// Logic Đóng Ứng dụng (Kill Process) - Tích hợp Whitelist
// ---------------------------------------------------------
json ModuleApplication::CloseApplication(DWORD pid) {
    json response;
    response["module"] = "application";
    response["action"] = "close_result";
    response["payload"] = { {"pid", pid} };

    std::string appName = GetProcessName(pid);
    
    if (appName.empty()) {
        response["payload"]["status"] = "error";
        response["payload"]["message"] = "Không tìm thấy tiến trình hoặc không đủ quyền đọc";
        return response;
    }

    // [BẢO MẬT] Kiểm tra Whitelist trước khi hành động
    if (!IsWhitelisted(appName)) {
        response["payload"]["status"] = "rejected";
        response["payload"]["message"] = "Từ chối truy cập: " + appName + " không nằm trong Whitelist";
        return response;
    }

    // Xin quyền TERMINATE để đóng tiến trình
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess != NULL) {
        if (TerminateProcess(hProcess, 0)) {
            response["payload"]["status"] = "success";
            response["payload"]["message"] = "Đã đóng " + appName;
        } else {
            response["payload"]["status"] = "error";
            response["payload"]["message"] = "Lỗi hệ điều hành khi TerminateProcess";
        }
        CloseHandle(hProcess);
    } else {
        response["payload"]["status"] = "error";
        response["payload"]["message"] = "Access Denied: Ứng dụng có thể chạy quyền Admin";
    }

    return response;
}

// ---------------------------------------------------------
// Logic Mở Ứng dụng (Open Process) - Tích hợp Whitelist
// ---------------------------------------------------------
json ModuleApplication::OpenApplication(const std::string& appName) {
    json response;
    response["module"] = "application";
    response["action"] = "open_result";
    response["payload"] = { {"name", appName} };

    if (!IsWhitelisted(appName)) {
        response["payload"]["status"] = "rejected";
        response["payload"]["message"] = "Từ chối truy cập: Không nằm trong Whitelist";
        return response;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Khởi chạy ứng dụng
    if (CreateProcessA(NULL, (LPSTR)appName.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        response["payload"]["status"] = "success";
        response["payload"]["pid"] = (int)pi.dwProcessId;
        
        // Đóng handle không cần thiết để tránh rò rỉ bộ nhớ
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        response["payload"]["status"] = "error";
        response["payload"]["message"] = "Lỗi CreateProcess. File có thể không nằm trong PATH.";
    }

    return response;
}