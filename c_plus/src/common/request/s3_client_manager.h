#pragma once

#include <aws/s3/S3Client.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>
#include <ctime>
#include <string>
#include <functional>
#include <stdexcept>
#include <limits>

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
        try {
            const auto& temporary_credentials = credential_json.at("amazonTemporaryCredentials");

            S3Credential credential;
            credential.accessKeyId     = temporary_credentials.at("accessKeyId").get<std::string>();
            credential.secretAccessKey = temporary_credentials.at("secretAccessKey").get<std::string>();
            credential.sessionToken    = temporary_credentials.at("sessionToken").get<std::string>();
            
            // Safe conversion with overflow/underflow checking
            const std::string expiration_str = temporary_credentials.at("expirationTimestampSecondsInUTC").get<std::string>();
            const long long expiration_ll = std::stoll(expiration_str);
            
            // Check for overflow/underflow: time_t may be 32-bit or 64-bit
            // Ensure value is within reasonable time_t range
            if (expiration_ll < 0 || 
                expiration_ll > static_cast<long long>((std::numeric_limits<std::time_t>::max)())) {
                throw std::out_of_range("Expiration timestamp out of range: " + expiration_str);
            }
            credential.expiration = static_cast<std::time_t>(expiration_ll);

            return credential;
        } catch (const nlohmann::json::exception& e) {
            throw std::runtime_error("JSON parsing error: " + std::string(e.what()));
        } catch (const std::invalid_argument& e) {
            throw std::runtime_error("Invalid expiration timestamp format: " + std::string(e.what()));
        } catch (const std::out_of_range& e) {
            throw; // Re-throw out_of_range from our own checks
        }
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
     * @param manager Shared pointer to the S3ClientManager that handles credential refresh
     * @param patient_id Patient ID used to fetch credentials
     */
    RefreshingS3Client(std::shared_ptr<S3ClientManager> manager, const std::string& patient_id);

    /**
     * Gets the S3 client, automatically refreshing credentials if needed.
     * @return Shared pointer to the S3 client
     * @throws std::runtime_error if manager is null
     */
    std::shared_ptr<Aws::S3::S3Client> get_client();

    /**
     * Execute an S3 operation with auto refresh and one retry on expired credentials.
     * The callable should accept a shared_ptr<Aws::S3::S3Client> and return an AWS Outcome type
     * that provides IsSuccess() and GetError().
     * @tparam Func Callable type
     * @param func  Callable receiving shared_ptr<S3Client> and returning Outcome
     * @return Outcome returned by the callable (possibly from the retry)
     */
    template <typename Func>
    auto with_auto_refresh(Func&& func)
        -> decltype(std::forward<Func>(func)(std::declval<std::shared_ptr<Aws::S3::S3Client>>()));

private:
    std::weak_ptr<S3ClientManager> manager_;    ///< Weak pointer to the S3ClientManager to avoid circular reference
    std::string patient_id_;                    ///< Patient ID for credential fetching
};

/**
 * Manages AWS S3 clients with automatic credential refresh.
 * This class handles credential expiration and patient ID changes,
 * automatically refreshing S3 clients when necessary.
 * Thread-safe implementation using mutex for concurrent access.
 * 
 * NOTE: This class must be managed by std::shared_ptr to use get_refreshing_client().
 * For stack-allocated instances, use get_client() directly instead.
 */
class S3ClientManager : public std::enable_shared_from_this<S3ClientManager> {
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

    /**
     * Force refresh the S3 client for a patient id regardless of current cached state.
     * Thread-safe.
     */
    std::shared_ptr<Aws::S3::S3Client> force_refresh(const std::string& patient_id);

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

// ---- Template implementation ----
// Retry limit for expired-credential retries used by with_auto_refresh()
static const int kMaxExpiredRetries = 3;
template <typename Func>
auto RefreshingS3Client::with_auto_refresh(Func&& func)
    -> decltype(std::forward<Func>(func)(std::declval<std::shared_ptr<Aws::S3::S3Client>>())) {
    auto manager = manager_.lock();
    if (!manager) {
        throw std::runtime_error("S3ClientManager has been destroyed");
    }

    // Retry for expired-credential errors; limit total retries by static count
    int attempt = 0;
    std::shared_ptr<Aws::S3::S3Client> client;

    while (true) {
        if (attempt == 0) {
            client = manager->get_client(patient_id_);
        }
        auto outcome = std::forward<Func>(func)(client);

        if (outcome.IsSuccess()) {
            return outcome;
        }

        const auto& error = outcome.GetError();
        const std::string exception_name = error.GetExceptionName().c_str();
        const std::string message = error.GetMessage().c_str();
        const bool is_expired =
            (exception_name.find("ExpiredToken") != std::string::npos) ||
            (exception_name.find("RequestExpired") != std::string::npos) ||
            (message.find("ExpiredToken") != std::string::npos) ||
            (message.find("RequestExpired") != std::string::npos);

        if (!is_expired || attempt >= kMaxExpiredRetries) {
            return outcome;
        }

        AWS_LOGSTREAM_INFO("RefreshingS3Client", "Detected expired credentials, refreshing and retrying (attempt " << (attempt + 1) << "/" << kMaxExpiredRetries << ") for patient_id=" << patient_id_);
        client = manager->force_refresh(patient_id_);
        ++attempt;
    }
}