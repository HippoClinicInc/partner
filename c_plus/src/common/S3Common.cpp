#include "S3Common.h"

// Global variables
bool g_isInitialized = false;
Aws::SDKOptions g_options;

// HippoClient credentials
String g_apiUrl = "https://dev.hippoclinic.com";
String g_email = "";
String g_password = "";

String create_response(int code, const String& message) {
    std::ostringstream oss;
    oss << "{"
        << "\"code\":" << code << ","
        << "\"message\":\"" << message << "\""
        << "}";
    return oss.str();
}

// Format error message helper function
String formatErrorMessage(const String& baseMessage, const String& detail) {
    if (detail.empty()) {
        return baseMessage;
    }
    return baseMessage + ": " + detail;
}

// Upload ID helper functions
String getUploadId(const String& dataId, long long timestamp) {
    return dataId + UPLOAD_ID_SEPARATOR + std::to_string(timestamp);
}

// Extract uploadDataName from S3 objectKey
// objectKey format: "patient/patientId/source_data/dataId/uploadDataName/" or 
//                   "patient/patientId/source_data/dataId/uploadDataName/filename"
// Returns the uploadDataName extracted from the path
String extractUploadDataName(const String& objectKey) {
    String uploadDataName = "";
    
    // Find the last slash
    size_t lastSlash = objectKey.find_last_of('/');
    if (lastSlash != String::npos) {
        // Get the path without the last segment
        String pathWithoutLastSegment = objectKey.substr(0, lastSlash);
        
        // Find the second-to-last slash
        size_t secondLastSlash = pathWithoutLastSegment.find_last_of('/');
        if (secondLastSlash != String::npos) {
            // Extract uploadDataName (between second-to-last and last slash)
            uploadDataName = pathWithoutLastSegment.substr(secondLastSlash + 1);
        }
    }
    
    return uploadDataName;
}

// AsyncUploadManager::addUpload implementation
String AsyncUploadManager::addUpload(const String& uploadId, const String& localFilePath, const String& s3ObjectKey, const String& patientId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto progress = std::make_shared<AsyncUploadProgress>();
    progress->uploadId = uploadId;
    progress->localFilePath = localFilePath;
    progress->s3ObjectKey = s3ObjectKey;
    progress->patientId = patientId;
    
    // Extract dataId from uploadId (format: "dataId_timestamp")
    size_t separatorPos = uploadId.find(UPLOAD_ID_SEPARATOR);
    if (separatorPos != String::npos) {
        progress->dataId = uploadId.substr(0, separatorPos);
    }
    
    // Extract uploadDataName from s3ObjectKey
    progress->uploadDataName = extractUploadDataName(s3ObjectKey);
    
    progress->status = UPLOAD_PENDING;  // Set to pending initially
    uploads_[uploadId] = progress;
    return uploadId;
}

// Initialize AWS SDK
const char* InitializeAwsSDK() {
    // If SDK is already initialized, return success status
    // This allows multiple calls to SetCredential without errors
    if (g_isInitialized) {
        static std::string response = create_response(SDK_INIT_SUCCESS, "AWS SDK already initialized");
        return response.c_str();
    }

    try {
        // Set log level (can be adjusted as needed)
        g_options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Info;

        // Initialize AWS SDK
        Aws::InitAPI(g_options);
        g_isInitialized = true;

        static std::string response = create_response(SDK_INIT_SUCCESS, "AWS SDK initialized successfully");
        return response.c_str();
    }
    catch (const std::exception& e) {
        static std::string response = create_response(UPLOAD_FAILED, formatErrorMessage("Failed to initialize AWS SDK", e.what()));
        return response.c_str();
    }
    catch (...) {
        static std::string response = create_response(UPLOAD_FAILED, formatErrorMessage("Failed to initialize AWS SDK", ErrorMessage::UNKNOWN_ERROR));
        return response.c_str();
    }
}

// Check if file exists
extern "C" S3UPLOAD_API int __stdcall FileExists(const char* filePath) {
    if (!filePath) return 0;
    
    std::ifstream file(filePath);
    return file.good() ? 1 : 0;
}

// Get file size
extern "C" S3UPLOAD_API long __stdcall GetS3FileSize(const char* filePath) {
    if (!filePath) return -1;
    
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return -1;
    }
    return static_cast<long>(file.tellg());
}

// Set credentials
extern "C" S3UPLOAD_API const char* __stdcall SetCredential(const char* userName, const char* password) {
    // Call InitializeAwsSDK first
    const char* initResult = InitializeAwsSDK();
    
    // Check if SDK initialization was successful
    try {
        // Parse the initialization result to check if it was successful
        String initResultStr = initResult;
        if (initResultStr.find("\"code\":5") == String::npos) {
            // SDK initialization failed, return the error
            return initResult;
        }
        
        // SDK initialized successfully, now set up HippoClient credentials
        g_email = userName;
        g_password = password;
        
        // Initialize HippoClient with credentials
        HippoClient::Init(g_apiUrl, g_email, g_password);
        
        // Log the credential setup
        AWS_LOGSTREAM_INFO("S3Upload", "Credentials set - URL: " << g_apiUrl << ", Email: " << g_email);
        
        // Return success response
        static std::string response = create_response(SDK_INIT_SUCCESS, "AWS SDK initialized and credentials set successfully");
        return response.c_str();
        
    } catch (const std::exception& e) {
        static std::string response = create_response(UPLOAD_FAILED, formatErrorMessage("Failed to set credentials", e.what()));
        return response.c_str();
    } catch (...) {
        static std::string response = create_response(UPLOAD_FAILED, formatErrorMessage("Failed to set credentials", ErrorMessage::UNKNOWN_ERROR));
        return response.c_str();
    }
}

// Backend API confirmation function
bool ConfirmUploadRawFile(const String& dataId, 
                         const String& uploadDataName, const String& patientId, 
                         long long uploadFileSizeBytes, const String& s3ObjectKey) {
    try {
        // Build JSON payload for HippoClient
        nlohmann::json payload;
        payload["dataId"] = dataId;
        payload["dataName"] = uploadDataName;
        payload["fileName"] = s3ObjectKey;
        payload["dataSize"] = uploadFileSizeBytes;
        payload["patientId"] = patientId;
        payload["dataType"] = 20;
        payload["uploadDataName"] = uploadDataName;
        payload["isRawDataInternal"] = 1;
        payload["dataVersions"] = nlohmann::json::array({0});
        
        // Use HippoClient to confirm upload
        nlohmann::json response = HippoClient::ConfirmUploadRawFile(payload);
        
        // Check if confirmation was successful by examining response
        bool hasSuccessUploads = response.contains("successUploads") && 
                                response["successUploads"].is_array() && 
                                response["successUploads"].size() > 0;
        
        bool hasFailedUploads = response.contains("failedUploads") && 
                               response["failedUploads"].is_array() && 
                               response["failedUploads"].size() > 0;
        
        if (hasSuccessUploads && !hasFailedUploads) {
            AWS_LOGSTREAM_INFO("S3Upload", "Upload confirmation successful for dataId: " << dataId 
                              << " - Success uploads: " << response["successUploads"].size());
            
            // Automatically cleanup uploads after successful confirmation
            CleanupUploadsByDataId(dataId);
            
            return true;
        } else if (hasFailedUploads) {
            AWS_LOGSTREAM_ERROR("S3Upload", "Upload confirmation failed for dataId: " << dataId 
                               << " - Failed uploads: " << response["failedUploads"].size());
            return false;
        } else {
            AWS_LOGSTREAM_WARN("S3Upload", "Upload confirmation unclear for dataId: " << dataId 
                              << " - No success or failed uploads found");
            return false;
        }
        
    } catch (const std::exception& e) {
        AWS_LOGSTREAM_ERROR("S3Upload", "Exception in ConfirmUploadRawFile: " << e.what());
        return false;
    } catch (...) {
        AWS_LOGSTREAM_ERROR("S3Upload", "Unknown exception in ConfirmUploadRawFile");
        return false;
    }
}

// S3 client creation helper
Aws::S3::S3Client createS3Client(const String& accessKey, 
                                const String& secretKey, 
                                const String& sessionToken, 
                                const String& region) {
    // Configure client
    AWS_LOGSTREAM_INFO("S3Upload", "Creating S3 client configuration...");
    Aws::S3::S3ClientConfiguration clientConfig;
    clientConfig.region = region;
    // 30 seconds timeout
    clientConfig.requestTimeoutMs = 30000;
    clientConfig.connectTimeoutMs = 10000;
    // 10 seconds connect timeout

    // Create AWS credentials (with Session Token)
    AWS_LOGSTREAM_INFO("S3Upload", "Creating AWS credentials...");
    Aws::Auth::AWSCredentials credentials;
    if (!sessionToken.empty()) {
        // Use temporary credentials (STS)
        AWS_LOGSTREAM_INFO("S3Upload", "Using temporary credentials with session token");
        credentials = Aws::Auth::AWSCredentials(accessKey, secretKey, sessionToken);
    } else {
        // Use permanent credentials
        AWS_LOGSTREAM_INFO("S3Upload", "Using permanent credentials");
        credentials = Aws::Auth::AWSCredentials(accessKey, secretKey);
    }

    // Create credentials provider
    AWS_LOGSTREAM_INFO("S3Upload", "Creating credentials provider...");
    auto credentialsProvider = Aws::MakeShared<Aws::Auth::SimpleAWSCredentialsProvider>("S3Upload", credentials);
    
    // Create S3 client - using credentials provider constructor
    AWS_LOGSTREAM_INFO("S3Upload", "Creating S3 client...");
    return Aws::S3::S3Client(credentialsProvider, nullptr, clientConfig);
}
