#include "../common/S3Common.h"
#include "../common/request/s3_client_manager.h"
#include <queue>

// Global queue processing control - ensures only one upload runs at a time
static std::mutex g_uploadMutex;
static std::condition_variable g_uploadCondition;
static std::atomic<bool> g_isUploading(false);
static std::atomic<int> g_activeUploads(0);
static const int MAX_CONCURRENT_UPLOADS = 1;  // Only allow one upload at a time

// Global upload task queue structure
struct UploadTask {
    String uploadId;
    String region;
    String bucketName;
    String objectKey;
    String localFilePath;
    String dataId;
    String patientId;
};

// Global upload queue and worker thread management
static std::queue<UploadTask> g_uploadQueue;
static std::mutex g_queueMutex;
static std::condition_variable g_queueCondition;
static std::atomic<bool> g_workerRunning(false);
static std::atomic<bool> g_shouldShutdown(false);
static std::thread g_workerThread;
static std::mutex g_workerThreadMutex;
static std::chrono::steady_clock::time_point g_lastHeartbeat;
static std::mutex g_heartbeatMutex;

// Upload processing function
// This function handles the actual file upload to S3, called by the worker thread
void asyncUploadWorker(const String& uploadId,
                      const String& region,
                      const String& bucketName,
                      const String& objectKey,
                      const String& localFilePath,
                      const String& dataId,
                      const String& patientId) {

    // Step 1: Get upload progress tracker from manager
    auto& manager = AsyncUploadManager::getInstance();
    auto progress = manager.getUpload(uploadId);
    if (!progress) return;

    try {
        // Step 2: Initialize upload progress and set status to uploading
        progress->startTime = std::chrono::steady_clock::now();
        manager.updateProgress(uploadId, UPLOAD_UPLOADING);

        AWS_LOGSTREAM_INFO("S3Upload", "=== Starting Async Upload ===");
        AWS_LOGSTREAM_INFO("S3Upload", "Upload ID: " << uploadId);
        AWS_LOGSTREAM_INFO("S3Upload", "Data ID: " << dataId);
        AWS_LOGSTREAM_INFO("S3Upload", "File: " << localFilePath);

        // Step 3: Check for cancellation before starting
        if (progress->shouldCancel.load()) {
            manager.updateProgress(uploadId, UPLOAD_CANCELLED);
            return;
        }

        // Step 4: Validate input parameters
        if (region.empty() || bucketName.empty() || objectKey.empty() || 
            localFilePath.empty() || patientId.empty()) {
            manager.updateProgress(uploadId, UPLOAD_FAILED, "Invalid parameters");
            return;
        }

        // Step 5: Verify AWS SDK is initialized
        if (!g_isInitialized) {
            manager.updateProgress(uploadId, UPLOAD_FAILED, "AWS SDK not initialized");
            return;
        }

        // Step 6: Check if local file exists
        if (!FileExists(localFilePath.c_str())) {
            manager.updateProgress(uploadId, UPLOAD_FAILED, "Local file does not exist");
            return;
        }

        // Step 7: Get file size and validate
        long fileSize = GetS3FileSize(localFilePath.c_str());
        if (fileSize < 0) {
            manager.updateProgress(uploadId, UPLOAD_FAILED, "Cannot read file size");
            return;
        }

        progress->totalSize = fileSize;
        AWS_LOGSTREAM_INFO("S3Upload", "File size: " << fileSize << " bytes");

        // Step 8: Check for cancellation again before heavy operations
        if (progress->shouldCancel.load()) {
            manager.updateProgress(uploadId, UPLOAD_CANCELLED);
            return;
        }

        // Step 9: Create S3 client using S3ClientManager
        // Define a lambda function to fetch S3 credentials for the patient
        auto credentials_fetcher = [](const std::string& patient_id) -> nlohmann::json {
            auto response = HippoClient::GetS3Credentials(patient_id);
            AWS_LOGSTREAM_INFO("S3Upload", "get_s3_credentials: " << response);
            return response;
        };

        // Initialize S3ClientManager with the specified region and credentials fetcher
        AWS_LOGSTREAM_INFO("S3Upload", "Creating S3ClientManager for region: " << region << ", patientId: " << patientId);
        auto s3_client_manager = std::make_shared<S3ClientManager>(region, credentials_fetcher);
        
        // Get a refreshing client proxy that automatically handles credential refresh
        auto s3_client_proxy = s3_client_manager->get_refreshing_client(patientId);
        
        // Verify that the client proxy was created successfully
        if (!s3_client_proxy) {
            manager.updateProgress(uploadId, UPLOAD_FAILED, "Failed to create S3 client proxy");
            AWS_LOGSTREAM_ERROR("S3Upload", "Failed to create S3 client proxy for patientId: " << patientId);
            return;
        }
        
        AWS_LOGSTREAM_INFO("S3Upload", "S3 client proxy created successfully");

        // Step 10: Create S3 PutObject request
        AWS_LOGSTREAM_INFO("S3Upload", "Creating PutObject request - Bucket: " << bucketName << ", Key: " << objectKey);
        Aws::S3::Model::PutObjectRequest request;
        request.SetBucket(bucketName);
        request.SetKey(objectKey);

        // Step 11: Final cancellation check before upload
        if (progress->shouldCancel.load()) {
            manager.updateProgress(uploadId, UPLOAD_CANCELLED);
            return;
        }

        // Step 12: Open file stream for reading
        AWS_LOGSTREAM_INFO("S3Upload", "Opening file for reading: " << localFilePath);
        auto inputData = Aws::MakeShared<Aws::FStream>("PutObjectInputStream",
                                                       localFilePath.c_str(),
                                                       std::ios_base::in | std::ios_base::binary);

        if (!inputData->is_open()) {
            std::string errorMsg = "Cannot open file for reading: " + localFilePath;
            manager.updateProgress(uploadId, UPLOAD_FAILED, errorMsg);
            AWS_LOGSTREAM_ERROR("S3Upload", errorMsg);
            return;
        }
        
        // Get file size from stream for logging (already got fileSize earlier, but verify consistency)
        inputData->seekg(0, std::ios::end);
        auto streamPos = inputData->tellg();
        inputData->seekg(0, std::ios::beg);
        long long streamFileSize = static_cast<long long>(streamPos);
        AWS_LOGSTREAM_INFO("S3Upload", "File opened successfully, size: " << streamFileSize << " bytes");

        // Step 13: Set request body and content type
        request.SetBody(inputData);
        request.SetContentType("application/octet-stream");

        AWS_LOGSTREAM_INFO("S3Upload", "Starting S3 PutObject operation - Bucket: " << bucketName 
                          << ", Key: " << objectKey << ", Size: " << streamFileSize << " bytes");

        // Step 14: Execute S3 upload with retry mechanism (up to 3 retries on failure)
        bool uploadSuccess = false;
        std::string finalErrorMsg = "";
        
        // Retry loop: attempt upload up to MAX_UPLOAD_RETRIES + 1 times (initial + 3 retries)
        for (int retryCount = 0; retryCount <= MAX_UPLOAD_RETRIES; retryCount++) {
            // Check for cancellation before each retry attempt
            if (progress->shouldCancel.load()) {
                manager.updateProgress(uploadId, UPLOAD_CANCELLED);
                return;
            }
            
            // Apply exponential backoff delay for retry attempts (2, 4, 6 seconds)
            if (retryCount > 0) {
                AWS_LOGSTREAM_INFO("S3Upload", "Retry attempt " << retryCount << " for upload ID: " << uploadId);
                std::this_thread::sleep_for(std::chrono::seconds(retryCount * 2));
            }
            
            // Execute the actual S3 upload operation
            AWS_LOGSTREAM_INFO("S3Upload", "Executing PutObject (attempt " << (retryCount + 1) << "/" << (MAX_UPLOAD_RETRIES + 1) << ") for upload ID: " << uploadId);
            auto outcome = s3_client_proxy->with_auto_refresh([&](std::shared_ptr<Aws::S3::S3Client> client) {
                return client->PutObject(request);
            });
            
            if (outcome.IsSuccess()) {
                // Upload succeeded - exit retry loop
                uploadSuccess = true;
                AWS_LOGSTREAM_INFO("S3Upload", "Async upload SUCCESS for ID: " << uploadId << " (attempt " << (retryCount + 1) << ")");
                
                // Log ETag if available
                if (outcome.GetResult().GetETag().size() > 0) {
                    AWS_LOGSTREAM_INFO("S3Upload", "Upload ETag: " << outcome.GetResult().GetETag());
                }
                break;
            } else {
                // Upload failed - log detailed error information
                auto error = outcome.GetError();
                std::string errorType = error.GetExceptionName();
                std::string errorMessage = error.GetMessage();
                int httpResponseCode = static_cast<int>(error.GetResponseCode());
                
                finalErrorMsg = "S3 upload failed (attempt " + std::to_string(retryCount + 1) + "): " + errorMessage;
                
                AWS_LOGSTREAM_ERROR("S3Upload", "Upload attempt " << (retryCount + 1) << " failed for ID: " << uploadId);
                AWS_LOGSTREAM_ERROR("S3Upload", "  - Error Type: " << errorType);
                AWS_LOGSTREAM_ERROR("S3Upload", "  - Error Message: " << errorMessage);
                AWS_LOGSTREAM_ERROR("S3Upload", "  - HTTP Response Code: " << httpResponseCode);
                
                // Log request ID if available
                if (error.GetRequestId().size() > 0) {
                    AWS_LOGSTREAM_ERROR("S3Upload", "  - Request ID: " << error.GetRequestId());
                }
                
                
                // If this is the last attempt, exit retry loop
                if (retryCount == MAX_UPLOAD_RETRIES) {
                    AWS_LOGSTREAM_ERROR("S3Upload", "All retry attempts exhausted for upload ID: " << uploadId);
                    break;
                }
            }
        }

        AWS_LOGSTREAM_INFO("S3Upload", "PutObject operation completed");

        // Step 15: Handle final upload result
        if (uploadSuccess) {
            progress->endTime = std::chrono::steady_clock::now();
            manager.updateProgress(uploadId, UPLOAD_SUCCESS);
            AWS_LOGSTREAM_INFO("S3Upload", "Async upload SUCCESS for ID: " << uploadId);
        } else {
            manager.updateProgress(uploadId, UPLOAD_FAILED, finalErrorMsg);
            AWS_LOGSTREAM_ERROR("S3Upload", "Async upload FAILED for ID: " << uploadId << " after " << (MAX_UPLOAD_RETRIES + 1) << " attempts - " << finalErrorMsg);
        }
        
        // Step 16: Handle confirmation AFTER upload completes
        // This allows the next file to start uploading while this file is being confirmed
        if (uploadSuccess) {
            AWS_LOGSTREAM_INFO("S3Upload", "Upload success, checking fileOperationType for ID: " << uploadId 
                              << ", fileOperationType: " << progress->fileOperationType 
                              << " (REAL_TIME_APPEND=" << REAL_TIME_APPEND
                              << ", BATCH_CREATE=" << BATCH_CREATE << ")");
            
            // 17.0: If REAL_TIME_APPEND, confirm immediately for this file
            if (progress->fileOperationType == REAL_TIME_APPEND) {
                // For incremental upload, use the actual file name instead of the folder name
                String actualFileName = extractFileName(progress->s3ObjectKey);
                bool incrementalConfirmSucceeded = ConfirmIncrementalUploadFile(
                    progress->dataId,
                    actualFileName,  // Use actual file name for dataName and uploadDataName
                    progress->patientId,
                    progress->totalSize,
                    progress->s3ObjectKey
                );
                
                AWS_LOGSTREAM_INFO("S3Upload", "ConfirmIncrementalUploadFile returned for ID: " << uploadId << ", success: " << incrementalConfirmSucceeded);
                
                if (incrementalConfirmSucceeded) {
                    manager.updateProgress(uploadId, CONFIRM_SUCCESS);
                    AWS_LOGSTREAM_INFO("S3Upload", "Confirmation SUCCESS for ID: " << uploadId);
                    
                } else {
                    manager.updateProgress(uploadId, CONFIRM_FAILED);
                    AWS_LOGSTREAM_WARN("S3Upload", "Confirmation FAILED for ID: " << uploadId);
                }
            }
            
            // Step 15.1: Check if this is the last file in a folder upload
            auto allUploads = manager.getAllUploadsByDataId(progress->dataId);
            bool allFilesCompleted = true;
            long long totalFolderSize = 0;
            
            for (auto& upload : allUploads) {
                if (upload && upload->status != UPLOAD_SUCCESS && upload->status != CONFIRM_SUCCESS) {
                    allFilesCompleted = false;
                    break;
                }
                if (upload) {
                    totalFolderSize += upload->totalSize;
                }
            }
            
            // Step 15.2: Attempt confirmation if BATCH_CREATE and this is the last file or single file
            if (progress->fileOperationType == BATCH_CREATE && allFilesCompleted && !progress->confirmationAttempted && !progress->dataId.empty()) {
                progress->confirmationAttempted = true;
                AWS_LOGSTREAM_INFO("S3Upload", "All files completed, attempting confirmation for dataId: " << progress->dataId);
                
                // Determine if this is a folder upload and extract parent directory path if needed
                String confirmObjectKey = progress->s3ObjectKey;
                bool isFolderUpload = allUploads.size() > 1;
                
                if (isFolderUpload) {
                    // For folder uploads, extract parent directory path (remove filename, keep directory with trailing slash)
                    size_t lastSlash = progress->s3ObjectKey.find_last_of('/');
                    if (lastSlash != String::npos) {
                        confirmObjectKey = progress->s3ObjectKey.substr(0, lastSlash + 1);
                        AWS_LOGSTREAM_INFO("S3Upload", "Folder upload detected - using parent directory: " << confirmObjectKey);
                    }
                }
                
                bool confirmSuccess = ConfirmUploadRawFile(
                    progress->dataId,
                    progress->uploadDataName,
                    progress->patientId,
                    totalFolderSize, // Use total folder size for confirmation
                    confirmObjectKey
                );
                
                if (confirmSuccess) {
                    // Update all uploads to CONFIRM_SUCCESS
                    for (auto& upload : allUploads) {
                        if (upload && upload->status == UPLOAD_SUCCESS) {
                            manager.updateProgress(upload->uploadId, CONFIRM_SUCCESS);
                        }
                    }
                    AWS_LOGSTREAM_INFO("S3Upload", "Backend confirmation SUCCESS for dataId: " << progress->dataId);
                    
                } else {
                    // Update all uploads to CONFIRM_FAILED
                    for (auto& upload : allUploads) {
                        if (upload && upload->status == UPLOAD_SUCCESS) {
                            manager.updateProgress(upload->uploadId, CONFIRM_FAILED);
                        }
                    }
                    AWS_LOGSTREAM_WARN("S3Upload", "Backend confirmation FAILED for dataId: " << progress->dataId << " (uploads still successful)");
                }
            }
        }

    } catch (const std::exception& e) {
        // Step 17: Handle exceptions during upload
        std::string errorMsg = "Upload failed with exception: " + std::string(e.what());
        manager.updateProgress(uploadId, UPLOAD_FAILED, errorMsg);
        AWS_LOGSTREAM_ERROR("S3Upload", "Exception in async upload: " << e.what());
    } catch (...) {
        // Step 18: Handle unknown exceptions
        manager.updateProgress(uploadId, UPLOAD_FAILED, "Unknown error");
        AWS_LOGSTREAM_ERROR("S3Upload", "Unknown exception in async upload");
    }
}

// Worker thread function - continuously processes upload tasks from the queue
void uploadWorkerThread() {
    AWS_LOGSTREAM_INFO("S3Upload", "Upload worker thread started");
    
    while (!g_shouldShutdown.load()) {
        try {
            // Update heartbeat
            {
                std::lock_guard<std::mutex> lock(g_heartbeatMutex);
                g_lastHeartbeat = std::chrono::steady_clock::now();
            }
            
            // Wait for and get next task from queue
            UploadTask task;
            {
                std::unique_lock<std::mutex> lock(g_queueMutex);
                
                // Wait with timeout (5 seconds) to check shutdown flag periodically
                bool hasTask = g_queueCondition.wait_for(lock, std::chrono::seconds(5), [] {
                    return !g_uploadQueue.empty() || g_shouldShutdown.load();
                });
                
                if (g_shouldShutdown.load() && g_uploadQueue.empty()) {
                    break;
                }
                
                if (!hasTask || g_uploadQueue.empty()) {
                    continue; // Timeout, check heartbeat and continue
                }
                
                // Get task from queue
                task = g_uploadQueue.front();
                g_uploadQueue.pop();
                
                AWS_LOGSTREAM_INFO("S3Upload", "Worker thread picked up task: " << task.uploadId 
                                  << ", queue size: " << g_uploadQueue.size());
            }
            
            // Process the upload task
            asyncUploadWorker(task.uploadId, task.region, task.bucketName, 
                            task.objectKey, task.localFilePath, 
                            task.dataId, task.patientId);
            
        } catch (const std::exception& e) {
            AWS_LOGSTREAM_ERROR("S3Upload", "Exception in worker thread: " << e.what());
            // Continue running even if one task fails
        } catch (...) {
            AWS_LOGSTREAM_ERROR("S3Upload", "Unknown exception in worker thread");
            // Continue running even if one task fails
        }
    }
    
    g_workerRunning = false;
    AWS_LOGSTREAM_INFO("S3Upload", "Upload worker thread stopped");
}

// Start or restart the worker thread
void ensureWorkerThreadRunning() {
    std::lock_guard<std::mutex> lock(g_workerThreadMutex);
    
    // Check if thread needs to be started or restarted
    bool needStart = false;
    
    if (!g_workerRunning.load()) {
        needStart = true;
        AWS_LOGSTREAM_WARN("S3Upload", "Worker thread not running, will start/restart");
    } else {
        // Check if thread is alive by checking heartbeat
        std::lock_guard<std::mutex> hbLock(g_heartbeatMutex);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastHeartbeat).count();
        
        if (elapsed > 30) { // If no heartbeat for 30 seconds, consider thread dead
            AWS_LOGSTREAM_ERROR("S3Upload", "Worker thread heartbeat timeout (" << elapsed 
                               << " seconds), will restart");
            needStart = true;
            g_workerRunning = false;
            
            // Try to join old thread if joinable
            if (g_workerThread.joinable()) {
                g_workerThread.detach(); // Detach dead thread
            }
        }
    }
    
    if (needStart) {
        g_shouldShutdown = false;
        g_workerRunning = true;
        
        // Initialize heartbeat
        {
            std::lock_guard<std::mutex> hbLock(g_heartbeatMutex);
            g_lastHeartbeat = std::chrono::steady_clock::now();
        }
        
        // Create new worker thread
        if (g_workerThread.joinable()) {
            g_workerThread.detach();
        }
        
        g_workerThread = std::thread(uploadWorkerThread);
        AWS_LOGSTREAM_INFO("S3Upload", "Worker thread started successfully");
    }
}

// Exported async upload function - adds upload task to global queue
// A single persistent worker thread processes all upload tasks sequentially
// Returns JSON with upload ID on success, error message on failure
extern "C" S3UPLOAD_API const char* __stdcall UploadFileAsync(
    const char* region,
    const char* bucketName,
    const char* objectKey,
    const char* localFilePath,
    const char* dataId,
    const char* patientId,
    int fileOperationType
) {
    static std::string response;

    // Step 1: Validate input parameters
    if (!region || !bucketName || !objectKey || !localFilePath || !dataId || !patientId) {
        response = create_response(UPLOAD_FAILED, formatErrorMessage(ErrorMessage::INVALID_PARAMETERS));
        return response.c_str();
    }

    // Step 2: Check if AWS SDK is initialized
    if (!g_isInitialized) {
        response = create_response(UPLOAD_FAILED, formatErrorMessage(ErrorMessage::SDK_NOT_INITIALIZED));
        return response.c_str();
    }

    // Step 2.1: Check upload queue limit (max 100 uploads)
    auto& manager = AsyncUploadManager::getInstance();
    size_t totalUploads = manager.getTotalUploads();
    
    if (totalUploads >= MAX_UPLOAD_LIMIT) {
        // Check if there are existing uploads with the same dataId
        auto existingUploads = manager.getAllUploadsByDataId(dataId);
        
        if (existingUploads.empty()) {
            // No existing uploads with same dataId, reject new upload
            std::string errorMsg = "Upload queue is full (" + std::to_string(totalUploads) + 
                                 " uploads). Please wait for some uploads to complete before trying again.";
            response = create_response(UPLOAD_FAILED, formatErrorMessage("Upload limit exceeded", errorMsg));
            AWS_LOGSTREAM_WARN("S3Upload", "Upload rejected due to queue limit: " << errorMsg);
            return response.c_str();
        } else {
            // Allow upload to continue if same dataId exists (folder upload scenario)
            AWS_LOGSTREAM_INFO("S3Upload", "Upload queue full but allowing continuation for existing dataId: " << dataId);
        }
    }

    try {
        // Step 3: Generate unique upload ID using dataId and current timestamp
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        String uploadId = getUploadId(dataId, timestamp);

        // Step 4: Register upload with manager for progress tracking and queue
        // uploadDataName and dataId will be automatically extracted inside addUpload
        manager.addUpload(uploadId, localFilePath, objectKey, patientId);

        // Save operation type to progress
        if (auto uploadProgress = manager.getUpload(uploadId)) {
            uploadProgress->fileOperationType = (fileOperationType == REAL_TIME_APPEND) ? REAL_TIME_APPEND: BATCH_CREATE;
            AWS_LOGSTREAM_INFO("S3Upload", "Setting fileOperationType for uploadId: " << uploadId 
                              << ", input fileOperationType: " << fileOperationType 
                              << ", set to: " << uploadProgress->fileOperationType
                              << " (REAL_TIME_APPEND=" << REAL_TIME_APPEND
                              << ", BATCH_CREATE=" << BATCH_CREATE << ")");
        } else {
            AWS_LOGSTREAM_ERROR("S3Upload", "Failed to get upload progress for uploadId: " << uploadId);
        }

        // Step 5: Ensure worker thread is running (start if not running or restart if dead)
        ensureWorkerThreadRunning();

        // Step 6: Convert C-style parameters to C++ strings (avoid pointer lifetime issues)
        String strRegion = region;
        String strBucketName = bucketName;
        String strObjectKey = objectKey;
        String strLocalFilePath = localFilePath;
        String strDataId = dataId;
        String strPatientId = patientId;

        // Step 7: Add task to upload queue
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            UploadTask task;
            task.uploadId = uploadId;
            task.region = strRegion;
            task.bucketName = strBucketName;
            task.objectKey = strObjectKey;
            task.localFilePath = strLocalFilePath;
            task.dataId = strDataId;
            task.patientId = strPatientId;
            
            g_uploadQueue.push(task);
            AWS_LOGSTREAM_INFO("S3Upload", "Task added to queue: " << uploadId 
                              << ", queue size: " << g_uploadQueue.size());
        }
        
        // Notify worker thread that new task is available
        g_queueCondition.notify_one();

        // Step 8: Return success response with upload ID
        response = create_response(UPLOAD_SUCCESS, uploadId);
        return response.c_str();

    } catch (const std::exception& e) {
        // Step 9: Handle exceptions during task queue addition
        response = create_response(UPLOAD_FAILED, formatErrorMessage("Failed to enqueue upload task", e.what()));
        return response.c_str();
    } catch (...) {
        // Step 10: Handle unknown exceptions
        response = create_response(UPLOAD_FAILED, formatErrorMessage("Failed to enqueue upload task", ErrorMessage::UNKNOWN_ERROR));
        return response.c_str();
    }
}

// Shutdown the upload worker thread gracefully
// Should be called when the application is shutting down
extern "C" S3UPLOAD_API void __stdcall ShutdownUploadWorker() {
    AWS_LOGSTREAM_INFO("S3Upload", "Shutting down upload worker thread...");
    
    // Signal worker thread to stop
    g_shouldShutdown = true;
    
    // Wake up worker thread if it's waiting
    g_queueCondition.notify_all();
    
    // Wait for worker thread to finish (with timeout)
    std::lock_guard<std::mutex> lock(g_workerThreadMutex);
    if (g_workerThread.joinable()) {
        // Use detach for now to avoid blocking too long
        // In a production environment, you might want to implement a timed join
        g_workerThread.detach();
    }
    
    g_workerRunning = false;
    AWS_LOGSTREAM_INFO("S3Upload", "Upload worker thread shutdown complete");
}

// Get the current upload queue size
// Returns the number of pending upload tasks in the queue
extern "C" S3UPLOAD_API int __stdcall GetUploadQueueSize() {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    return static_cast<int>(g_uploadQueue.size());
}

// Get async upload status as byte array - safer for VB6 interop
// Returns the size of data copied to buffer, 0 on error
extern "C" S3UPLOAD_API int __stdcall GetAsyncUploadStatusBytes(
    const char* dataId, 
    unsigned char* buffer, 
    int bufferSize
) {
    // Step 1: Validate parameters
    if (!dataId || !buffer || bufferSize <= 0) {
        return 0;
    }

    // Step 2: Look up all uploads that match the dataId prefix
    auto& manager = AsyncUploadManager::getInstance();
    auto allUploads = manager.getAllUploadsByDataId(dataId);
    if (allUploads.empty()) {
        // Return error JSON if no uploads found
        std::string errorJson = create_response(UPLOAD_FAILED, formatErrorMessage("No uploads found with dataId"));
        int dataSize = static_cast<int>(errorJson.size());
        if (dataSize > bufferSize) dataSize = bufferSize;
        memcpy(buffer, errorJson.c_str(), dataSize);
        return dataSize;
    }

    try {
        // Step 3: Check status of all uploads with this dataId
        bool allCompleted = true;
        bool anyFailed = false;
        bool anyUploading = false;
        std::string errorMessage = "";
        long long totalSize = 0;
        int uploadedCount = 0;
        long long uploadedSize = 0;
        
        for (auto& progress : allUploads) {
            totalSize += progress->totalSize;
            
            if (progress->status == UPLOAD_SUCCESS) {
                uploadedCount++;
                uploadedSize += progress->totalSize;
            } else if (progress->status == UPLOAD_FAILED) {
                anyFailed = true;
                if (errorMessage.empty()) {
                    errorMessage = progress->errorMessage;
                }
                allCompleted = false;
            } else if (progress->status == UPLOAD_UPLOADING || progress->status == UPLOAD_PENDING || progress->status == UPLOAD_CANCELLED) {
                anyUploading = true;
                allCompleted = false;
            }
        }
        
        // Step 4: Determine overall status and handle folder confirmation
        int overallStatus;
        if (anyFailed) {
            overallStatus = UPLOAD_FAILED;
        } else if (allCompleted && !anyUploading) {
            // Check confirmation status
            bool allConfirmed = true;
            bool anyConfirmFailed = false;
            
            for (auto& progress : allUploads) {
                if (progress->status == CONFIRM_FAILED) {
                    anyConfirmFailed = true;
                    allConfirmed = false;
                } else if (progress->status != CONFIRM_SUCCESS) {
                    allConfirmed = false;
                }
            }
            
            if (allConfirmed) {
                overallStatus = CONFIRM_SUCCESS;
            } else if (anyConfirmFailed) {
                overallStatus = CONFIRM_FAILED;
            } else {
                overallStatus = UPLOAD_SUCCESS; // Upload completed, confirmation may be in progress
            }
        } else {
            overallStatus = UPLOAD_UPLOADING;
        }

        // Step 5: Build JSON response with array of upload information and summary
        int totalUploadCount = static_cast<int>(allUploads.size());
        
        std::ostringstream oss;
        oss << "{"
            << "\"code\":" << UPLOAD_SUCCESS << ","
            << "\"status\":" << overallStatus << ","
            << "\"uploadedCount\":" << uploadedCount << ","
            << "\"uploadedSize\":" << uploadedSize << ","
            << "\"totalSize\":" << totalSize << ","
            << "\"totalUploadCount\":" << totalUploadCount << ","
            << "\"errorMessage\":\"" << errorMessage << "\","
            << "\"dataId\":\"" << dataId << "\","
            << "\"uploads\":[";

        // Add array of individual upload information
        for (size_t i = 0; i < allUploads.size(); ++i) {
            auto& progress = allUploads[i];
            if (i > 0) oss << ",";
            
            // Convert time points to milliseconds since epoch
            auto startTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                progress->startTime.time_since_epoch()).count();
            
            long long endTimeMs = 0;
            if (progress->endTime.time_since_epoch().count() > 0) {
                endTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    progress->endTime.time_since_epoch()).count();
            }
            
            oss << "{"
                << "\"uploadId\":\"" << progress->uploadId << "\","
                << "\"localFilePath\":\"" << progress->localFilePath << "\","
                << "\"s3ObjectKey\":\"" << progress->s3ObjectKey << "\","
                << "\"status\":" << progress->status << ","
                << "\"totalSize\":" << progress->totalSize << ","
                << "\"errorMessage\":\"" << progress->errorMessage << "\","
                << "\"startTime\":" << startTimeMs << ","
                << "\"endTime\":" << endTimeMs
                << "}";
        }

        oss << "]}";

        std::string response = oss.str();
        
        // Step 5: Copy data to buffer (truncate if necessary)
        int dataSize = static_cast<int>(response.size());
        if (dataSize > bufferSize) {
            dataSize = bufferSize;
        }
        
        memcpy(buffer, response.c_str(), dataSize);
        return dataSize;
        
    } catch (const std::exception& e) {
        // Step 6: Handle exceptions during status query
        std::string errorJson = create_response(UPLOAD_FAILED, formatErrorMessage("Failed to get upload status", e.what()));
        int dataSize = static_cast<int>(errorJson.size());
        if (dataSize > bufferSize) dataSize = bufferSize;
        memcpy(buffer, errorJson.c_str(), dataSize);
        return dataSize;
    } catch (...) {
        // Step 7: Handle unknown exceptions
        std::string errorJson = create_response(UPLOAD_FAILED, formatErrorMessage("Failed to get upload status", ErrorMessage::UNKNOWN_ERROR));
        int dataSize = static_cast<int>(errorJson.size());
        if (dataSize > bufferSize) dataSize = bufferSize;
        memcpy(buffer, errorJson.c_str(), dataSize);
        return dataSize;
    }
}
