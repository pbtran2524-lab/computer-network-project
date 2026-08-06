#ifndef MODULE_SCREEN_H
#define MODULE_SCREEN_H

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>

// Dùng ConsentManager từ Consent.h và SetIndicatorState từ OverlayDot.h
// (không khai báo lại ở đây để tránh ODR violation)
#include "Consent.h"
#include "OverlayDot.h"

using json = nlohmann::json;

class ModuleScreen {
private:
    ULONG_PTR           gdiplusToken;
    std::atomic<bool>   isStreaming;
    std::thread         streamThread;

    int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
    std::vector<uint8_t> CaptureAndCompressToMemory(int targetW, int targetH, long quality);
    void StreamLoop(std::function<void(const json&, const std::vector<uint8_t>&)> sendCallback);

public:
    ModuleScreen();
    ~ModuleScreen();

    // Chụp ảnh 1 lần (Screenshot) — hiển thị chấm đỏ SOLID
    void HandleScreenshot(std::function<void(const json&, const std::vector<uint8_t>&)> sendCallback);

    // Bật/Tắt Live Screen — hiển thị chấm đỏ FLASHING, yêu cầu consent
    void HandleLiveStream(const std::string& action,
                          std::function<void(const json&, const std::vector<uint8_t>&)> sendCallback);
};

#endif // MODULE_SCREEN_H