#include "hippo_client.h"
#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <stdexcept>

using json = nlohmann::json;

// Static member initialization
std::string HippoClient::base_url_;
std::string HippoClient::account_;
std::string HippoClient::password_;
std::string HippoClient::jwt_token_;
std::string HippoClient::hospital_id_;
const std::string HippoClient::HTTP_STATUS_UNAUTHORIZED = "401";

// Public interface
void HippoClient::Init(const std::string& baseUrl, const std::string& account, const std::string& password) {
    base_url_ = baseUrl;
    account_ = account;
    password_ = password;
    std::cout << "[HippoClient] Initialized with account=" << account_ << std::endl;
}

json HippoClient::ConfirmUploadRawFile(const json& rawDeviceData) {
    std::string url = base_url_ + "/hippo/thirdParty/file/confirmUploadRawFile";
    json response = RequestWithToken("POST", url, rawDeviceData);
    std::cout << "[confirm_upload_raw_file] response:\n" << response.dump(2) << std::endl;
    return response;
}

json HippoClient::ConfirmIncrementalUploadFile(const json& payload) {
    std::string url = base_url_ + "/hippo/thirdParty/file/confirmIncrementalUploadFile";
    json response = RequestWithToken("POST", url, payload);
    std::cout << "[confirm_incremental_upload_file] response:\n" << response.dump(2) << std::endl;
    return response;
}

json HippoClient::GetS3Credentials(const std::string& patientId) {
    std::string url = base_url_ + "/hippo/thirdParty/file/getS3Credentials";
    json payload = {
        {"keyId", patientId},
        // Indicates obtaining credentials to access a patient folder.
        {"resourceType", 2}
    };

    json response = RequestWithToken("POST", url, payload);
    std::cout << "[get_s3_credentials] response:\n" << response << std::endl;
    return response;
}

// Private methods
void HippoClient::Login() {
    std::string url = base_url_ + "/hippo/thirdParty/user/login";
    json payload = {
        {"userMessage", {{"email", account_}}},
        {"password", password_}
    };

    json response = HttpRequest("POST", url, payload);

    if (!response.contains("jwtToken")) {
        throw std::runtime_error("Login failed: missing jwtToken in response");
    }

    jwt_token_ = response["jwtToken"];

    // Extract hospital ID from user info
    if (!response.contains("userInfo") || !response["userInfo"].contains("hospitalId")) {
        throw std::runtime_error("Login failed: missing hospitalId in response");
    }
    hospital_id_ = response["userInfo"]["hospitalId"].get<std::string>();

    std::cout << "[HippoClient] Login success, jwt_token=" << jwt_token_
              << ", hospital_id=" << hospital_id_ << std::endl;
}

std::string HippoClient::GetToken() {
    if (jwt_token_.empty()) Login();
    return "Bearer " + jwt_token_;
}

// Login retry mechanism
bool HippoClient::LoginWithRetries(int maxLoginRetries) {
    int attempt = 0;
    while (attempt < maxLoginRetries) {
        try {
            Login(); // Attempt login
            return true; // Login successful
        } catch (const std::exception& error) {
            std::cerr << "[HippoClient] Login attempt " << (attempt + 1)
                      << " failed: " << error.what() << std::endl;
            attempt++;

            // Exponential backoff: 2^attempt seconds (2s, 4s, 8s, ...)
            int sleep_time = 1 << attempt;
            if (attempt < maxLoginRetries) {
                std::cerr << "[HippoClient] Retrying login after " << sleep_time << "s..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(sleep_time));
            }
        }
    }
    return false; // All login attempts failed
}

// General request wrapper
json HippoClient::RequestWithToken(const std::string& method,
                                   const std::string& url,
                                   const json& payload,
                                   int maxRetries) {
    int attempt = 0;
    while (attempt < maxRetries) {
        try {
            std::string token = GetToken();
            std::cout << "[HippoClient] Making request to: " << url << std::endl;
            json response = HttpRequest(method, url, payload, token);
            return response;
        } catch (const std::exception& error) {
            std::string error_message = error.what();
            std::cerr << "[HippoClient] Request failed (attempt " << (attempt + 1)
                      << ") for URL=" << url << ": " << error_message << std::endl;

            // Check if error is due to expired/invalid token (401 Unauthorized)
            if (error_message.find(HTTP_STATUS_UNAUTHORIZED) != std::string::npos) {
                std::cerr << "[HippoClient] Token expired, attempting re-login..." << std::endl;
                jwt_token_.clear();
                if (!LoginWithRetries()) {
                    throw std::runtime_error("Login failed after retries, cannot refresh token");
                }
                // Continue to retry the request with new token (don't increment attempt)
                continue;
            }

            // For other errors, retry with exponential backoff
            attempt++;
            if (attempt >= maxRetries) {
                throw; // Re-throw the last exception
            }

            // Exponential backoff: 2^attempt seconds
            int sleep_time = 1 << attempt;
            std::cerr << "[HippoClient] Retrying after " << sleep_time << "s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(sleep_time));
        }
    }
    throw std::runtime_error("Request failed after " + std::to_string(maxRetries) + " retries for URL=" + url);
}

/**
 * libcurl callback function to receive HTTP response body content.
 * This callback is invoked by libcurl for each chunk of response data received.
 * @param contents Pointer to the received data chunk
 * @param size Size of each data element
 * @param nmemb Number of data elements
 * @param user_pointer Pointer to user-provided data (std::string* in our case)
 * @return Total number of bytes processed (must equal size * nmemb)
 */
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* user_pointer) {
    size_t total_size = size * nmemb;
    std::string* response_buffer = static_cast<std::string*>(user_pointer);
    response_buffer->append(static_cast<char*>(contents), total_size);
    return total_size;
}

/**
 * Unified HTTP request function (supports GET/POST/PUT/DELETE).
 * Performs HTTP request using libcurl with SSL verification, timeout handling,
 * and automatic JSON parsing. Returns the "data" field if present, otherwise
 * returns the full JSON response.
 * @param method  HTTP method, e.g., "GET", "POST", "PUT", "DELETE"
 * @param url     Full request URL
 * @param payload JSON payload (only used for POST/PUT requests)
 * @param token   Authorization token (optional, format: "Bearer <token>")
 * @return json   Parsed JSON response (returns "data" field if present, otherwise full response)
 */
json HippoClient::HttpRequest(const std::string& method,
                              const std::string& url,
                              const json& payload,
                              const std::string& token) {
    // 1. Initialize CURL handle
    CURL* curl_handle = curl_easy_init();
    if (!curl_handle) {
        throw std::runtime_error("Failed to initialize curl handle");
    }

    std::string response_string;         // Buffer to store server response
    struct curl_slist* headers = nullptr; // HTTP header list
    std::string payload_string;           // Serialized JSON payload (ensures scope)

    // 2. Construct HTTP headers
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    headers = curl_slist_append(headers, "Accept: application/json");

    // 3. Add Authorization header if token is provided
    if (!token.empty()) {
        std::string auth_header = "Authorization: " + token;
        headers = curl_slist_append(headers, auth_header.c_str());
    }

    // 4. Configure CURL options: URL, method, headers, and response handling
    curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, method.c_str());   // Supports GET/POST/PUT/DELETE
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);             // Set HTTP headers
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);    // Response data callback
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &response_string);     // Write response to buffer

    // 5. Security settings: enable HTTPS certificate verification (required in production)
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 1L); // Verify SSL certificate
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L); // Verify hostname matches certificate

    // 6. Timeout settings (prevent long blocking)
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);        // Total request timeout: 30 seconds
    curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L); // Connection timeout: 10 seconds

    // 7. For POST/PUT requests, serialize JSON payload and attach to request body
    if (method == "POST" || method == "PUT") {
        payload_string = payload.dump();
        std::cout << "[DEBUG] Sending JSON payload: " << payload_string << std::endl;

        // Use COPYPOSTFIELDS to ensure libcurl copies data internally, avoiding dangling pointer issues
        curl_easy_setopt(curl_handle, CURLOPT_COPYPOSTFIELDS, payload_string.c_str());
    }

    // 8. Execute HTTP request
    CURLcode curl_result = curl_easy_perform(curl_handle);

    // 9. Retrieve HTTP status code from response
    long http_status_code = 0;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_status_code);

    // 10. Free CURL resources (headers must be freed before cleanup)
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl_handle);

    // 11. Check if CURL execution was successful
    if (curl_result != CURLE_OK) {
        throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(curl_result));
    }

    // 12. Parse response as JSON
    json response_json;
    try {
        response_json = json::parse(response_string);
    } catch (const std::exception& parse_error) {
        // If JSON parsing fails, include raw response in error message for debugging
        throw std::runtime_error(std::string("Invalid JSON response: ") + parse_error.what() +
                                 "\nRaw response: " + response_string);
    }

    // 13. Check HTTP status code and handle errors
    if (http_status_code == 401) {
        throw std::runtime_error("401 Unauthorized - Authentication token is invalid or expired");
    }
    if (http_status_code != 200) {
        throw std::runtime_error("HTTP error " + std::to_string(http_status_code) +
                                 " - Response: " + response_string);
    }

    // 14. Return "data" field if present (API convention), otherwise return full JSON response
    return response_json.contains("data") ? response_json["data"] : response_json;
}