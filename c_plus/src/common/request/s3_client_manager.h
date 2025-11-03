#pragma once

#include <aws/s3/S3Client.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>
#include <ctime>
#include <string>
#include <functional>

/**
 * Function type for fetching AWS S3 credentials token.
 * Takes a patient ID and returns a JSON object containing temporary credentials.
 */
using TokenFetcher = std::function<nlohmann::json(const std::string&)>;

/**
 * Structure representing AWS S3 credentials including access key, secret key,
 * session token, and expiration timestamp.
 */
struct S3Credential {
    std::string accessKeyId;      ///< AWS access key ID
    std::string secretAccessKey;  ///< AWS secret access key
    std::string sessionToken;     ///< AWS session token (for temporary credentials)
    std::time_t expiration;       ///< Credential expiration timestamp in seconds

    /**
     * Creates an S3Credential object from a JSON response.
     * @param credential_json JSON object containing AWS temporary credentials
     * @return S3Credential object parsed from the JSON
     * @throws std::exception if required JSON fields are missing
     */
    static S3Credential from_json(const nlohmann::json& credential_json) {
        const auto& temporary_credentials = credential_json.at("amazonTemporaryCredentials");

        S3Credential credential;
        credential.accessKeyId     = temporary_credentials.at("accessKeyId").get<std::string>();
        credential.secretAccessKey = temporary_credentials.at("secretAccessKey").get<std::string>();
        credential.sessionToken    = temporary_credentials.at("sessionToken").get<std::string>();
        credential.expiration      = std::stoll(temporary_credentials.at("expirationTimestampSecondsInUTC").get<std::string>());
        
        return credential;
    }
};


// Forward declaration
class S3ClientManager;

/**
 * Wrapper class that provides automatic credential refresh for S3 clients.
 * This class maintains a reference to the S3ClientManager and a patient ID,
 * allowing clients to get refreshed S3 clients without managing patient IDs themselves.
 */
class RefreshingS3Client {
public:
    /**
     * Constructs a RefreshingS3Client with a manager and patient ID.
     * @param manager Pointer to the S3ClientManager that handles credential refresh
     * @param patient_id Patient ID used to fetch credentials
     */
    RefreshingS3Client(S3ClientManager* manager, const std::string& patient_id);
    
    /**
     * Gets the S3 client, automatically refreshing credentials if needed.
     * @return Shared pointer to the S3 client
     */
    std::shared_ptr<Aws::S3::S3Client> get_client();

private:
    S3ClientManager* manager_;    ///< Pointer to the S3ClientManager
    std::string patient_id_;      ///< Patient ID for credential fetching
};

/**
 * Manages AWS S3 clients with automatic credential refresh.
 * This class handles credential expiration and patient ID changes,
 * automatically refreshing S3 clients when necessary.
 * Thread-safe implementation using mutex for concurrent access.
 */
class S3ClientManager {
public:
    /**
     * Constructs an S3ClientManager with the specified configuration.
     * @param region AWS region for S3 operations
     * @param fetcher Function to fetch AWS credentials token
     * @param max_pool_connections Maximum number of pool connections (currently unused, reserved for future use)
     * @param refresh_margin Time margin in seconds before expiration to trigger refresh
     */
    S3ClientManager(const std::string& region, TokenFetcher fetcher,
                    size_t max_pool_connections = 25,
                    std::time_t refresh_margin = 300);

    /**
     * Gets an S3 client for the specified patient ID.
     * Automatically refreshes credentials if needed (patient ID changed, client missing, or credentials expiring soon).
     * @param patient_id Patient ID to get credentials for
     * @return Shared pointer to the S3 client
     */
    std::shared_ptr<Aws::S3::S3Client> get_client(const std::string& patient_id);
    
    /**
     * Creates a RefreshingS3Client wrapper for the specified patient ID.
     * This wrapper can be passed to other components and will automatically use the correct patient ID.
     * @param patient_id Patient ID for the refreshing client
     * @return Shared pointer to a RefreshingS3Client instance
     */
    std::shared_ptr<RefreshingS3Client> get_refreshing_client(const std::string& patient_id);

private:
    /**
     * Checks if the S3 client needs to be refreshed.
     * @param patient_id Patient ID to check
     * @return true if refresh is needed (patient ID changed, no client, or credentials expiring soon)
     */
    bool need_refresh(const std::string& patient_id);
    
    /**
     * Refreshes the S3 client by fetching new credentials and creating a new client.
     * This method should only be called from get_client() which holds the mutex lock.
     * @param patient_id Patient ID to fetch credentials for
     * @return Shared pointer to the newly created S3 client
     */
    std::shared_ptr<Aws::S3::S3Client> refresh_client(const std::string& patient_id);

    std::string region_;                  ///< AWS region for S3 operations
    TokenFetcher token_fetcher_;          ///< Function to fetch AWS credentials token
    size_t max_pool_connections_;        ///< Maximum number of pool connections (reserved for future use)
    std::time_t refresh_margin_;          ///< Time margin in seconds before expiration to trigger refresh

    std::string current_patient_id_;                      ///< Currently cached patient ID
    std::shared_ptr<Aws::S3::S3Client> current_client_;  ///< Currently cached S3 client
    S3Credential current_credential_;                     ///< Currently cached credentials

    std::mutex mutex_;  ///< Mutex for thread-safe access
};
