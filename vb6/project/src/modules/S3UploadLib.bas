Attribute VB_Name = "S3UploadLib"
' VB6 Declaration File - S3UploadLib.bas
' Usage: Add this module to your VB6 project

Option Explicit

' File operation type - keep in sync with C++ enum FileOperationType
Public Enum FileOperationType
    BATCH_CREATE = 0
    REAL_TIME_SIGNAL_APPEND = 1
End Enum

' API Declarations - DLL files are located in the lib directory
' Note: VB6 will look for DLLs in the application directory and system PATH
' For this project structure, we need to ensure the lib directory is accessible

' Set credentials and initialize AWS SDK
' Return type: JSON string
' The code is corresponds to the error code constants above
' { "code": 0, "message": "success" }
Declare Function SetCredential Lib "S3UploadLib.dll" ( _
    ByVal hippoApiUrl As String, _
    ByVal userName As String, _
    ByVal password As String _
) As String

' Return type: JSON string
' The code is corresponds to the error code constants above
' { "code": 0, "message": "success" }
Declare Function UploadFileSync Lib "S3UploadLib.dll" ( _
    ByVal accessKey As String, _
    ByVal secretKey As String, _
    ByVal sessionToken As String, _
    ByVal region As String, _
    ByVal bucketName As String, _
    ByVal objectKey As String, _
    ByVal localFilePath As String _
) As String

' Start asynchronous upload to S3
' Return value: JSON string with upload ID on success, error on failure
Declare Function UploadFileAsync Lib "S3UploadLib.dll" ( _
    ByVal region As String, _
    ByVal bucketName As String, _
    ByVal objectKey As String, _
    ByVal localFilePath As String, _
    ByVal dataId As String, _
    ByVal patientId As String, _
    ByVal fileOperationType As Long _
) As String

' Get upload status as byte array (safer for large responses)
' Parameters:
'   dataId: Data ID used to identify the upload
'   buffer: Byte array to receive the JSON data
'   bufferSize: Size of the buffer
' Return value: Number of bytes copied to buffer, 0 on error
Declare Function GetAsyncUploadStatusBytes Lib "S3UploadLib.dll" ( _
    ByVal dataId As String, _
    ByRef buffer As Byte, _
    ByVal bufferSize As Long _
) As Long

' Note: CleanupUploadsByDataId is now called automatically after successful confirmation
' No need to call it manually from VB6