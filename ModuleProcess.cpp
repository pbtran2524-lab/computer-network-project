#include "ModuleProcess.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <map>
#include <cctype>

// ---------------------------------------------------------
// Constructor & Helpers
// ---------------------------------------------------------
ModuleProcess::ModuleProcess() {
    // Whitelist mặc định, bạn có thể thiết lập lại từ config
    whitelist = {"chrome.exe", "explorer.exe", "notepad.exe"};
}

void ModuleProcess::SetWhitelist(const std::vector<std::string>& allowedProcesses) {
    whitelist = allowedProcesses;
}

std::string ModuleProcess::ToLowerCase(const std::string& str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);
    return lowerStr;
}

bool ModuleProcess::IsWhitelisted(const std::string& processName) {
    std::string lowerName = ToLowerCase(processName);
    for (const auto& allowed : whitelist) {
        if (ToLowerCase(allowed) == lowerName) {
            return true;
        }
    }
    return false;
}

uint64_t ModuleProcess::FileTimeToUInt64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

// ---------------------------------------------------------
// Lấy danh sách, RAM và CPU Percent
// ---------------------------------------------------------
json ModuleProcess::ListProcesses() {
    json response;
    response["module"] = "processes";
    response["action"] = "list";
    json payloadArray = json::array();

    // 1. Lấy số lượng nhân CPU (Cores) để chia trung bình % CPU
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numProcessors = sysInfo.dwNumberOfProcessors;
    if (numProcessors == 0) numProcessors = 1;

    // 2. Chụp Snapshot danh sách Process
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        response["payload"] = payloadArray; // Trả mảng rỗng nếu lỗi
        return response;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    struct ProcessData {
        DWORD pid;
        std::string name;
        uint64_t cpuTimeT1 = 0;
        uint64_t cpuTimeT2 = 0;
        SIZE_T ramBytes = 0;
        HANDLE hProcess = NULL;
    };
    std::vector<ProcessData> trackedProcs;

    // 3. Lần 1: Lấy danh sách và thời điểm (T1)
    FILETIME sysIdle, sysKernel, sysUser;
    GetSystemTimes(&sysIdle, &sysKernel, &sysUser);
    uint64_t sysTimeT1 = FileTimeToUInt64(sysKernel) + FileTimeToUInt64(sysUser);

    if (Process32First(hSnapshot, &pe32)) {
        do {
            std::string procName = pe32.szExeFile;
            // Chỉ theo dõi các Process trong Whitelist
            if (IsWhitelisted(procName)) {
                ProcessData pData;
                pData.pid = pe32.th32ProcessID;
                pData.name = procName;
                
                // Cần quyền PROCESS_QUERY_LIMITED_INFORMATION để đọc CPU/RAM mà ít bị chặn (Access Denied)
                pData.hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pData.pid);
                
                if (pData.hProcess != NULL) {
                    FILETIME pCreation, pExit, pKernel, pUser;
                    if (GetProcessTimes(pData.hProcess, &pCreation, &pExit, &pKernel, &pUser)) {
                        pData.cpuTimeT1 = FileTimeToUInt64(pKernel) + FileTimeToUInt64(pUser);
                    }
                    trackedProcs.push_back(pData);
                }
            }
        } while (Process32Next(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);

    // 4. Tạm dừng một khoảng (250ms) để đo tốc độ CPU tiêu thụ
    Sleep(250);

    // 5. Lần 2: Lấy thời điểm hệ thống (T2)
    GetSystemTimes(&sysIdle, &sysKernel, &sysUser);
    uint64_t sysTimeT2 = FileTimeToUInt64(sysKernel) + FileTimeToUInt64(sysUser);
    uint64_t sysTimeDelta = sysTimeT2 - sysTimeT1;
    if (sysTimeDelta == 0) sysTimeDelta = 1; // Tránh chia cho 0

    // 6. Tính toán kết quả cho từng Process
    for (auto& p : trackedProcs) {
        FILETIME pCreation, pExit, pKernel, pUser;
        
        // Đo CPU (T2)
        if (GetProcessTimes(p.hProcess, &pCreation, &pExit, &pKernel, &pUser)) {
            p.cpuTimeT2 = FileTimeToUInt64(pKernel) + FileTimeToUInt64(pUser);
            uint64_t procTimeDelta = p.cpuTimeT2 - p.cpuTimeT1;
            
            // Công thức tính CPU Percent (Chuẩn Task Manager)
            double cpuPercent = (double)(procTimeDelta * 100) / (double)sysTimeDelta;
            cpuPercent = cpuPercent / numProcessors; 
            
            // Làm tròn 1 chữ số thập phân
            cpuPercent = std::round(cpuPercent * 10.0) / 10.0;

            // Đo RAM (Bytes)
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(p.hProcess, &pmc, sizeof(pmc))) {
                p.ramBytes = pmc.WorkingSetSize; // Đơn vị: Byte
            }

            // Đóng gói JSON
            payloadArray.push_back({
                {"pid", p.pid},
                {"name", p.name},
                {"ram_bytes", p.ramBytes},
                {"cpu_percent", cpuPercent}
            });
        }
        CloseHandle(p.hProcess); // Nhớ đóng handle
    }

    response["payload"] = payloadArray;
    return response;
}

// ---------------------------------------------------------
// Terminate Process (Kill)
// ---------------------------------------------------------
json ModuleProcess::KillProcess(DWORD pid) {
    json response;
    response["module"] = "processes";
    response["action"] = "kill_result";
    response["payload"] = { {"pid", pid} };

    // Bước 1: Mở Process để kiểm tra tên (xem có nằm trong Whitelist không)
    HANDLE hCheck = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hCheck) {
        response["payload"]["status"] = "error";
        response["payload"]["message"] = "Không thể mở Process ID này (có thể đã bị đóng hoặc lỗi quyền).";
        return response;
    }

    char szProcessName[MAX_PATH] = "<unknown>";
    HMODULE hMod;
    DWORD cbNeeded;
    if (EnumProcessModules(hCheck, &hMod, sizeof(hMod), &cbNeeded)) {
        GetModuleBaseNameA(hCheck, hMod, szProcessName, sizeof(szProcessName));
    }
    CloseHandle(hCheck);

    // Bước 2: Kiểm tra Whitelist
    if (!IsWhitelisted(std::string(szProcessName))) {
        response["payload"]["status"] = "rejected";
        response["payload"]["message"] = "Lệnh bị từ chối: Process không nằm trong Whitelist!";
        return response;
    }

    // Bước 3: Nếu hợp lệ, mở Process với quyền TERMINATE
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess != NULL) {
        if (TerminateProcess(hProcess, 0)) { // 0 là mã thoát (exit code)
            response["payload"]["status"] = "success";
            response["payload"]["message"] = "Đã buộc dừng thành công.";
        } else {
            response["payload"]["status"] = "error";
            response["payload"]["message"] = "Hệ điều hành từ chối Terminate (Lỗi: " + std::to_string(GetLastError()) + ")";
        }
        CloseHandle(hProcess);
    } else {
        response["payload"]["status"] = "error";
        response["payload"]["message"] = "Không đủ quyền (Access Denied) để Terminate.";
    }

    return response;
}