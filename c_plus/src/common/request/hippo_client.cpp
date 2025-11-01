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

json HippoClient::GenerateUniqueDataId(int quantity) {
    if (quantity <= 0) throw std::invalid_argument("quantity must be > 0");

    std::string url = base_url_ + "/hippo/thirdParty/file/generateUniqueKey/" + std::to_string(quantity);
    return RequestWithToken("GET", url);
}

json HippoClient::GetS3Credentials(const std::string& patientId) {
    std::string url = base_url_ + "/hippo/thirdParty/file/getS3Credentials";
    json payload = {
        {"keyId", patientId},
        // Indicates obtaining credentials to access a patient folder.
        {"resourceType", 2}
    };

    json resp = RequestWithToken("POST", url, payload);
    std::cout << "[get_s3_credentials] response:\n" << resp << std::endl;
    return resp;
}

// Private methods
void HippoClient::Login() {
    std::string url = base_url_ + "/hippo/thirdParty/user/login";
    json payload = {
        {"userMessage", {{"email", account_}}},
        {"password", password_}
    };

    json resp = HttpRequest("POST", url, payload);

    if (!resp.contains("jwtToken")) {
        throw std::runtime_error("Login failed: missing jwtToken in response");
    }

    jwt_token_ = resp["jwtToken"];
    hospital_id_ = resp["userInfo"]["hospitalId"].get<std::string>();

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
            Login(); // original login() method
            return true; // login success
        } catch (const std::exception& e) {
            std::cerr << "[HippoClient] Login attempt " << (attempt + 1)
                      << " failed: " << e.what() << std::endl;
            attempt++;
            int sleep_time = 1 << attempt; // exponential backoff
            std::this_thread::sleep_for(std::chrono::seconds(sleep_time));
        }
    }
    return false; // login completely failed
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
            std::cout << "[HippoClient] Requesting token: " << token << std::endl;
            json response = HttpRequest(method, url, payload, token);
            return response;
        } catch (const std::exception& e) {
            std::cout << "[HippoClient] Request failed: " << e.what() << std::endl;
            std::string msg = e.what();
            std::cerr << "[HippoClient] Request failed (attempt " << (attempt + 1)
                      << ") for URL=" << url << ": " << msg << std::endl;

            if (msg.find("401") != std::string::npos) {
                std::cerr << "[HippoClient] Token expired, re-login..." << std::endl;
                jwt_token_.clear();
                if (!LoginWithRetries()) {
                    throw std::runtime_error("Login failed after retries, cannot refresh token");
                }
                continue;
            }

            attempt++;
            if (attempt >= maxRetries) throw;
            int sleep_time = 1 << attempt;
            std::cerr << "[HippoClient] Retrying after " << sleep_time << "s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(sleep_time));
        }
    }
    throw std::runtime_error("Request failed after retries for URL=" + url);
}

// libcurl callback function: used to receive HTTP response body content
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), total_size);
    return total_size;
}

/**
 * @brief Unified HTTP request function (supports GET/POST/PUT/DELETE)
 *
 * @param method  HTTP method, e.g., "GET", "POST", "PUT", "DELETE"
 * @param url     Request URL
 * @param payload JSON payload (only valid for POST/PUT)
 * @param token   Token (optional)
 * @return json   Parsed JSON data (if contains "data", returns data field)
 */
json HippoClient::HttpRequest(const std::string& method,
                              const std::string& url,
                              const json& payload,
                              const std::string& token) {
    // 1. Initialize CURL handle
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("Failed to init curl");

    std::string response_string;         // stores server response
    struct curl_slist* headers = nullptr; // HTTP header list
    std::string payload_str;             // pre-declare payload string to ensure scope

    // 2. Construct HTTP Header
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
    headers = curl_slist_append(headers, "Accept: application/json");

    // 3. Add Authorization header if token exists
    if (!token.empty()) {
        headers = curl_slist_append(headers, ("Authorization: " + token).c_str());
    }

    // 4. Basic configuration: URL, request method, callback function, headers, etc.
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());   // supports GET/POST/PUT/DELETE
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);             // set HTTP headers
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);    // response callback
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);     // write response to string

    // 5. Security settings: enable HTTPS certificate verification (must enable in production)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L); // verify SSL certificate
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L); // verify hostname matches certificate

    // 6. Timeout settings (prevent long blocking)
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);        // total timeout 30s
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L); // connection timeout 10s

    // 7. If POST or PUT, serialize payload and attach to request body
    if (method == "POST" || method == "PUT") {
        payload_str = payload.dump();
        std::cout << "[DEBUG] Sending JSON: " << payload_str << std::endl;

        // Use COPYPOSTFIELDS to ensure libcurl copies data internally, avoiding dangling pointer
        curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, payload_str.c_str());
    }

    // 8. Execute request
    CURLcode res = curl_easy_perform(curl);

    // 9. Get HTTP status code
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    // 10. Free resources (first free headers, then cleanup)
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // 11. Check if CURL executed successfully
    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("CURL failed: ") + curl_easy_strerror(res));
    }

    // 12. Parse response JSON
    json result_json;
    try {
        result_json = json::parse(response_string);
    } catch (const std::exception& e) {
        // If not valid JSON, throw detailed error (include raw response)
        throw std::runtime_error(std::string("Invalid JSON response: ") + e.what() +
                                 "\nRaw response: " + response_string);
    }

    // 13. Check HTTP status code for errors
    if (response_code == 401) {
        throw std::runtime_error("401 Unauthorized");
    }
    if (response_code != 200) {
        throw std::runtime_error("HTTP error: " + std::to_string(response_code) +
                                 " - " + response_string);
    }

    // 14. If JSON contains "data" field, return data, otherwise return full JSON
    return result_json.contains("data") ? result_json["data"] : result_json;
}
