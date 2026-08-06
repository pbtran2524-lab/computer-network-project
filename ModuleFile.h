#ifndef MODULE_FILE_H
#define MODULE_FILE_H

#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

class ModuleFile {
private:
    fs::path sandboxDir;

    // Kỹ thuật cốt lõi chống Directory Traversal (../)
    bool IsPathSafe(const fs::path& targetPath);
    
    // Tính toán kích thước thư mục (Quét đệ quy)
    uintmax_t CalculateFolderSize(const fs::path& folderPath);

public:
    // Khởi tạo với thư mục Sandbox mặc định
    ModuleFile(const std::string& allowedRootPath);

    // Thay đổi thư mục Sandbox
    void SetSandboxDir(const std::string& newPath);

    // Liệt kê file và thư mục
    json ListDirectory(const std::string& relativePath);

    // Đọc file nhị phân theo từng Chunk (để gửi qua socket tránh tràn RAM)
    std::vector<char> ReadFileChunk(const std::string& relativePath, uint64_t offset, size_t chunkSize);
};

#endif // MODULE_FILE_H