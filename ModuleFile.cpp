#include "ModuleFile.h"
#include <fstream>
#include <iostream>

// ---------------------------------------------------------
// Constructor & Cấu hình Sandbox
// ---------------------------------------------------------
ModuleFile::ModuleFile(const std::string& allowedRootPath) {
    SetSandboxDir(allowedRootPath);
}

void ModuleFile::SetSandboxDir(const std::string& newPath) {
    // Chuyển đổi thành đường dẫn chuẩn tắc tuyệt đối (ví dụ: D:\Shared)
    sandboxDir = fs::weakly_canonical(newPath);
    
    // Nếu thư mục sandbox chưa tồn tại, tạo mới nó
    if (!fs::exists(sandboxDir)) {
        fs::create_directories(sandboxDir);
    }
}

// ---------------------------------------------------------
// Chống Directory Traversal (Bảo mật cốt lõi)
// ---------------------------------------------------------
bool ModuleFile::IsPathSafe(const fs::path& targetPath) {
    // weakly_canonical sẽ tự động hóa giải các chuỗi "../"
    // Ví dụ: D:\Shared\..\Windows -> D:\Windows
    fs::path absoluteTarget = fs::weakly_canonical(targetPath);
    
    std::string sandboxStr = sandboxDir.string();
    std::string targetStr = absoluteTarget.string();

    // Đảm bảo targetStr bắt đầu bằng sandboxStr
    // D:\Windows KHÔNG bắt đầu bằng D:\Shared -> Trả về false (Chặn!)
    return targetStr.find(sandboxStr) == 0;
}

// ---------------------------------------------------------
// Tính kích thước Folder đệ quy
// ---------------------------------------------------------
uintmax_t ModuleFile::CalculateFolderSize(const fs::path& folderPath) {
    uintmax_t totalSize = 0;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
            if (fs::is_regular_file(entry.status())) {
                totalSize += fs::file_size(entry);
            }
        }
    } catch (const fs::filesystem_error& e) {
        // Bỏ qua các file không có quyền truy cập
        OutputDebugStringA(("[File] Lỗi truy cập tính size folder: " + std::string(e.what()) + "\n").c_str());
    }
    return totalSize;
}

// ---------------------------------------------------------
// Liệt kê File & Folder (Action: List)
// ---------------------------------------------------------
json ModuleFile::ListDirectory(const std::string& relativePath) {
    json response;
    response["module"] = "file";
    response["action"] = "list";
    json payloadArray = json::array();

    fs::path targetPath = sandboxDir / relativePath;

    // [BẢO MẬT] Kiểm tra tính hợp lệ của đường dẫn
    if (!IsPathSafe(targetPath) || !fs::exists(targetPath) || !fs::is_directory(targetPath)) {
        response["status"] = "error";
        response["message"] = "Đường dẫn không hợp lệ hoặc vượt quyền cho phép!";
        response["payload"] = payloadArray;
        return response;
    }

    // Duyệt thư mục (Chỉ 1 cấp, không đệ quy)
    try {
        for (const auto& entry : fs::directory_iterator(targetPath)) {
            json item;
            item["name"] = entry.path().filename().string();
            
            if (fs::is_directory(entry.status())) {
                item["type"] = "folder";
                item["size_bytes"] = CalculateFolderSize(entry.path());
            } 
            else if (fs::is_regular_file(entry.status())) {
                item["type"] = "file";
                item["size_bytes"] = fs::file_size(entry.path());
            }

            payloadArray.push_back(item);
        }
        response["status"] = "success";
    } 
    catch (const std::exception& e) {
        response["status"] = "error";
        response["message"] = e.what();
    }

    response["payload"] = payloadArray;
    return response;
}

// ---------------------------------------------------------
// Đọc File theo Chunk (Action: Download)
// ---------------------------------------------------------
std::vector<char> ModuleFile::ReadFileChunk(const std::string& relativePath, uint64_t offset, size_t chunkSize) {
    fs::path targetPath = sandboxDir / relativePath;

    // [BẢO MẬT] Chặn ngay lập tức nếu ra khỏi Sandbox
    if (!IsPathSafe(targetPath) || !fs::is_regular_file(targetPath)) {
        OutputDebugStringA("[File] Vi phạm bảo mật hoặc không phải file.\n");
        return {}; // Trả về mảng rỗng
    }

    // Mở file nhị phân
    std::ifstream file(targetPath, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    // Nhảy tới vị trí (offset) cần đọc
    file.seekg(offset, std::ios::beg);
    if (file.fail()) {
        return {}; // Offset vượt quá file
    }

    // Khởi tạo buffer và tiến hành đọc
    std::vector<char> buffer(chunkSize);
    file.read(buffer.data(), chunkSize);
    
    // Kiểm tra xem đã đọc được bao nhiêu byte thực tế 
    // (nếu file còn ít hơn chunkSize thì sẽ đọc không đủ)
    size_t bytesRead = file.gcount();
    buffer.resize(bytesRead); // Cắt mảng lại cho vừa vặn để tiết kiệm RAM

    return buffer;
}