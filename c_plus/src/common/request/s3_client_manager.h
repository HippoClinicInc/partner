#pragma once

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <nlohmann/json.hpp>

// Struct to hold temporary S3 credentials obtained from an external service.
struct S3Credential {
    std::string accessKeyId;
    std::string secretAccessKey;
    std::string sessionToken;
    std::time_t expiration;  // Expiration time in epoch seconds (UTC)

    // Factory method to create S3Credential from a JSON object.
    static S3Credential from_json(const nlohmann::json& json_obj) {
        S3Credential credential;
        auto credential_json = json_obj.at("amazonTemporaryCredentials");
        credential.accessKeyId = credential_json.at("accessKeyId").get<std::string>();
        credential.secretAccessKey = credential_json.at("secretAccessKey").get<std::string>();
        credential.sessionToken = credential_json.at("sessionToken").get<std::string>();
        credential.expiration = std::stoll(credential_json.at("expirationTimestampSecondsInUTC").get<std::string>());
        return credential;
    }
};

// Forward declaration
class S3ClientManager;

// Proxy wrapper that automatically fetches a refreshed client when needed.
class RefreshingS3Client {
public:
    RefreshingS3Client(S3ClientManager* manager, const std::string& patient_id);

    // Returns an S3 client that is automatically refreshed if credentials expire.
    std::shared_ptr<Aws::S3::S3Client> get_client();

private:
    S3ClientManager* manager_;   // Reference to manager that handles client creation
    std::string patient_id_;     // Patient ID used to fetch the right credentials
};

// Manages cached AWS S3 clients per patient, auto-refreshing when credentials expire.
class S3ClientManager {
public:
    using TokenFetcher = std::function<nlohmann::json(const std::string& patient_id)>;

    S3ClientManager(const std::string& region, TokenFetcher fetcher,
                    size_t max_pool_connections = 4,
                    std::time_t refresh_margin = 600,
                    size_t max_cache_size = 1000);

    // Get a raw S3 client (may trigger refresh if credentials expired)
    std::shared_ptr<Aws::S3::S3Client> get_client(const std::string& patient_id);

    // Get a proxy client that auto-refreshes
    std::shared_ptr<RefreshingS3Client> get_refreshing_client(const std::string& patient_id);

private:
    // Internal structure holding both client and credential data
    struct ClientEntry {
        std::shared_ptr<Aws::S3::S3Client> client;
        S3Credential credential;
    };

    // Determine whether credentials for this patient need to be refreshed
    bool need_refresh(const std::string& patient_id);

    // Refresh credentials and rebuild client
    std::shared_ptr<Aws::S3::S3Client> refresh_client(const std::string& patient_id);

    // Clean up expired or excess cached clients
    void cleanup_cache();

    // AWS region to use for S3 clients
    std::string region_;

    // Function to retrieve credentials for a given patient ID
    TokenFetcher token_fetcher_;

    // AWS SDK configuration parameters
    size_t max_pool_connections_;
    std::time_t refresh_margin_;  // Margin before expiration to trigger refresh
    size_t max_cache_size_;       // Maximum allowed cached clients

    // Thread safety for shared map
    std::mutex mutex_;

    // Cache of patient_id -> (S3Client + Credential)
    std::unordered_map<std::string, ClientEntry> patient_clients_;
};
