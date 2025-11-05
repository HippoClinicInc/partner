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

' S3 configuration constants (can be overridden by environment-specific modules if needed)
Public Const S3_BUCKET As String = "hippoclinic-staging"
Public Const S3_REGION As String = "us-west-1"

' Allowed file extensions for REAL_TIME_SIGNAL_APPEND mode
Public Const FILE_EXT_EDF As String = "edf"
Public Const FILE_EXT_BDF As String = "bdf"
Public Const FILE_EXT_AVI As String = "avi"
Public Const FILE_EXT_MP4 As String = "mp4"

' Windows API declarations (used by monitoring loops)
Public Declare Sub Sleep Lib "kernel32" (ByVal dwMilliseconds As Long)

' Validate folder contents for REAL_TIME_SIGNAL_APPEND mode
' This validation ensures:
' - Only .edf, .bdf, .avi, .mp4 files are allowed
' - If folder contains video files, it must also have at least one .edf or .bdf file
Public Function ValidateFolderContents(ByVal folderPath As String, ByRef errorMessage As String) As Boolean
    Dim fileSystem As Object
    Dim folder As Object
    Dim currentFileObject As Object
    Dim fileExt As String
    Dim hasEdfOrBdf As Boolean
    Dim hasVideo As Boolean
    Dim invalidFiles As String
    
    Set fileSystem = CreateObject("Scripting.FileSystemObject")
    
    If Not fileSystem.FolderExists(folderPath) Then
        errorMessage = "Folder does not exist: " & folderPath
        ValidateFolderContents = False
        Exit Function
    End If
    
    Set folder = fileSystem.GetFolder(folderPath)
    
    If folder.Files.Count = 0 Then
        errorMessage = "No files found in folder"
        ValidateFolderContents = False
        Exit Function
    End If
    
    hasEdfOrBdf = False
    hasVideo = False
    invalidFiles = ""
    
    ' Check each file's extension
    For Each currentFileObject In folder.Files
        fileExt = LCase(fileSystem.GetExtensionName(currentFileObject.Name))
        
        ' Check if file type is allowed
        If fileExt <> FILE_EXT_EDF And fileExt <> FILE_EXT_BDF And fileExt <> FILE_EXT_AVI And fileExt <> FILE_EXT_MP4 Then
            If invalidFiles = "" Then
                invalidFiles = currentFileObject.Name
            Else
                invalidFiles = invalidFiles & ", " & currentFileObject.Name
            End If
        End If
        
        ' Track file types
        If fileExt = FILE_EXT_EDF Or fileExt = FILE_EXT_BDF Then
            hasEdfOrBdf = True
        ElseIf fileExt = FILE_EXT_AVI Or fileExt = FILE_EXT_MP4 Then
            hasVideo = True
        End If
    Next currentFileObject
    
    ' Check for invalid files
    If invalidFiles <> "" Then
        errorMessage = "Folder contains unsupported file types. Only .edf, .bdf, .avi, .mp4 files are allowed." & vbCrLf & "Unsupported files: " & invalidFiles
        ValidateFolderContents = False
        Exit Function
    End If
    
    ' Check if folder only has video files
    If hasVideo And Not hasEdfOrBdf Then
        errorMessage = "Folder contains only video files. At least one .edf or .bdf file is required."
        ValidateFolderContents = False
        Exit Function
    End If
    
    ValidateFolderContents = True
    errorMessage = ""
    
    Set currentFileObject = Nothing
    Set folder = Nothing
    Set fileSystem = Nothing
End Function

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
    Dim fileExt As String
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

    ' For REAL_TIME_SIGNAL_APPEND mode, upload files in specific order: signal files first, then video files
    If fileOperationType = REAL_TIME_SIGNAL_APPEND Then
        ' Arrays to store files by priority
        Dim signalFiles() As String
        Dim videoFiles() As String
        Dim signalCount As Integer
        Dim videoCount As Integer
        Dim fileIndex As Integer
        
        ' Initialize arrays
        ReDim signalFiles(0 To fileCount - 1)
        ReDim videoFiles(0 To fileCount - 1)
        signalCount = 0
        videoCount = 0
        
        ' Separate files into signal files (.edf, .bdf) and video files (.avi, .mp4)
        For Each currentFileObject In folder.Files
            fileExt = LCase(fileSystem.GetExtensionName(currentFileObject.Name))
            
            If fileExt = FILE_EXT_EDF Or fileExt = FILE_EXT_BDF Then
                signalFiles(signalCount) = currentFileObject.Path
                signalCount = signalCount + 1
            ElseIf fileExt = FILE_EXT_AVI Or fileExt = FILE_EXT_MP4 Then
                videoFiles(videoCount) = currentFileObject.Path
                videoCount = videoCount + 1
            End If
        Next currentFileObject
        
        ' Upload signal files first (.edf, .bdf)
        For fileIndex = 0 To signalCount - 1
            currentFile = signalFiles(fileIndex)
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
        Next fileIndex
        
        ' Then upload video files (.avi, .mp4)
        For fileIndex = 0 To videoCount - 1
            currentFile = videoFiles(fileIndex)
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
        Next fileIndex
    Else
        ' For BATCH_CREATE mode, upload all files without ordering
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
    End If

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
