#include "s3_client_manager.h"
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/S3Client.h>
#include <iostream>

using json = nlohmann::json;

RefreshingS3Client::RefreshingS3Client(std::shared_ptr<S3ClientManager> manager, const std::string& patient_id)
    : manager_(manager), patient_id_(patient_id) {}

std::shared_ptr<Aws::S3::S3Client> RefreshingS3Client::get_client() {
    auto manager = manager_.lock();
    if (!manager) {
        throw std::runtime_error("S3ClientManager has been destroyed");
    }
    return manager->get_client(patient_id_);
}

// ---------------- S3ClientManager Implementation ----------------

S3ClientManager::S3ClientManager(const std::string& region, TokenFetcher fetcher,
                                 size_t max_pool_connections, std::time_t refresh_margin)
    : region_(region),
      token_fetcher_(fetcher),
      max_pool_connections_(max_pool_connections),
      refresh_margin_(refresh_margin) {}

bool S3ClientManager::need_refresh(const std::string& patient_id) {
    const auto current_time = std::time(nullptr);

    // Refresh if patient ID changed
    if (patient_id != current_patient_id_) {
        return true;
    }

    // Refresh if no client exists
    if (!current_client_) {
        return true;
    }

    // Refresh if credentials are expiring soon (within refresh_margin_ seconds)
    // Use safe subtraction to avoid integer underflow/overflow
    if (refresh_margin_ >= current_credential_.expiration) {
        // If margin is larger than expiration, definitely need refresh
        return true;
    }
    if (current_time > current_credential_.expiration - refresh_margin_) {
        return true;
    }

    return false;
}

std::shared_ptr<Aws::S3::S3Client> S3ClientManager::get_client(const std::string& patient_id) {
    // Thread-safe access using mutex lock
    std::lock_guard<std::mutex> lock(mutex_);

    if (need_refresh(patient_id)) {
        return refresh_client(patient_id);
    }

    return current_client_;
}

std::shared_ptr<RefreshingS3Client> S3ClientManager::get_refreshing_client(const std::string& patient_id) {
    // Use shared_from_this to safely create a shared_ptr to this instance
    // This requires that S3ClientManager is managed by std::shared_ptr
    try {
        return std::make_shared<RefreshingS3Client>(shared_from_this(), patient_id);
    } catch (const std::bad_weak_ptr&) {
        throw std::runtime_error(
            "S3ClientManager must be managed by std::shared_ptr to use get_refreshing_client(). "
            "Use get_client() directly for stack-allocated instances.");
    }
}

std::shared_ptr<Aws::S3::S3Client> S3ClientManager::refresh_client(const std::string& patient_id) {
    AWS_LOGSTREAM_INFO("S3ClientManager", "Refreshing client for patient_id: " << patient_id);

    // Fetch credentials from token fetcher
    json credential_json;
    try {
        credential_json = token_fetcher_(patient_id);
    } catch (const std::exception& e) {
        AWS_LOGSTREAM_ERROR("S3ClientManager", "Failed to fetch credentials for patient_id: " 
                           << patient_id << ", error: " << e.what());
        throw; // Re-throw to let caller handle the error
    }
    // Log credential fetch success without exposing sensitive data
    AWS_LOGSTREAM_INFO("S3ClientManager", "Successfully fetched credentials JSON (size: " 
                       << credential_json.dump().length() << " bytes)");

    // Parse credentials from JSON
    S3Credential credential;
    try {
        credential = S3Credential::from_json(credential_json);
    } catch (const std::exception& e) {
        AWS_LOGSTREAM_ERROR("S3ClientManager", "Failed to parse credentials JSON for patient_id: " 
                           << patient_id << ", error: " << e.what());
        throw; // Re-throw to let caller handle the error
    }

    // Configure S3 client with timeout settings and disable IMDS
    // Using ClientConfiguration instead of S3ClientConfiguration to avoid linker symbol conflicts
    AWS_LOGSTREAM_INFO("S3ClientManager", "Creating S3 client configuration...");
    Aws::Client::ClientConfiguration client_config;
    client_config.region = region_;
    // Set timeouts: 30 seconds for request timeout, 10 seconds for connect timeout
    client_config.requestTimeoutMs = 30000;
    client_config.connectTimeoutMs = 10000;
    // Disable EC2 Instance Metadata Service (IMDS) to avoid timeout errors
    client_config.disableIMDS = true;

    // Create AWS credentials with optional session token
    AWS_LOGSTREAM_INFO("S3ClientManager", "Creating AWS credentials...");
    Aws::Auth::AWSCredentials aws_credentials;
    if (!credential.sessionToken.empty()) {
        // Use temporary credentials (from AWS STS - Security Token Service)
        AWS_LOGSTREAM_INFO("S3ClientManager", "Using temporary credentials with session token");
        aws_credentials = Aws::Auth::AWSCredentials(
            credential.accessKeyId,
            credential.secretAccessKey,
            credential.sessionToken);
    } else {
        // Use permanent credentials (access key and secret key only)
        AWS_LOGSTREAM_INFO("S3ClientManager", "Using permanent credentials");
        aws_credentials = Aws::Auth::AWSCredentials(
            credential.accessKeyId,
            credential.secretAccessKey);
    }

    // Create credentials provider wrapper
    AWS_LOGSTREAM_INFO("S3ClientManager", "Creating credentials provider...");
    auto credentials_provider = Aws::MakeShared<Aws::Auth::SimpleAWSCredentialsProvider>(
        "S3ClientManager", aws_credentials);

    // Create S3 client with shared_ptr ownership
    // PayloadSigningPolicy::Never means we don't sign the request payload (suitable for streaming uploads)
    AWS_LOGSTREAM_INFO("S3ClientManager", "Creating S3 client...");
    auto s3_client = std::make_shared<Aws::S3::S3Client>(
        credentials_provider,
        client_config,
        Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
        true);

    // Update cached values
    current_patient_id_ = patient_id;
    current_client_ = s3_client;
    current_credential_ = credential;

    AWS_LOGSTREAM_INFO("S3ClientManager", "Successfully refreshed client for patient_id: " << patient_id);

    return s3_client;
}

std::shared_ptr<Aws::S3::S3Client> S3ClientManager::force_refresh(const std::string& patient_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return refresh_client(patient_id);
}


