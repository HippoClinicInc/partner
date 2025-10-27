#ifndef HIPPO_CLIENT_H
#define HIPPO_CLIENT_H

#include <string>
#include <nlohmann/json.hpp>

class HippoClient {
public:
  // Initialize (called once by the business layer)
  static void Init(const std::string& baseUrl, const std::string& account, const std::string& password);

  // API
  static nlohmann::json ConfirmUploadRawFile(const nlohmann::json& rawDeviceData);

  static nlohmann::json GenerateUniqueDataId(int quantity);

  static nlohmann::json GetS3Credentials(const std::string& patientId);

private:
  // Login & token management
  static void Login();
  static std::string GetToken();

  // Login retry mechanism
  static bool LoginWithRetries(int maxLoginRetries = 3);

  // General request wrapper
  static nlohmann::json RequestWithToken(const std::string& method,
                                         const std::string& url,
                                         const nlohmann::json& payload = nlohmann::json::object(),
                                         int maxRetries = 3);

  // HTTP utility function
  static nlohmann::json HttpRequest(const std::string& method,
                                    const std::string& url,
                                    const nlohmann::json& payload = nlohmann::json::object(),
                                    const std::string& token = "");

private:
  static std::string base_url_;
  static std::string account_;
  static std::string password_;
  static std::string jwt_token_;
  static std::string hospital_id_;
};

#endif // HIPPO_CLIENT_H
