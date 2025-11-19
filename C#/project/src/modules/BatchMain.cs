using System;
using System.Text.Json;
using System.Threading.Tasks;

namespace HippoClinic.S3Upload
{
/// <summary>
/// BatchMain.cs - Main function to handle file upload workflow with HippoClinic API
///
/// Required references:
/// - System.Net.Http (for HTTP requests)
/// - System.Text.Json (for JSON parsing)
/// - System.Runtime.InteropServices (for DLL interop)
///
/// Required files:
/// - Common.cs
/// - S3UploadLib.cs
/// - HippoBackend.cs
/// - FileLib.cs
/// - DllPathManager.cs
/// - BatchMain.cs
///
/// S3 configuration constants moved to Common.cs
/// </summary>
public class BatchMain
{
    /// <summary>
    /// Main function to handle file upload workflow with HippoClinic API
    /// </summary>
    public static async Task Main(string[] args)
    {
        // HippoClinic configuration constants (change these according to your environment)
        const string HIPPO_ACCOUNT = "2546566177@qq.com";
        const string HIPPO_PASSWORD = "u3LJ2lXv";

        // Patient configuration constants (for demo purposes)
        const string DEFAULT_MRN = "123";
        const string DEFAULT_PATIENT_NAME = "Test api";

        // Maximum wait time for upload monitoring (seconds)
        const long UPLOAD_MAX_WAIT_TIME_SECONDS = 600; 

        string patientId;
        string dataId;
        string uploadFilePath;
        bool isFolder;
        bool uploadSuccess;
        string uploadDataName;
        long totalFileSize;
        string sdkInitResult;
        string s3FileKey;

        // 0. Initialize HippoBackend with configuration
        HippoBackend.Initialize(Common.HippoBaseUrl, HIPPO_ACCOUNT, HIPPO_PASSWORD);

        // 1. Set DLL search path and validate DLL files
        if (!DllPathManager.SetDllSearchPath())
        {
            Console.WriteLine("ERROR: Failed to set DLL search path");
            Console.WriteLine("ERROR: Failed to set DLL search path. Please check if lib directory exists and " +
                              "contains S3UploadLib.dll.");
            return;
        }

        // 2. Get file path from user input and validate existence
        Console.Write("Please enter the file path to upload: ");
        uploadFilePath = (Console.ReadLine() ?? string.Empty).Trim();
        if (string.IsNullOrEmpty(uploadFilePath))
        {
            Console.WriteLine("ERROR: No file path provided");
            return;
        }

        // Remove quotes if present
        if (uploadFilePath.Length > 1 && ((uploadFilePath.StartsWith("\"") && uploadFilePath.EndsWith("\"")) ||
                                          (uploadFilePath.StartsWith("'") && uploadFilePath.EndsWith("'"))))
        {
            uploadFilePath = uploadFilePath.Substring(1, uploadFilePath.Length - 2);
        }

        // 1.1. Validate file/folder exists
        if (!FileLib.FileOrFolderExists(uploadFilePath))
        {
            Console.WriteLine($"ERROR: Path does not exist: {uploadFilePath}");
            return;
        }

        // 3. Create patient record (token managed internally)
        var (patientCreated, createdPatientId) =
            await HippoBackend.CreatePatientAsync(DEFAULT_MRN, DEFAULT_PATIENT_NAME).ConfigureAwait(false);
        if (!patientCreated)
        {
            Console.WriteLine("ERROR: Failed to create patient");
            return;
        }
        patientId = createdPatientId;

        // 4. Generate unique data ID (token managed internally)
        var (dataIdGenerated, generatedDataId) =
            await HippoBackend.GenerateDataIdAsync().ConfigureAwait(false);
        if (!dataIdGenerated)
        {
            Console.WriteLine("ERROR: Failed to generate data ID");
            return;
        }
        dataId = generatedDataId;

        // 5. Set credentials and initialize AWS SDK
        IntPtr sdkInitResultPtr = S3UploadLib.SetCredential(Common.HippoBaseUrl, HIPPO_ACCOUNT, HIPPO_PASSWORD);
        sdkInitResult = S3UploadLib.PtrToString(sdkInitResultPtr);

        using (JsonDocument jsonDocument = JsonDocument.Parse(sdkInitResult))
        {
            var root = jsonDocument.RootElement;
            if (root.TryGetProperty("code", out var codeElement))
            {
                if (codeElement.GetInt64() != (long)UploadStatus.SDK_INIT_SUCCESS)
                {
                    Console.WriteLine($"ERROR: AWS SDK initialization failed - code: {sdkInitResult}");
                    return;
                }
            }
        }

        Console.WriteLine("1. Submit the files to the queue.");
        Console.WriteLine();

        // Determine upload type and execute upload
        isFolder = FileLib.IsPathFolder(uploadFilePath);
        if (isFolder)
        {
            // 6.1. Upload folder contents
            // Upload data name: abc.ds
            uploadDataName = System.IO.Path.GetFileName(uploadFilePath.TrimEnd('\\', '/'));
            // S3 file key: patient/patientId/source_data/dataId/abc.ds/
            s3FileKey = $"patient/{patientId}/source_data/{dataId}/{uploadDataName}/";
            string uploadIds;
            uploadSuccess = Common.UploadFolderContents(uploadFilePath, out totalFileSize, s3FileKey, dataId, patientId,
                                                        out uploadIds);

            // 6.1.1. Monitor folder upload status
            if (uploadSuccess)
            {
                Console.WriteLine();
                Console.WriteLine("2. All folder uploads submitted, monitoring status...");
                Console.WriteLine();
                uploadSuccess = Common.MonitorMultipleUploadStatus(dataId, UPLOAD_MAX_WAIT_TIME_SECONDS);
            }
        }
        else
        {
            // 6.2. Upload single file
            // Upload data name: abc.ds
            uploadDataName = FileLib.GetFileName(uploadFilePath);
            // S3 file key: patient/patientId/source_data/dataId/abc.ds/abc.ds
            s3FileKey = $"patient/{patientId}/source_data/{dataId}/{uploadDataName}/{uploadDataName}";
            string singleUploadId;
            uploadSuccess = Common.UploadSingleFile(uploadFilePath, s3FileKey, dataId, patientId,
                                                    FileOperationType.BATCH_CREATE, out singleUploadId);

            // 6.2.1. Monitor single file upload status
            if (uploadSuccess)
            {
                totalFileSize = FileLib.GetLocalFileSize(uploadFilePath);

                Console.WriteLine("Single file upload started, monitoring status...");
                uploadSuccess = Common.MonitorUploadStatus(dataId, UPLOAD_MAX_WAIT_TIME_SECONDS);
            }
        }

        // 7. Upload process completed (confirmation and cleanup are handled automatically by C++ backend)
        if (uploadSuccess)
        {
            Console.WriteLine("SUCCESS: Upload completed and confirmed");
        }
        else
        {
            Console.WriteLine("ERROR: Upload process failed");
        }
    }
}
}
