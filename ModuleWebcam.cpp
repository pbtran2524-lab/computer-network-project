#include "ModuleWebcam.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>

ModuleWebcam::ModuleWebcam() : isStreaming(false) {}

ModuleWebcam::~ModuleWebcam() {
    isStreaming = false;
    if (streamThread.joinable()) {
        streamThread.join();
    }
}

// ============================================================
// Vòng lặp lấy khung hình Webcam, Nén JPEG và Gửi đi
// ============================================================
void ModuleWebcam::StreamLoop(std::function<void(const json&, const std::vector<uint8_t>&)> sendCallback) {
    cv::VideoCapture cap(0);

    if (!cap.isOpened()) {
        json errorMsg = {
            {"module", "webcam"},
            {"status", "error"},
            {"message", "Không tìm thấy hoặc không thể mở Webcam!"}
        };
        sendCallback(errorMsg, {});
        isStreaming = false;
        SetIndicatorState(STATE_OFF);
        return;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH,  1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);

    // Bật đèn đỏ nhấp nháy cảnh báo webcam đang hoạt động
    SetIndicatorState(STATE_FLASHING);

    cv::Mat frame;
    uint64_t frameIndex = 0;
    std::vector<int>     encodeParams = {cv::IMWRITE_JPEG_QUALITY, 60};
    std::vector<uint8_t> buffer;

    while (isStreaming) {
        auto start = std::chrono::high_resolution_clock::now();

        cap >> frame;
        if (frame.empty()) break; // Camera bị ngắt

        // Đảm bảo đúng kích thước
        if (frame.cols != 1280 || frame.rows != 720) {
            cv::resize(frame, frame, cv::Size(1280, 720));
        }

        // Nén JPEG trong RAM
        cv::imencode(".jpg", frame, buffer, encodeParams);

        json metadata = {
            {"module", "webcam"},
            {"action", "stream"},
            {"payload", {
                {"frame_index", frameIndex++},
                {"timestamp",   std::time(nullptr)},
                {"size_bytes",  buffer.size()}
            }}
        };

        sendCallback(metadata, buffer);

        // Căn chỉnh delay ~24 FPS (41ms/frame)
        auto end       = std::chrono::high_resolution_clock::now();
        auto durMs     = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        if (durMs < 41) {
            std::this_thread::sleep_for(std::chrono::milliseconds(41 - durMs));
        }
    }

    cap.release();
    SetIndicatorState(STATE_OFF); // Tắt đèn cảnh báo
}

// ============================================================
// Xử lý Lệnh – bắt buộc xin quyền trước khi cấp luồng
// ============================================================
void ModuleWebcam::HandleWebcamCommand(
        const std::string& action,
        std::function<void(const json&, const std::vector<uint8_t>&)> sendCallback) {

    std::lock_guard<std::mutex> lock(streamMutex);

    if (action == "stop") {
        if (isStreaming) {
            isStreaming = false;
            if (streamThread.joinable()) {
                streamThread.join();
            }
        }
        json stopMsg = {{"module", "webcam"}, {"status", "stopped"}};
        sendCallback(stopMsg, {});
        return;
    }

    if (action == "start") {
        if (isStreaming) {
            json busyMsg = {{"module", "webcam"}, {"status", "already_running"}};
            sendCallback(busyMsg, {});
            return;
        }

        // Báo server đang chờ consent
        json pendingMsg = {{"module", "webcam"}, {"status", "waiting_user_consent"}};
        sendCallback(pendingMsg, {});

        // Hiển thị popup xin quyền (ConsentManager là thread-safe singleton)
        ConsentManager::AskPermission(
            "Quản trị viên muốn mở Webcam của bạn.\nBạn có đồng ý không?",
            15,
            // onAccept
            [this, sendCallback]() {
                json acceptMsg = {{"module", "webcam"}, {"status", "accepted"}};
                sendCallback(acceptMsg, {});
                this->isStreaming = true;
                this->streamThread = std::thread(&ModuleWebcam::StreamLoop, this, sendCallback);
            },
            // onReject
            [sendCallback]() {
                json rejectMsg = {{"module", "webcam"}, {"status", "rejected"}};
                sendCallback(rejectMsg, {});
            }
        );
    } else {
        OutputDebugStringA("[Webcam] Hành động không hợp lệ.\n");
    }
}