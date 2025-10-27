#include "s3_client_manager.h"
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/client/ClientConfiguration.h>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

RefreshingS3Client::RefreshingS3Client(S3ClientManager* manager, const std::string& patient_id)
    : manager_(manager), patient_id_(patient_id) {}

std::shared_ptr<Aws::S3::S3Client> RefreshingS3Client::get_client() {
    auto client = manager_->get_client(patient_id_);
    return client;
}

// ---------------- S3ClientManager Implementation ----------------

S3ClientManager::S3ClientManager(const std::string& region, TokenFetcher fetcher,
                                 size_t max_pool_connections,
                                 std::time_t refresh_margin,
                                 size_t max_cache_size)
    : region_(region),
      token_fetcher_(fetcher),
      max_pool_connections_(max_pool_connections),
      refresh_margin_(refresh_margin),
      max_cache_size_(max_cache_size) {}

bool S3ClientManager::need_refresh(const std::string& patient_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = patient_clients_.find(patient_id);
    if (it == patient_clients_.end()) return true;
    auto now = std::time(nullptr);
    return now > it->second.credential.expiration - refresh_margin_;
}

std::shared_ptr<Aws::S3::S3Client> S3ClientManager::get_client(const std::string& patient_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (need_refresh(patient_id)) {
        return refresh_client(patient_id);
    }
    return patient_clients_[patient_id].client;
}

std::shared_ptr<RefreshingS3Client> S3ClientManager::get_refreshing_client(const std::string& patient_id) {
    return std::make_shared<RefreshingS3Client>(this, patient_id);
}

std::shared_ptr<Aws::S3::S3Client> S3ClientManager::refresh_client(const std::string& patient_id) {
    json cred_json = token_fetcher_(patient_id);
    S3Credential cred = S3Credential::from_json(cred_json);

    Aws::Auth::AWSCredentials awsCred(
    Aws::String(cred.accessKeyId.c_str()),
    Aws::String(cred.secretAccessKey.c_str()),
    Aws::String(cred.sessionToken.c_str())
);
    Aws::Client::ClientConfiguration config;
    config.region = region_;
    config.maxConnections = static_cast<int>(max_pool_connections_);

    auto client = std::make_shared<Aws::S3::S3Client>(awsCred, config);

    cleanup_cache();

    patient_clients_[patient_id] = ClientEntry{client, cred};
    std::cout << "Refreshed S3 client for patient_id: " << patient_id << std::endl;

    return client;
}

void S3ClientManager::cleanup_cache() {
    auto now = std::time(nullptr);
    // Remove expired entries
    for (auto it = patient_clients_.begin(); it != patient_clients_.end();) {
        if (it->second.credential.expiration <= now) {
            it = patient_clients_.erase(it);
        } else {
            ++it;
        }
    }

    // Remove oldest if cache too big
    if (patient_clients_.size() > max_cache_size_) {
        std::vector<std::pair<std::string, ClientEntry>> items(patient_clients_.begin(), patient_clients_.end());
        std::sort(items.begin(), items.end(),
                  [](const auto& a, const auto& b) {
                      return a.second.credential.expiration < b.second.credential.expiration;
                  });
        size_t to_remove = patient_clients_.size() - max_cache_size_;
        for (size_t i = 0; i < to_remove; ++i) {
            patient_clients_.erase(items[i].first);
        }
    }
}
