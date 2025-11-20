using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace HippoClinic.S3Upload
{
/// <summary>
/// Shared enums and configuration - used by both batch and real-time flows
///
/// NOTE: Keep UploadStatus in sync with C++ enum UploadStatus in `c_plus/src/common/S3Common.h`.
/// If you change values or add/remove states on one side, you MUST update the other side.
/// This enum is used for both the top-level "code" field and the detailed "status" field
/// returned from the C++ DLL APIs.
/// </summary>
public enum UploadStatus
{
    UPLOAD_PENDING = 0,
    UPLOAD_UPLOADING = 1,
    UPLOAD_SUCCESS = 2,
    UPLOAD_FAILED = 3,
    UPLOAD_CANCELLED = 4,
    SDK_INIT_SUCCESS = 5,
    SDK_CLEAN_SUCCESS = 6,
    CONFIRM_SUCCESS = 7,
    CONFIRM_FAILED = 8
}

/// <summary>
/// Keep in sync with C++ enum FileOperationType
/// </summary>
public enum FileOperationType
{
    BATCH_CREATE = 0,
    REAL_TIME_APPEND = 1
}

    /// <summary>
    /// HippoClinic configuration constants
    /// </summary>
    public static class Common
    {
        public const string HippoBaseUrl = "https://hippoclinic.com";

        /// <summary>
        /// S3 configuration constants
        /// For prod running, do not change it here. In our current prod settings,
        /// the prod bucket is called "hippoclinic-staging". We will change it later.
        /// </summary>
        public const string S3Bucket = "hippoclinic-staging";
        public const string S3Region = "us-west-1";

        /// <summary>
        /// Polling interval in milliseconds for upload status monitoring
        /// </summary>
        private const int UploadStatusPollingIntervalMilliseconds = 10000; // 10 seconds

    /// <summary>
    /// Upload all files in a folder to S3 and confirm with API (BatchCreate mode only)
    /// </summary>
    public static bool UploadFolderContents(string folderPath, out long totalFileSize, string s3FileKey, string dataId,
                                            string patientId, out string uploadIds)
    {
        totalFileSize = 0;
        uploadIds = string.Empty;

        if (!Directory.Exists(folderPath))
        {
            Console.WriteLine($"ERROR: Folder does not exist: {folderPath}");
            return false;
        }

        var files = Directory.GetFiles(folderPath);
        if (files.Length == 0)
        {
            Console.WriteLine("ERROR: No files found in folder");
            return false;
        }

        int uploadedCount = 0;
        int failedCount = 0;
        var uploadIdList = new List<string>();

        // Upload all files in the folder (using BatchCreate mode)
        foreach (var filePath in files)
        {
            var fileInfo = new FileInfo(filePath);
            var singleS3FileKey = s3FileKey + Path.GetFileName(filePath);

            if (UploadSingleFile(filePath, singleS3FileKey, dataId, patientId, FileOperationType.BATCH_CREATE,
                                 out string currentUploadId))
            {
                uploadedCount++;
                totalFileSize += fileInfo.Length;
                uploadIdList.Add(currentUploadId);
            }
            else
            {
                failedCount++;
                Console.WriteLine($"ERROR: Failed to start upload for {filePath}");
            }
        }

        if (failedCount > 0)
        {
            Console.WriteLine($"ERROR: {failedCount} files failed to upload");
            return false;
        }

        uploadIds = string.Join(",", uploadIdList);
        return true;
    }

    /// <summary>
    /// Upload a single file to S3 using AWS SDK (uses fileOperationType)
    /// </summary>
    public static bool UploadSingleFile(string filePath, string s3FileKey, string dataId, string patientId,
                                        FileOperationType fileOperationType, out string uploadId)
    {
        uploadId = string.Empty;

        try
        {
            if (!File.Exists(filePath))
            {
                Console.WriteLine($"ERROR: Local file does not exist: {filePath}");
                return false;
            }

            var fileSize = new FileInfo(filePath).Length;

            Console.WriteLine($"Submit to queue - {filePath}");
            var startResponsePtr = S3UploadLib.UploadFileAsync(S3Region, S3Bucket, s3FileKey, filePath, dataId,
                                                               patientId, (long)fileOperationType);
            var startResponse = S3UploadLib.PtrToString(startResponsePtr);
            Console.WriteLine(startResponse);

            using (JsonDocument jsonDocument = JsonDocument.Parse(startResponse))
            {
                var root = jsonDocument.RootElement;
                if (root.TryGetProperty("code", out var codeElement))
                {
                    var startCode = codeElement.GetInt64();
                    if (startCode != (long)UploadStatus.UPLOAD_SUCCESS)
                    {
                        var message = root.TryGetProperty("message", out var msgElement) ? msgElement.GetString()
                                                                                         : "Unknown error";
                        Console.WriteLine($"ERROR: Failed to start async upload - {message}");
                        return false;
                    }

                    uploadId = root.TryGetProperty("message", out var uploadIdElement)
                                   ? (uploadIdElement.GetString() ?? string.Empty)
                                   : string.Empty;
                    Console.WriteLine($"SUCCESS: Upload started for file - {filePath} with upload ID: {uploadId}");
                    return true;
                }
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"ERROR: Upload failed - {ex.Message}");
            return false;
        }

        return false;
    }

    /// <summary>
    /// Monitor upload status for a single dataId
    /// </summary>
    public static bool MonitorUploadStatus(string dataId, long maxWaitTime)
    {
        bool isCompleted = false;
        bool isError = false;
        long waitTime = 0;
        const int bufferSize = 2097152; // 2MB buffer
        byte[] buffer = new byte[bufferSize];

        while (waitTime < maxWaitTime)
        {
            Console.WriteLine($"Query status for dataId: {dataId} (attempt {waitTime + 1})");

            try
            {
                int bytesReceived = S3UploadLib.GetAsyncUploadStatusBytes(dataId, buffer, bufferSize);

                if (bytesReceived <= 0)
                {
                    Console.WriteLine("No data received from GetAsyncUploadStatusBytes");
                    isError = true;
                    break;
                }

                string statusResponse = System.Text.Encoding.UTF8.GetString(buffer, 0, bytesReceived);
                Console.WriteLine($"Status response: {statusResponse}");
                Console.WriteLine();

                using (JsonDocument jsonDocument = JsonDocument.Parse(statusResponse))
                {
                    var root = jsonDocument.RootElement;
                    if (!root.TryGetProperty("code", out var codeElement))
                    {
                        isError = true;
                        Console.WriteLine("ERROR: Failed to get upload status - missing 'code' field");
                        break;
                    }

                    var statusCode = codeElement.GetInt64();

                    bool shouldExitLoop = false;
                    switch ((UploadStatus)statusCode)
                    {
                        case UploadStatus.UPLOAD_SUCCESS:
                            if (!root.TryGetProperty("status", out var statusElement))
                            {
                                Console.WriteLine("WARNING: Upload succeeded but response missing 'status' field");
                                break;
                            }

                            var uploadStatus = (UploadStatus)statusElement.GetInt64();
                            switch (uploadStatus)
                            {
                                case UploadStatus.CONFIRM_SUCCESS:
                                    isCompleted = true;
                                    shouldExitLoop = true;
                                    break;
                                case UploadStatus.CONFIRM_FAILED:
                                    Console.WriteLine("WARNING: Upload completed but confirmation failed");
                                    isCompleted = true;
                                    shouldExitLoop = true;
                                    break;
                                case UploadStatus.UPLOAD_SUCCESS:
                                    // Continue waiting
                                    break;
                                case UploadStatus.UPLOAD_UPLOADING:
                                    Console.WriteLine("INFO: Upload is in progress...");
                                    break;
                                case UploadStatus.UPLOAD_FAILED:
                                    isError = true;
                                    Console.WriteLine($"ERROR: Upload failed - {GetJsonMessage(root, "errorMessage")}");
                                    shouldExitLoop = true;
                                    break;
                                default:
                                    Console.WriteLine($"WARNING: Unexpected upload status code: {uploadStatus}");
                                    break;
                            }
                            break;
                        case UploadStatus.UPLOAD_FAILED:
                            isError = true;
                            Console.WriteLine($"ERROR: Upload task not found - {GetJsonMessage(root)}");
                            shouldExitLoop = true;
                            break;
                        default:
                            isError = true;
                            Console.WriteLine($"ERROR: Failed to get upload status - {GetJsonMessage(root)}");
                            shouldExitLoop = true;
                            break;
                    }

                    if (shouldExitLoop)
                    {
                        break;
                    }
                }
            }
            catch (JsonException ex)
            {
                Console.WriteLine($"ERROR: Failed to parse JSON response: {ex.Message}");
                isError = true;
                break;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR: Upload monitoring failed - {ex.Message}");
                isError = true;
                break;
            }

            // Sleep for polling interval before next status check
            Thread.Sleep(UploadStatusPollingIntervalMilliseconds);
            waitTime++;
        }

        if (isCompleted)
        {
            Console.WriteLine();
            Console.WriteLine($"SUCCESS: Upload completed for dataId: {dataId}");
            return true;
        }
        else if (isError)
        {
            return false;
        }
        else
        {
            Console.WriteLine($"ERROR: Upload timeout after {maxWaitTime} seconds for dataId: {dataId}");
            return false;
        }
    }

    /// <summary>
    /// Monitor multiple upload statuses (for folder uploads)
    /// </summary>
    public static bool MonitorMultipleUploadStatus(string dataId, long maxWaitTime)
    {
        return MonitorUploadStatus(dataId, maxWaitTime);
    }

    private static string GetJsonMessage(JsonElement root, string propertyName = "message",
                                         string defaultValue = "Unknown error")
    {
        if (root.TryGetProperty(propertyName, out var propertyElement))
        {
            return propertyElement.GetString() ?? defaultValue;
        }

        return defaultValue;
    }
}
}
