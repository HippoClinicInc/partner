' Attribute VB_Name = "Common"
Option Explicit

' Shared enums and configuration - used by both batch and real-time flows
'
' NOTE: Keep UploadStatus in sync with C++ enum UploadStatus in `c_plus/src/common/S3Common.h`.
' If you change values or add/remove states on one side, you MUST update the other side.
' This enum is used for both the top-level "code" field and the detailed "status" field
' returned from the C++ DLL APIs.
Public Enum UploadStatus
    UPLOAD_PENDING = 0
    UPLOAD_UPLOADING = 1
    UPLOAD_SUCCESS = 2
    UPLOAD_FAILED = 3
    UPLOAD_CANCELLED = 4
    SDK_INIT_SUCCESS = 5
    SDK_CLEAN_SUCCESS = 6
    CONFIRM_SUCCESS = 7
    CONFIRM_FAILED = 8
End Enum

' Keep in sync with C++ enum FileOperationType
Public Enum FileOperationType
    BATCH_CREATE = 0
    REAL_TIME_SIGNAL_APPEND = 1
End Enum

' S3 configuration constants
' For prod running, do not change it here. In our current prod settings,
' the prod bucket is called "hippoclinic-staging". We will change it later.
Public Const S3_BUCKET As String = "hippoclinic-staging"
Public Const S3_REGION As String = "us-west-1"

' Windows API declarations (used by monitoring loops)
Public Declare Sub Sleep Lib "kernel32" (ByVal dwMilliseconds As Long)

' Upload all files in a folder to S3 and confirm with API (uses fileOperationType)
Public Function UploadFolderContents(ByVal folderPath As String, ByRef totalFileSize As Long, ByVal s3FileKey As String, ByVal dataId As String, ByVal patientId As String, ByVal fileOperationType As Long, ByRef uploadIds As String) As Boolean
    Dim fileSystem As Object
    Dim folder As Object
    Dim currentFileObject As Object
    Dim fileCount As Integer
    Dim uploadedCount As Integer
    Dim failedCount As Integer
    Dim currentFile As String
    Dim currentFileSize As Long
    Dim singleS3FileKey As String
    Dim currentUploadId As String

    Set fileSystem = CreateObject("Scripting.FileSystemObject")

    If Not fileSystem.FolderExists(folderPath) Then
        Debug.Print "ERROR: Folder does not exist: " & folderPath
        UploadFolderContents = False
        Exit Function
    End If

    Set folder = fileSystem.GetFolder(folderPath)
    fileCount = folder.Files.Count

    If fileCount = 0 Then
        Debug.Print "ERROR: No files found in folder"
        UploadFolderContents = False
        Exit Function
    End If

    uploadedCount = 0
    failedCount = 0
    totalFileSize = 0
    uploadIds = ""

    ' Upload all files in the folder
    For Each currentFileObject In folder.Files
        currentFile = currentFileObject.Path
        currentFileSize = GetLocalFileSize(currentFile)

        singleS3FileKey = s3FileKey & GetFileName(currentFile)
        If UploadSingleFile(currentFile, singleS3FileKey, dataId, patientId, fileOperationType, currentUploadId) Then
            uploadedCount = uploadedCount + 1
            totalFileSize = totalFileSize + currentFileSize
            If uploadIds = "" Then
                uploadIds = currentUploadId
            Else
                uploadIds = uploadIds & "," & currentUploadId
            End If
        Else
            failedCount = failedCount + 1
            Debug.Print "ERROR: Failed to start upload for " & currentFile
        End If
    Next currentFileObject

    If failedCount > 0 Then
        Debug.Print "ERROR: " & failedCount & " files failed to upload"
        UploadFolderContents = False
    Else
        UploadFolderContents = True
    End If

    Set currentFileObject = Nothing
    Set folder = Nothing
    Set fileSystem = Nothing
End Function

' Upload a single file to S3 using AWS SDK (uses fileOperationType)
Public Function UploadSingleFile(ByVal filePath As String, ByVal s3FileKey As String, ByVal dataId As String, ByVal patientId As String, ByVal fileOperationType As Long, ByRef uploadId As String) As Boolean
    Dim existsResult As Long
    Dim fileSize As Long

    On Error GoTo ErrorHandler

    existsResult = FileExists(filePath)
    If existsResult <> 1 Then
        Debug.Print "ERROR: Local file does not exist: " & filePath
        UploadSingleFile = False
        Exit Function
    End If

    fileSize = GetLocalFileSize(filePath)

    Debug.Print "Submit to queue - " & filePath
    Dim startResponse As String
    startResponse = UploadFileAsync(S3_REGION, S3_BUCKET, s3FileKey, filePath, dataId, patientId, fileOperationType)
    Debug.Print startResponse

    Dim startObj As Object
    Set startObj = JsonConverter.ParseJson(startResponse)
    Dim startCode As Long
    startCode = startObj("code")

    If startCode <> UPLOAD_SUCCESS Then
        Debug.Print "ERROR: Failed to start async upload - " & startObj("message")
        UploadSingleFile = False
        Exit Function
    End If

    uploadId = startObj("message")
    Debug.Print "SUCCESS: Upload started for file - " & filePath & " with upload ID: " & uploadId
    UploadSingleFile = True
    Exit Function

ErrorHandler:
    Debug.Print "ERROR: Upload failed - " & Err.Description
    UploadSingleFile = False
End Function

' Monitor upload status for a single dataId
Public Function MonitorUploadStatus(ByVal dataId As String, ByVal maxWaitTime As Long) As Boolean
    Dim statusResponse As String
    Dim statusObj As Object
    Dim isCompleted As Boolean
    Dim isError As Boolean
    Dim waitTime As Long
    Dim statusCode As Long
    Dim uploadStatus As Long
    Dim hasSeenValidStatus As Boolean
    Dim buffer(0 To 2097151) As Byte
    Dim bytesReceived As Long
    Dim byteIndex As Long
    Dim sleepCounter As Integer

    waitTime = 0
    hasSeenValidStatus = False

    Do While waitTime < maxWaitTime
        Debug.Print "Query status for dataId: " & dataId & " (attempt " & (waitTime + 1) & ")"

        On Error GoTo ErrorHandler
        bytesReceived = GetAsyncUploadStatusBytes(dataId, buffer(0), UBound(buffer) + 1)
        On Error GoTo ErrorHandler

        If bytesReceived > 0 Then
            statusResponse = ""
            For byteIndex = 0 To bytesReceived - 1
                statusResponse = statusResponse & Chr(buffer(byteIndex))
            Next byteIndex

            Debug.Print "Status response: " & statusResponse
            Debug.Print
        Else
            Debug.Print "No data received from GetAsyncUploadStatusBytes"
            GoTo ErrorHandler
        End If

        GoTo ParseResponse

ParseResponse:
        On Error GoTo JsonParseError
        Set statusObj = JsonConverter.ParseJson(statusResponse)
        On Error GoTo ErrorHandler

        statusCode = statusObj("code")

        If statusCode = UPLOAD_SUCCESS Then
            uploadStatus = statusObj("status")
            hasSeenValidStatus = True

            If uploadStatus = CONFIRM_SUCCESS Then
                isCompleted = True
                Exit Do
            ElseIf uploadStatus = CONFIRM_FAILED Then
                Debug.Print "WARNING: Upload completed but confirmation failed"
                isCompleted = True
                Exit Do
            ElseIf uploadStatus = UPLOAD_SUCCESS Then
                ' Continue waiting
            ElseIf uploadStatus = UPLOAD_FAILED Then
                isError = True
                Debug.Print "ERROR: Upload failed - " & statusObj("errorMessage")
                Exit Do
            End If
        ElseIf statusCode = UPLOAD_FAILED Then
            If hasSeenValidStatus Then
                Debug.Print "INFO: Upload task has been cleaned up (likely completed successfully)"
                isCompleted = True
                Exit Do
            Else
                isError = True
                Debug.Print "ERROR: Upload task not found - " & statusObj("message")
                Exit Do
            End If
        Else
            isError = True
            Debug.Print "ERROR: Failed to get upload status - " & statusObj("message")
            Exit Do
        End If

        For sleepCounter = 1 To 10
            DoEvents
            Sleep 1000
        Next sleepCounter
        waitTime = waitTime + 1
    Loop

    GoTo ContinueAfterLoop

JsonParseError:
    Debug.Print "ERROR: Failed to parse JSON response: " & statusResponse
    Debug.Print "JSON Parse Error: " & Err.Description
    isError = True
    On Error GoTo ErrorHandler

ContinueAfterLoop:
    If isCompleted Then
        Debug.Print
        Debug.Print "SUCCESS: Upload completed for dataId: " & dataId
        MonitorUploadStatus = True
    ElseIf isError Then
        MonitorUploadStatus = False
    Else
        Debug.Print "ERROR: Upload timeout after " & maxWaitTime & " seconds for dataId: " & dataId
        MonitorUploadStatus = False
    End If
    Exit Function

ErrorHandler:
    Debug.Print "ERROR: Upload monitoring failed - " & Err.Description
    MonitorUploadStatus = False
End Function

' Monitor multiple upload statuses (for folder uploads)
Public Function MonitorMultipleUploadStatus(ByVal dataId As String, ByVal maxWaitTime As Long) As Boolean
    MonitorMultipleUploadStatus = MonitorUploadStatus(dataId, maxWaitTime)
End Function
