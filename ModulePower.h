#ifndef MODULE_POWER_H
#define MODULE_POWER_H

#include <windows.h>
#include <string>
#include <nlohmann/json.hpp>
#include <functional>

// Dùng ConsentManager từ Consent.h thống nhất (không tự khai báo lại)
#include "Consent.h"

using json = nlohmann::json;

class ModulePower {
private:
    bool EnableShutdownPrivilege();
    void ExecuteShutdown();
    void ExecuteRestart();
    void ExecuteSleep();

public:
    ModulePower() = default;

    // Nhận lệnh từ Router và xử lý logic đếm ngược
    void HandlePowerCommand(const std::string& action,
                            std::function<void(const std::string&)> sendResponseCallback);
};

#endif // MODULE_POWER_H