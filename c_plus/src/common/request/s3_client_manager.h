#pragma once

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <nlohmann/json.hpp>

// Struct to hold temporary S3 credentials
struct S3Credential {
    std::string accessKeyId;
    std::string secretAccessKey;
    std::string sessionToken;
    std::time_t expiration;  // epoch seconds

    static S3Credential from_json(const nlohmann::json& j) {
        S3Credential cred;
        auto tmp = j.at("amazonTemporaryCredentials");
        cred.accessKeyId = tmp.at("accessKeyId").get<std::string>();
        cred.secretAccessKey = tmp.at("secretAccessKey").get<std::string>();
        cred.sessionToken = tmp.at("sessionToken").get<std::string>();
        cred.expiration = std::stoll(tmp.at("expirationTimestampSecondsInUTC").get<std::string>());
        return cred;
    }
};

// Forward declaration
class S3ClientManager;

// Proxy client that auto-refreshes expired credentials
class RefreshingS3Client {
public:
    RefreshingS3Client(S3ClientManager* manager, const std::string& patient_id);

    std::shared_ptr<Aws::S3::S3Client> get_client();

private:
    S3ClientManager* manager_;
    std::string patient_id_;
};

// Manager class to handle cached S3 clients per patient
class S3ClientManager {
public:
    using TokenFetcher = std::function<nlohmann::json(const std::string& patient_id)>;

    S3ClientManager(const std::string& region, TokenFetcher fetcher,
                    size_t max_pool_connections = 4,
                    std::time_t refresh_margin = 600,
                    size_t max_cache_size = 1000);

    // Return raw S3 client
    std::shared_ptr<Aws::S3::S3Client> get_client(const std::string& patient_id);

    // Return refreshing proxy client
    std::shared_ptr<RefreshingS3Client> get_refreshing_client(const std::string& patient_id);

private:
    struct ClientEntry {
        std::shared_ptr<Aws::S3::S3Client> client;
        S3Credential credential;
    };

    bool need_refresh(const std::string& patient_id);
    std::shared_ptr<Aws::S3::S3Client> refresh_client(const std::string& patient_id);
    void cleanup_cache();

    std::string region_;
    TokenFetcher token_fetcher_;
    size_t max_pool_connections_;
    std::time_t refresh_margin_;
    size_t max_cache_size_;

    std::mutex mutex_;
    std::unordered_map<std::string, ClientEntry> patient_clients_;
};
