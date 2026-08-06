#include "ModuleScreen.h"
#include <iostream>
#include <chrono>
#include <objbase.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

using namespace Gdiplus;

// ============================================================
// Khởi tạo và Dọn dẹp GDI+
// ============================================================
ModuleScreen::ModuleScreen() : isStreaming(false) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

ModuleScreen::~ModuleScreen() {
    isStreaming = false;
    if (streamThread.joinable()) {
        streamThread.join();
    }
    GdiplusShutdown(gdiplusToken);
}

// ============================================================
// Helper: Tìm Encoder (ví dụ: "image/jpeg")
// ============================================================
int ModuleScreen::GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (!pImageCodecInfo) return -1;
    GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

// ============================================================
// Lõi: Chụp màn hình, Resize, Nén JPEG vào RAM
// ============================================================
std::vector<uint8_t> ModuleScreen::CaptureAndCompressToMemory(int targetW, int targetH, long quality) {
    HWND hDesktop  = GetDesktopWindow();
    HDC  hScreenDC = GetDC(hDesktop);
    HDC  hMemoryDC = CreateCompatibleDC(hScreenDC);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    HBITMAP hBitmap    = CreateCompatibleBitmap(hScreenDC, screenW, screenH);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    BitBlt(hMemoryDC, 0, 0, screenW, screenH, hScreenDC, 0, 0, SRCCOPY);

    Bitmap originalBitmap(hBitmap, NULL);

    // Resize về độ phân giải mục tiêu
    Bitmap resizedBitmap(targetW, targetH, PixelFormat24bppRGB);
    Graphics graphics(&resizedBitmap);
    graphics.SetInterpolationMode(InterpolationModeBilinear);
    graphics.DrawImage(&originalBitmap, Rect(0, 0, targetW, targetH));

    // Tạo IStream trong bộ nhớ
    IStream* pStream = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &pStream);

    // Nén JPEG với quality tùy chỉnh
    CLSID jpegClsid;
    GetEncoderClsid(L"image/jpeg", &jpegClsid);

    EncoderParameters encoderParams;
    encoderParams.Count                        = 1;
    encoderParams.Parameter[0].Guid            = EncoderQuality;
    encoderParams.Parameter[0].Type            = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues  = 1;
    encoderParams.Parameter[0].Value           = &quality;

    resizedBitmap.Save(pStream, &jpegClsid, &encoderParams);

    // Trích xuất bytes từ IStream
    STATSTG statstg;
    pStream->Stat(&statstg, STATFLAG_NONAME);
    size_t streamSize = statstg.cbSize.QuadPart;

    std::vector<uint8_t> buffer(streamSize);
    LARGE_INTEGER liZero = {};
    pStream->Seek(liZero, STREAM_SEEK_SET, NULL);

    ULONG bytesRead = 0;
    pStream->Read(buffer.data(), (ULONG)streamSize, &bytesRead);

    // Dọn dẹp tài nguyên GDI
    pStream->Release();
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(hDesktop, hScreenDC);

    return buffer;
}

// ============================================================
// Chụp ảnh tĩnh (Screenshot) – chấm đỏ SOLID
// ============================================================
void ModuleScreen::HandleScreenshot(
        std::function<void(const json&, const std::vector<uint8_t>&)> sendCallback) {

    // Thông báo cho người dùng biết trước khi chụp
    ConsentManager::CountdownPrompt(
        "Quản trị viên muốn chụp ảnh màn hình của bạn.\nBạn có đồng ý không?",
        5,
        // onAccept: Chụp ảnh
        [this, sendCallback]() {
            SetIndicatorState(STATE_SOLID); // Đèn đỏ đứng yên trong khi chụp

            auto imgData = CaptureAndCompressToMemory(1280, 720, 80);

            json metadata = {
                {"module", "screen"},
                {"action", "screenshot_result"},
                {"payload", {
                    {"timestamp",  std::time(nullptr)},
                    {"size_bytes", imgData.size()}
                }}
            };

            sendCallback(metadata, imgData);

            SetIndicatorState(STATE_OFF); // Tắt đèn sau khi xong
        },
        // onReject: Từ chối chụp
        [sendCallback]() {
            json rejectMsg = {{"module", "screen"}, {"status", "rejected"}};
            sendCallback(rejectMsg, {});
        }
    );
}

// ============================================================
// Vòng lặp Live Stream (24 FPS) – chấm đỏ FLASHING
// ============================================================
void ModuleScreen::StreamLoop(
        std::function<void(const json&, const std::vector<uint8_t>&)> sendCallback) {

    uint64_t frameIndex = 0;
    SetIndicatorState(STATE_FLASHING); // Đèn nhấp nháy khi streaming

    while (isStreaming) {
        auto start = std::chrono::high_resolution_clock::now();

        auto imgData = CaptureAndCompressToMemory(1280, 720, 60);

        json metadata = {
            {"module", "screen"},
            {"action", "stream"},
            {"payload", {
                {"frame_index", frameIndex++},
                {"timestamp",   std::time(nullptr)},
                {"size_bytes",  imgData.size()}
            }}
        };

        sendCallback(metadata, imgData);

        auto end     = std::chrono::high_resolution_clock::now();
        auto durMs   = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        // ~24 FPS = 41ms/frame
        if (durMs < 41) {
            std::this_thread::sleep_for(std::chrono::milliseconds(41 - durMs));
        }
    }

    SetIndicatorState(STATE_OFF); // Tắt đèn khi dừng stream
}

// ============================================================
// Xử lý Start/Stop Stream – hiển thị popup xin quyền trước
// ============================================================
void ModuleScreen::HandleLiveStream(
        const std::string& action,
        std::function<void(const json&, const std::vector<uint8_t>&)> sendCallback) {

    if (action == "stop") {
        isStreaming = false;
        if (streamThread.joinable()) {
            streamThread.join();
        }
        json stopMsg = {{"module", "screen"}, {"status", "stopped"}};
        sendCallback(stopMsg, {});
        return;
    }

    if (action == "start") {
        if (isStreaming) {
            json busyMsg = {{"module", "screen"}, {"status", "already_running"}};
            sendCallback(busyMsg, {});
            return;
        }

        // Báo server đang chờ consent của người dùng
        json pendingMsg = {{"module", "screen"}, {"status", "waiting_user_consent"}};
        sendCallback(pendingMsg, {});

        // Hiển thị popup đếm ngược xin phép
        ConsentManager::CountdownPrompt(
            "Quản trị viên muốn truyền trực tiếp màn hình của bạn.\nBạn có đồng ý không?",
            15,
            // onAccept
            [this, sendCallback]() {
                json acceptMsg = {{"module", "screen"}, {"status", "accepted"}};
                sendCallback(acceptMsg, {});
                this->isStreaming  = true;
                this->streamThread = std::thread(&ModuleScreen::StreamLoop, this, sendCallback);
            },
            // onReject/timeout
            [sendCallback]() {
                json rejectMsg = {{"module", "screen"}, {"status", "rejected"}};
                sendCallback(rejectMsg, {});
            }
        );
    }
}