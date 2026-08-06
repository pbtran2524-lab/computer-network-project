#include "NetworkRouting.h"
#include "Consent.h"
#include "OverlayDot.h"
#include "ModuleApplication.h"
#include "ModuleProcess.h"
#include "ModulePower.h"
#include "ModuleFile.h"
#include "ModuleScreen.h"
#include "ModuleWebcam.h"
#include "ModuleKeylogger.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

extern std::atomic<bool> isRunning; // Kế thừa từ WinMain ở bài trước

// ---------------------------------------------------------
// Hàm mô phỏng gửi dữ liệu qua Socket
// Trong thực tế, bạn sẽ thay bằng send() của hàm Socket hoặc thư viện WebSocket
// ---------------------------------------------------------
void SendMessageToServer(const std::string& message) {
    OutputDebugStringA(("[Socket Send] " + message + "\n").c_str());
}

// ---------------------------------------------------------
// Hàm mô phỏng gửi dữ liệu nhị phân (Binary) qua Socket
// Trong thực tế, dùng send() hoặc ws.send_binary()
// ---------------------------------------------------------
void SendBinaryToServer(const std::vector<uint8_t>& data) {
    std::string logMsg = "[Socket Send Binary] " + std::to_string(data.size()) + " bytes\n";
    OutputDebugStringA(logMsg.c_str());
}

// ---------------------------------------------------------
// Cơ chế Heartbeat (Ping/Pong), Chạy trên một luồng riêng, gửi tín hiệu mỗi 30 giây để giữ kết nối
// ---------------------------------------------------------
void HeartbeatWorker() {
    while (isRunning) {
        // Tạo gói tin Ping bằng nlohmann/json
        json pingMsg = {
            {"agent_id", "PC-001"},
            {"module", "system"},
            {"action", "ping"},
            {"payload", json::object()}
        };
        
        SendMessageToServer(pingMsg.dump());
        
        // Ngủ 30 giây (chia nhỏ để thoát nhanh khi isRunning = false)
        for (int i = 0; i < 30 && isRunning; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void StartHeartbeatThread() {
    std::thread hbThread(HeartbeatWorker);
    hbThread.detach(); // Chạy độc lập
}

// ---------------------------------------------------------
// Bộ định tuyến Trung tâm (Router)
// ---------------------------------------------------------
void RouteMessage(const std::string& rawJsonMessage) {
    try {
        // 1. Parse chuỗi JSON
        json j = json::parse(rawJsonMessage);

        // 2. Kiểm tra các trường bắt buộc để tránh lỗi crash
        if (!j.contains("module") || !j.contains("action")) {
            OutputDebugStringA("[Router Error] Gói tin thiếu 'module' hoặc 'action'\n");
            return;
        }

        std::string moduleName = j["module"].get<std::string>();
        std::string actionName = j["action"].get<std::string>();
        
        // Payload có thể rỗng, nếu không có thì gán object rỗng
        json payload = j.contains("payload") ? j["payload"] : json::object();

        // 3. Chuyển hướng tới các Module tương ứng
        if (moduleName == "power") {
            HandlePowerModule(actionName, payload);
        } 
        else if (moduleName == "app") {
            HandleAppModule(actionName, payload);
        } 
        else if (moduleName == "processes") {
            HandleProcessModule(actionName, payload);
        } 
        else if (moduleName == "screen") {
            HandleScreenModule(actionName, payload);
        } 
        else if (moduleName == "filedownload") {
            HandleFileDownloadModule(actionName, payload);
        } 
        else if (moduleName == "webcam") {
            HandleWebcamModule(actionName, payload);
        } 
        else if (moduleName == "system" && actionName == "pong") {
            OutputDebugStringA("[Heartbeat] Nhận được Pong từ Server.\n");
        }
        else if (moduleName == "keylogger") {
            HandleKeyloggerModule(actionName, payload);
        }
        else {
            OutputDebugStringA(("[Router Error] Module không được hỗ trợ: " + moduleName + "\n").c_str());
        }

    } 
    catch (json::parse_error& e) {
        // Bắt lỗi khi dữ liệu gửi tới không phải là JSON chuẩn
        OutputDebugStringA(("[JSON Parse Error] " + std::string(e.what()) + "\n").c_str());
    } 
    catch (json::type_error& e) {
        // Bắt lỗi khi trường dữ liệu không đúng kiểu (ví dụ action là số thay vì chuỗi)
        OutputDebugStringA(("[JSON Type Error] " + std::string(e.what()) + "\n").c_str());
    }
    catch (...) {
        // Bắt mọi ngoại lệ khác để đảm bảo Agent không bao giờ bị Crash
        OutputDebugStringA("[Router Error] Lỗi không xác định trong quá trình phân giải JSON.\n");
    }
}

// ---------------------------------------------------------
// Khung: Triển khai các hàm rỗng cho 7 Modules
// ---------------------------------------------------------

ModuleApplication appModule;
void HandleAppModule(const std::string& action, const json& payload) {
    if (action == "list") {
        json result = appModule.ListApplications();
        SendMessageToServer(result.dump());
    } 
    else if (action == "kill") {
        if (payload.contains("pid")) {
            DWORD pid = payload["pid"].get<DWORD>();
            json result = appModule.CloseApplication(pid);
            SendMessageToServer(result.dump());
        }
    }
    else if (action == "open") {
        if (payload.contains("name")) {
            std::string appName = payload["name"].get<std::string>();
            json result = appModule.OpenApplication(appName);
            SendMessageToServer(result.dump());
        }
    }
}

ModuleProcess processModule;
void HandleProcessModule(const std::string& action, const json& payload) {
    if (action == "list") {
        // Lưu ý: Quá trình này sẽ mất ~250ms vì phải Sleep để đo đạc độ chênh lệch CPU.
        json result = processModule.ListProcesses();
        SendMessageToServer(result.dump());
    } 
    else if (action == "kill") {
        if (payload.contains("process_id")) {
            DWORD pid = payload["process_id"].get<DWORD>();
            
            // Gọi hàm Kill Process (đã tự động check whitelist bên trong)
            json result = processModule.KillProcess(pid);
            SendMessageToServer(result.dump());
        } else {
            OutputDebugStringA("[Process] Thiếu trường process_id trong lệnh kill.\n");
        }
    }
}

ModulePower powerModule;
void HandlePowerModule(const std::string& action, const json& payload) {
    // In log để debug
    OutputDebugStringA(("[Power] Nhan lenh: " + action + "\n").c_str());

    // Truyền action và một Lambda function làm Callback để ModulePower 
    // có thể gọi ngược lại hàm SendMessageToServer của Router
    powerModule.HandlePowerCommand(action, [](const std::string& responseJson) {
        SendMessageToServer(responseJson);
    });
}

ModuleScreen screenModule;
void HandleScreenModule(const std::string& action, const json& payload) {
    if (action == "screenshot") {
        screenModule.HandleScreenshot([](const json& meta, const std::vector<uint8_t>& imgData) {
            // Gửi Metadata (Chuỗi JSON)
            SendMessageToServer(meta.dump());
            
            // Gửi Binary (Gói ảnh JPEG)
            // Ví dụ với WebSocket: ws.send_binary(imgData.data(), imgData.size());
            SendBinaryToServer(imgData);
        });
    } 
    else if (action == "start" || action == "stop") {
        screenModule.HandleLiveStream(action, [](const json& meta, const std::vector<uint8_t>& imgData) {
            SendMessageToServer(meta.dump());
            if (!imgData.empty()) {
                SendBinaryToServer(imgData);
            }
        });
    }
}

ModuleFile fileModule("D:\\Shared"); // Khởi tạo giới hạn Sandbox (Chỉ cho phép đọc/tải trong thư mục D:\Shared)
void HandleFileDownloadModule(const std::string& action, const json& payload) {
    if (action == "list") {
        std::string relativeDir = ""; 
        if (payload.contains("path")) {
            relativeDir = payload["path"].get<std::string>();
        }
        
        json result = fileModule.ListDirectory(relativeDir);
        SendMessageToServer(result.dump());
    } 
    else if (action == "read_chunk") {
        // Gói lệnh từ Web yêu cầu đọc 1 chunk
        // payload: {"path": "document.pdf", "offset": 0, "chunk_size": 1048576}
        if (payload.contains("path") && payload.contains("offset") && payload.contains("chunk_size")) {
            
            std::string path = payload["path"].get<std::string>();
            uint64_t offset = payload["offset"].get<uint64_t>();
            size_t chunkSize = payload["chunk_size"].get<size_t>();

            // Lấy cục dữ liệu từ đĩa
            std::vector<char> buffer = fileModule.ReadFileChunk(path, offset, chunkSize);
            
            if (!buffer.empty()) {
                std::vector<uint8_t> binaryData(buffer.begin(), buffer.end());
                SendBinaryToServer(binaryData);
                // Lưu ý: Không bọc Binary vào JSON vì sẽ làm phình to dung lượng do Base64.
                // Nếu dùng WebSockets, hãy dùng kiểu dữ liệu ws.send_binary(buffer.data(), buffer.size());
            }
        }
    }
}

ModuleWebcam webcamModule;
void HandleWebcamModule(const std::string& action, const json& payload) {
    if (action == "start" || action == "stop") {
        
        webcamModule.HandleWebcamCommand(action, [](const json& meta, const std::vector<uint8_t>& imgData) {
            
            // 1. Gửi chuỗi Metadata (Báo trạng thái hoặc thông tin Frame)
            SendMessageToServer(meta.dump());
            
            // 2. Gửi cục Binary (Nếu imgData không rỗng)
            if (!imgData.empty()) {
                // Ví dụ hàm gửi Binary giả định:
                SendBinaryToServer(imgData); 
            }
        });
        
    } else {
        OutputDebugStringA("[Webcam] Hành động không hợp lệ.\n");
    }
}

ModuleKeylogger keyloggerModule;
void HandleKeyloggerModule(const std::string& action, const json& payload) {
    if (action == "start" || action == "log") {
        // Lấy process_id từ payload (tùy chọn)
        DWORD pid = 0;
        if (payload.contains("process_id")) {
            pid = payload["process_id"].get<DWORD>();
        }

        // Bắt đầu ghi bàn phím — callback này được gọi tự động mỗi 5 giây
        // Consent popup + OverlayDot được xử lý bên trong ModuleKeylogger
        json result = keyloggerModule.StartLogging(
            pid,
            [](const std::string& logJson) {
                // Gửi log về server theo cùng cơ chế với các module khác
                SendMessageToServer(logJson);
            }
        );

        // Gửi phản hồi kết quả lệnh về server
        SendMessageToServer(result.dump());
    }
    else if (action == "stop") {
        json result = keyloggerModule.StopLogging();
        SendMessageToServer(result.dump());
    }
    else if (action == "flush") {
        // Network client yêu cầu flush thủ công (ngoài auto-flush 5 giây)
        json result = keyloggerModule.FlushLog();
        SendMessageToServer(result.dump());
    }
    else {
        // Hành động không hợp lệ
        json errMsg = {
            {"module",  "keylogger"},
            {"status",  "error"},
            {"message", "Hanh dong khong hop le: " + action +
                        ". Ho tro: start, log, stop, flush."}
        };
        SendMessageToServer(errMsg.dump());
        OutputDebugStringA(("[Keylogger] Hanh dong khong ho tro: " + action + "\n").c_str());
    }
}