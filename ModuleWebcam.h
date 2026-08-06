#ifndef MODULE_WEBCAM_H
#define MODULE_WEBCAM_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp>

// OpenCV chỉ cần trong .cpp, nhưng cần khai báo ở đây để compiler biết kiểu dữ liệu
// Bao gồm Consent & OverlayDot thay vì khai báo lại ConsentManager
#include "Consent.h"
#include "OverlayDot.h"

using json = nlohmann::json;

class ModuleWebcam {
private:
    std::atomic<bool>   isStreaming;
    std::thread         streamThread;
    std::mutex          streamMutex;

    void StreamLoop(std::function<void(const json&, const std::vector<uint8_t>&)> sendCallback);

public:
    ModuleWebcam();
    ~ModuleWebcam();

    // Nhận lệnh start/stop từ Router
    void HandleWebcamCommand(const std::string& action,
                             std::function<void(const json&, const std::vector<uint8_t>&)> sendCallback);
};

#endif // MODULE_WEBCAM_H