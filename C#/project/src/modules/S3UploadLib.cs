using System;
using System.Runtime.InteropServices;

namespace HippoClinic.S3Upload
{
/// <summary>
/// C# Declaration File - S3UploadLib.cs
/// Usage: Add this class to your C# project
///
/// API Declarations - DLL files are located in the lib directory
/// Note: C# will look for DLLs in the application directory and system PATH
/// For this project structure, we need to ensure the lib directory is accessible
/// </summary>
public static class S3UploadLib
{
    /// <summary>
    /// Set credentials and initialize AWS SDK
    /// Return type: JSON string
    /// The code corresponds to the error code constants in Common.cs
    /// { "code": 0, "message": "success" }
    /// </summary>
    [DllImport("S3UploadLib.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    public static extern IntPtr SetCredential([MarshalAs(UnmanagedType.LPStr)] string hippoApiUrl,
                                              [MarshalAs(UnmanagedType.LPStr)] string userName,
                                              [MarshalAs(UnmanagedType.LPStr)] string password);

    /// <summary>
    /// Start asynchronous upload to S3
    /// Return value: JSON string with upload ID on success, error on failure
    /// </summary>
    [DllImport("S3UploadLib.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    public static extern IntPtr UploadFileAsync([MarshalAs(UnmanagedType.LPStr)] string region,
                                                [MarshalAs(UnmanagedType.LPStr)] string bucketName,
                                                [MarshalAs(UnmanagedType.LPStr)] string objectKey,
                                                [MarshalAs(UnmanagedType.LPStr)] string localFilePath,
                                                [MarshalAs(UnmanagedType.LPStr)] string dataId,
                                                [MarshalAs(UnmanagedType.LPStr)] string patientId,
                                                long fileOperationType);

    /// <summary>
    /// Get upload status as byte array (safer for large responses)
    /// Parameters:
    ///   dataId: Data ID used to identify the upload
    ///   buffer: Byte array to receive the JSON data
    ///   bufferSize: Size of the buffer
    /// Return value: Number of bytes copied to buffer, 0 on error
    /// </summary>
    [DllImport("S3UploadLib.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    public static extern int GetAsyncUploadStatusBytes([MarshalAs(UnmanagedType.LPStr)] string dataId,
                                                       [MarshalAs(UnmanagedType.LPArray,
                                                                  SizeParamIndex = 2)] byte[] buffer,
                                                       int bufferSize);

    /// <summary>
    /// Helper method to convert IntPtr to string (for marshalling C-style strings)
    /// </summary>
    public static string PtrToString(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero)
            return string.Empty;
        return Marshal.PtrToStringAnsi(ptr) ?? string.Empty;
    }
}
}
