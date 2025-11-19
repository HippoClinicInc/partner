using System;
using System.Text.Json;
using System.Threading.Tasks;

namespace HippoClinic.S3Upload
{
/// <summary>
/// RealTimeFileAppendMain.cs - Real-time signal append upload flow
///
/// NOTE: The overall flow is the same as in BatchMain.cs.
/// - The only difference: the upload interface here uses FileOperationType as RealTimeAppend (real-time incremental
/// append).
/// - In batch scenarios (BatchMain.cs), BatchCreate is used.
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
/// - RealTimeFileAppendMain.cs
///
/// S3 configuration constants are maintained centrally in Common.cs
/// </summary>
public class RealTimeFileAppendMain
{
    /// <summary>
    /// Real-time signal append upload flow
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
        bool uploadSuccess;
        string uploadDataName;
        long totalFileSize;
        string sdkInitResult;
        string s3FileKey;

        // 0. Initialize HippoBackend with configuration
        HippoBackend.Initialize(Common.HippoBaseUrl, HIPPO_ACCOUNT, HIPPO_PASSWORD);

        // 0. Set DLL search path and validate DLL files
        if (!DllPathManager.SetDllSearchPath())
        {
            Console.WriteLine("ERROR: Failed to set DLL search path");
            Console.WriteLine("ERROR: Failed to set DLL search path. Please check if lib directory exists and " +
                              "contains S3UploadLib.dll.");
            return;
        }

        // 1. Get file path from user input and validate existence
        Console.Write("Please enter the file path to upload (real-time): ");
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

        // 2. Check if the path is a folder (not supported for real-time append)
        if (FileLib.IsPathFolder(uploadFilePath))
        {
            Console.WriteLine("ERROR: Folder upload is not supported for RealTimeAppend mode");
            Console.WriteLine(
                "ERROR: Folder upload is not supported for RealTimeAppend mode. Please provide a single file path.");
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

        // 4. Ask user to choose between new or append mode
        Console.WriteLine("Please choose upload mode:");
        Console.WriteLine("1 - New (create new dataId)");
        Console.WriteLine("2 - Append (use existing dataId)");
        Console.Write("Enter choice (default: 1): ");
        string userChoice = (Console.ReadLine() ?? string.Empty).Trim();

        if (string.IsNullOrEmpty(userChoice))
        {
            Console.WriteLine("ERROR: User cancelled upload mode selection");
            return;
        }

        if (userChoice == "2")
        {
            // 4.1. Append mode: ask user to input existing dataId
            Console.Write("Please enter the existing dataId to append: ");
            dataId = (Console.ReadLine() ?? string.Empty).Trim();
            if (string.IsNullOrEmpty(dataId))
            {
                Console.WriteLine("ERROR: No dataId provided for append mode");
                return;
            }
            Console.WriteLine($"Using existing dataId for append: {dataId}");
        }
        else
        {
            // 4.2. New mode: generate new dataId (default behavior)
            var (dataIdGenerated, generatedDataId) =
                await HippoBackend.GenerateDataIdAsync().ConfigureAwait(false);
            if (!dataIdGenerated)
            {
                Console.WriteLine("ERROR: Failed to generate data ID");
                return;
            }
            dataId = generatedDataId;
        }

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

        // 6. Upload single file
        // Upload data name: abc.ds
        uploadDataName = FileLib.GetFileName(uploadFilePath);
        // S3 file key: patient/patientId/source_data/dataId/abc.ds/abc.ds
        s3FileKey = $"patient/{patientId}/source_data/{dataId}/{uploadDataName}/{uploadDataName}";
        string singleUploadId;
        uploadSuccess = Common.UploadSingleFile(uploadFilePath, s3FileKey, dataId, patientId,
                                                FileOperationType.REAL_TIME_APPEND, out singleUploadId);

        // 7. Monitor single file upload status
        if (uploadSuccess)
        {
            totalFileSize = FileLib.GetLocalFileSize(uploadFilePath);
            Console.WriteLine("Single file upload started (real-time), monitoring status...");
            uploadSuccess = Common.MonitorUploadStatus(dataId, UPLOAD_MAX_WAIT_TIME_SECONDS);
        }

        // 8. Upload process completed (confirmation and cleanup are handled automatically by C++ backend)
        if (uploadSuccess)
        {
            Console.WriteLine("SUCCESS: Real-time upload completed and confirmed");
        }
        else
        {
            Console.WriteLine("ERROR: Real-time upload process failed");
        }
    }
}
}
