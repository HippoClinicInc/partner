#include "s3_client_manager.h"
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/client/ClientConfiguration.h>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

// RefreshingS3Client Implementation
RefreshingS3Client::RefreshingS3Client(S3ClientManager* manager, const std::string& patient_id)
    : manager_(manager), patient_id_(patient_id) {}

std::shared_ptr<Aws::S3::S3Client> RefreshingS3Client::get_client() {
    // Always retrieve the current valid client from the manager
    auto s3_client = manager_->get_client(patient_id_);
    return s3_client;
}

// S3ClientManager Implementation
S3ClientManager::S3ClientManager(const std::string& region, TokenFetcher fetcher,
                                 size_t max_pool_connections,
                                 std::time_t refresh_margin,
                                 size_t max_cache_size)
    : region_(region),
      token_fetcher_(fetcher),
      max_pool_connections_(max_pool_connections),
      refresh_margin_(refresh_margin),
      max_cache_size_(max_cache_size) {}


// Check if credentials for a specific patient need to be refreshed.
// Returns true if credentials are missing or near expiration.
bool S3ClientManager::need_refresh(const std::string& patient_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = patient_clients_.find(patient_id);
    if (it == patient_clients_.end()) return true;

    auto current_time = std::time(nullptr);
    return current_time > it->second.credential.expiration - refresh_margin_;
}


// Get an existing client for a patient, refreshing if needed.
bool S3ClientManager::need_refresh(const std::string& patient_id) {
    auto it = patient_clients_.find(patient_id);
    if (it == patient_clients_.end()) return true;

    auto current_time = std::time(nullptr);
    return current_time > it->second.credential.expiration - refresh_margin_;
}



// Create a proxy wrapper that automatically refreshes its credentials.
std::shared_ptr<RefreshingS3Client> S3ClientManager::get_refreshing_client(const std::string& patient_id) {
    return std::make_shared<RefreshingS3Client>(this, patient_id);
}


// Fetch new temporary credentials and construct a new S3 client.
std::shared_ptr<Aws::S3::S3Client> S3ClientManager::refresh_client(const std::string& patient_id) {
    // 1. Fetch new temporary credentials (NO LOCK)
    // Fetching tokens may involve network calls, so we avoid holding mutex here
    nlohmann::json credential_json = token_fetcher_(patient_id);
    S3Credential new_credential = S3Credential::from_json(credential_json);

    // 2. Create AWS credential and client configuration
    Aws::Auth::AWSCredentials aws_credentials(
        Aws::String(new_credential.accessKeyId.c_str()),
        Aws::String(new_credential.secretAccessKey.c_str()),
        Aws::String(new_credential.sessionToken.c_str())
    );

    Aws::Client::ClientConfiguration client_config;
    client_config.region = region_;
    client_config.maxConnections = static_cast<int>(max_pool_connections_);

    // Create a new S3 client using these credentials
    auto new_s3_client = std::make_shared<Aws::S3::S3Client>(aws_credentials, client_config);


    // 3. Update cache (LOCK NEEDED)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Remove expired entries or shrink cache if needed
        cleanup_cache();

        // Update or insert new client entry
        patient_clients_[patient_id] = ClientEntry{new_s3_client, new_credential};
    }


    // 4. Log refresh event
    std::cout << "Refreshed S3 client for patient_id: " << patient_id << std::endl;

    return new_s3_client;
}


// Remove expired clients or shrink cache if over limit.
void S3ClientManager::cleanup_cache() {
    auto current_time = std::time(nullptr);

    // Remove expired entries
    for (auto it = patient_clients_.begin(); it != patient_clients_.end();) {
        if (it->second.credential.expiration <= current_time) {
            it = patient_clients_.erase(it);
        } else {
            ++it;
        }
    }

    // Remove oldest entries if cache exceeds maximum size
    if (patient_clients_.size() > max_cache_size_) {
        std::vector<std::pair<std::string, ClientEntry>> items(patient_clients_.begin(), patient_clients_.end());

        // Sort by credential expiration time (oldest first)
        std::sort(items.begin(), items.end(),
                  [](const auto& a, const auto& b) {
                      return a.second.credential.expiration < b.second.credential.expiration;
                  });

        size_t items_to_remove = patient_clients_.size() - max_cache_size_;
        for (size_t i = 0; i < items_to_remove; ++i) {
            patient_clients_.erase(items[i].first);
        }
    }
}
