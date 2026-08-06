#ifndef MODULE_PROCESS_H
#define MODULE_PROCESS_H

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class ModuleProcess {
private:
    std::vector<std::string> whitelist;

    struct ProcessCpuRecord {
        ULONGLONG lastSystemTime;
        ULONGLONG lastProcessTime;
    };
    std::unordered_map<DWORD, ProcessCpuRecord> cpuRecords;

    static std::string ToLowerCase(const std::string& str);
    bool IsWhitelisted(const std::string& processName);

    // Tên hàm thống nhất với implementation trong .cpp
    static uint64_t FileTimeToUInt64(const FILETIME& ft);

public:
    ModuleProcess();

    void SetWhitelist(const std::vector<std::string>& allowedProcesses);

    // Quét toàn bộ Process, tính RAM/CPU và trả về JSON chuẩn
    json ListProcesses();

    // Terminate một process (kiểm tra whitelist trước)
    json KillProcess(DWORD pid);
};

#endif // MODULE_PROCESS_H