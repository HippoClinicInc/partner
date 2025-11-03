#ifndef HIPPO_CLIENT_H
#define HIPPO_CLIENT_H

#include <string>
#include <nlohmann/json.hpp>

/**
 * Provides HTTP client functionality for Hippo API interactions.
 * This class handles authentication, token management, and API requests with automatic
 * retry mechanisms and token refresh on expiration.
 */
class HippoClient {
public:
  /**
   * Initialize the HippoClient with base URL and credentials.
   * This method must be called once before using any other API methods.
   * @param baseUrl Base URL of the Hippo API server
   * @param account User account (email)
   * @param password User password
   */
  static void Init(const std::string& baseUrl, const std::string& account, const std::string& password);

  /**
   * Confirm upload of a raw file to the Hippo server.
   * @param rawDeviceData JSON data containing device information for file confirmation
   * @return JSON response from the server
   */
  static nlohmann::json ConfirmUploadRawFile(const nlohmann::json& rawDeviceData);

  /**
   * Get S3 credentials for accessing patient-specific folders.
   * @param patientId Patient identifier used to generate S3 credentials
   * @return JSON response containing S3 access credentials
   */
  static nlohmann::json GetS3Credentials(const std::string& patientId);

private:
  /**
   * Perform login and obtain JWT token.
   * Stores the JWT token and hospital ID in static members upon successful login.
   * Throws std::runtime_error if login fails or response is invalid.
   */
  static void Login();
  
  /**
   * Get the current authentication token, performing login if necessary.
   * @return Bearer token string (e.g., "Bearer <jwt_token>")
   */
  static std::string GetToken();

  /**
   * Attempt login with automatic retry mechanism and exponential backoff.
   * @param maxLoginRetries Maximum number of login retry attempts (default: 3)
   * @return true if login succeeded, false if all retries failed
   */
  static bool LoginWithRetries(int maxLoginRetries = 3);

  /**
   * Make an HTTP request with automatic token management and retry logic.
   * Automatically refreshes token on 401 errors and retries failed requests.
   * @param method HTTP method (GET, POST, PUT, DELETE)
   * @param url Full request URL
   * @param payload JSON payload (only used for POST/PUT requests)
   * @param maxRetries Maximum number of retry attempts for failed requests
   * @return JSON response from the server
   */
  static nlohmann::json RequestWithToken(const std::string& method,
                                         const std::string& url,
                                         const nlohmann::json& payload = nlohmann::json::object(),
                                         int maxRetries = 3);

  /**
   * Low-level HTTP request function using libcurl.
   * Performs the actual HTTP request and returns parsed JSON response.
   * Handles SSL verification, timeouts, and error checking.
   * @param method HTTP method (GET, POST, PUT, DELETE)
   * @param url Full request URL
   * @param payload JSON payload (only used for POST/PUT requests)
   * @param token Authorization token (optional, format: "Bearer <token>")
   * @return JSON response (returns "data" field if present, otherwise full response)
   */
  static nlohmann::json HttpRequest(const std::string& method,
                                    const std::string& url,
                                    const nlohmann::json& payload = nlohmann::json::object(),
                                    const std::string& token = "");

private:
  static std::string base_url_;      ///< Base URL of the Hippo API server
  static std::string account_;       ///< User account (email)
  static std::string password_;      ///< User password
  static std::string jwt_token_;     ///< Current JWT authentication token
  static std::string hospital_id_;   ///< Hospital identifier from login response
};

#endif // HIPPO_CLIENT_H
