#pragma once

#include <aws/s3/S3Client.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>
#include <ctime>
#include <string>
#include <functional>

using TokenFetcher = std::function<nlohmann::json(const std::string&)>;

struct S3Credential {
    std::string accessKeyId;
    std::string secretAccessKey;
    std::string sessionToken;
    std::time_t expiration;

    static S3Credential from_json(const nlohmann::json& j) {
        const auto& creds = j.at("amazonTemporaryCredentials");

        S3Credential c;
        c.accessKeyId     = creds.at("accessKeyId").get<std::string>();
        c.secretAccessKey = creds.at("secretAccessKey").get<std::string>();
        c.sessionToken    = creds.at("sessionToken").get<std::string>();

        c.expiration = std::stoll(creds.at("expirationTimestampSecondsInUTC").get<std::string>());
        return c;
    }
};


class S3ClientManager;

class RefreshingS3Client {
public:
    RefreshingS3Client(S3ClientManager* manager, const std::string& patient_id);
    std::shared_ptr<Aws::S3::S3Client> get_client();

private:
    S3ClientManager* manager_;
    std::string patient_id_;
};

class S3ClientManager {
public:
    S3ClientManager(const std::string& region, TokenFetcher fetcher,
                    size_t max_pool_connections = 25,
                    std::time_t refresh_margin = 300);

    std::shared_ptr<Aws::S3::S3Client> get_client(const std::string& patient_id);
    std::shared_ptr<RefreshingS3Client> get_refreshing_client(const std::string& patient_id);

private:
    bool need_refresh(const std::string& patient_id);
    std::shared_ptr<Aws::S3::S3Client> refresh_client(const std::string& patient_id);

    std::string region_;
    TokenFetcher token_fetcher_;
    size_t max_pool_connections_;
    std::time_t refresh_margin_;

    std::string current_patient_id_;
    std::shared_ptr<Aws::S3::S3Client> current_client_;
    S3Credential current_credential_;

    std::mutex mutex_;
};
