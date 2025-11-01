#include "s3_client_manager.h"
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/S3Client.h>
#include <iostream>

using json = nlohmann::json;

RefreshingS3Client::RefreshingS3Client(S3ClientManager* manager, const std::string& patient_id)
    : manager_(manager), patient_id_(patient_id) {}

std::shared_ptr<Aws::S3::S3Client> RefreshingS3Client::get_client() {
    return manager_->get_client(patient_id_);
}

// ---------------- S3ClientManager Implementation ----------------

S3ClientManager::S3ClientManager(const std::string& region, TokenFetcher fetcher,
                                 size_t max_pool_connections, std::time_t refresh_margin)
    : region_(region),
      token_fetcher_(fetcher),
      max_pool_connections_(max_pool_connections),
      refresh_margin_(refresh_margin) {}

bool S3ClientManager::need_refresh(const std::string& patient_id) {
    auto now = std::time(nullptr);
    if (patient_id != current_patient_id_) return true;
    if (!current_client_) return true;
    if (now > current_credential_.expiration - refresh_margin_) return true;
    return false;
}

std::shared_ptr<Aws::S3::S3Client> S3ClientManager::get_client(const std::string& patient_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (need_refresh(patient_id)) {
        return refresh_client(patient_id);
    }
    return current_client_;
}

std::shared_ptr<RefreshingS3Client> S3ClientManager::get_refreshing_client(const std::string& patient_id) {
    return std::make_shared<RefreshingS3Client>(this, patient_id);
}

std::shared_ptr<Aws::S3::S3Client> S3ClientManager::refresh_client(const std::string& patient_id) {
    AWS_LOGSTREAM_INFO("S3ClientManager", "refresh_client patient_id: " << patient_id);
    json cred_json = token_fetcher_(patient_id);
    AWS_LOGSTREAM_INFO("S3ClientManager", "refresh_client cred_json: " << cred_json);
    S3Credential cred = S3Credential::from_json(cred_json);

    // Configure client with timeout settings and disable IMDS
    // Use ClientConfiguration instead of S3ClientConfiguration to avoid linker symbol conflicts
    AWS_LOGSTREAM_INFO("S3ClientManager", "Creating S3 client configuration...");
    Aws::Client::ClientConfiguration clientConfig;
    clientConfig.region = region_;
    // 30 seconds request timeout, 10 seconds connect timeout
    clientConfig.requestTimeoutMs = 30000;
    clientConfig.connectTimeoutMs = 10000;
    // Disable EC2 metadata service to avoid timeout errors
    clientConfig.disableIMDS = true;

    // Create AWS credentials (with Session Token) - reuse logic from createS3Client
    AWS_LOGSTREAM_INFO("S3ClientManager", "Creating AWS credentials...");
    Aws::Auth::AWSCredentials credentials;
    if (!cred.sessionToken.empty()) {
        // Use temporary credentials (STS)
        AWS_LOGSTREAM_INFO("S3ClientManager", "Using temporary credentials with session token");
        credentials = Aws::Auth::AWSCredentials(cred.accessKeyId, cred.secretAccessKey, cred.sessionToken);
    } else {
        // Use permanent credentials
        AWS_LOGSTREAM_INFO("S3ClientManager", "Using permanent credentials");
        credentials = Aws::Auth::AWSCredentials(cred.accessKeyId, cred.secretAccessKey);
    }

    // Create credentials provider
    AWS_LOGSTREAM_INFO("S3ClientManager", "Creating credentials provider...");
    auto credentialsProvider = Aws::MakeShared<Aws::Auth::SimpleAWSCredentialsProvider>(
        "S3ClientManager", credentials);

    // Create S3 client with shared_ptr
    AWS_LOGSTREAM_INFO("S3ClientManager", "Creating S3 client...");
    auto client = std::make_shared<Aws::S3::S3Client>(
        credentialsProvider, clientConfig,
        Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, true);

    current_patient_id_ = patient_id;
    current_client_ = client;
    current_credential_ = cred;

    std::cout << "[S3ClientManager] Refreshed client for patient_id: " << patient_id << std::endl;
    AWS_LOGSTREAM_INFO("S3ClientManager", "Refreshed client for patient_id: " << patient_id);

    return client;
}



