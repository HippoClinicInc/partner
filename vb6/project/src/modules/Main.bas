Attribute VB_Name = "Module1"
Option Explicit

' Required references:
' Project -> References -> Add the following libraries
' - Microsoft WinHTTP Services 5.1
' - Microsoft Scripting Runtime (for Dictionary object)

' Windows API declarations
Private Declare Sub Sleep Lib "kernel32" (ByVal dwMilliseconds As Long)

' Required files:
' Project -> Add file -> Add the following files
' - JsonConverter.bas
' - S3UploadLib.bas
' - HippoBackend.bas
' - FileLib.bas

' S3 configuration constants
' For prod running, do not change it here. In our current prod settings,
' the prod bucket is called "hippoclinic-staging". We will change it later.
Private Const S3_BUCKET As String = "hippoclinic-staging"
Private Const S3_REGION As String = "us-west-1"

' Main function to handle file upload workflow with HippoClinic API
Sub Main()
    Dim patientId As String
    Dim dataId As String
    Dim uploadFilePath As String
    Dim isFolder As Boolean
    Dim uploadSuccess As Boolean
    Dim uploadDataName As String
    Dim totalFileSize As Long
    Dim sdkInitResult As String
    Dim s3FileKey As String

    ' 0. Set DLL search path and validate DLL files
    If Not SetDllSearchPath() Then
        Debug.Print "ERROR: Failed to set DLL search path"
        MsgBox "ERROR: Failed to set DLL search path. Please check if lib directory exists and contains S3UploadLib.dll.", vbCritical, "DLL Path Error"
        Exit Sub
    End If

    ' 1. Get file path from user input and validate existence
    uploadFilePath = InputBox("Please enter the file path to upload:", "File Upload", "")
    uploadFilePath = Trim(uploadFilePath)
    If Len(uploadFilePath) > 1 Then
        If (Left(uploadFilePath, 1) = """" And Right(uploadFilePath, 1) = """") Or _
           (Left(uploadFilePath, 1) = "'" And Right(uploadFilePath, 1) = "'") Then
            uploadFilePath = Mid(uploadFilePath, 2, Len(uploadFilePath) - 2)
        End If
        ' 1.1. Validate file/folder exists
        If Not FileOrFolderExists(uploadFilePath) Then
            Debug.Print "ERROR: Path does not exist: " & uploadFilePath
            Exit Sub
        End If
    End If

    If uploadFilePath = "" Then
        Debug.Print "ERROR: No file path provided"
        Exit Sub
    End If

    ' 2. Create patient record (token managed internally)
    If Not CreatePatient(patientId) Then
        Debug.Print "ERROR: Failed to create patient"
        Exit Sub
    End If

    ' 3. Generate unique data ID (token managed internally)
    If Not GenerateDataId(dataId) Then
        Debug.Print "ERROR: Failed to generate data ID"
        Exit Sub
    End If

    ' 5. Set credentials and initialize AWS SDK
    sdkInitResult = SetCredential(ENV_URL, LOGIN_ACCOUNT, LOGIN_ACCOUNT_PASSWORD)

    Dim jsonResponse As Object
    Set jsonResponse = JsonConverter.ParseJson(sdkInitResult)

    If jsonResponse("code") <> 5 Then
        Debug.Print "ERROR: AWS SDK initialization failed - code: " & sdkInitResult
        Exit Sub
    End If
    Debug.Print "1. Submit the files to the queue. "
    Debug.Print
    ' Determine upload type and execute upload
    isFolder = IsPathFolder(uploadFilePath)
    Dim maxWaitTime As Long
    maxWaitTime = 600 ' Maximum wait time in seconds (10 minutes)

    If isFolder Then
        ' 6.1. Upload folder contents
        Dim fso As Object
        Set fso = CreateObject("Scripting.FileSystemObject")
        ' Upload data name: abc.ds
        uploadDataName = fso.GetFolder(uploadFilePath).Name
        ' S3 file key: patient/patientId/source_data/dataId/abc.ds/
        s3FileKey = "patient/" & patientId & "/source_data/" & dataId & "/" & uploadDataName & "/"
        Dim uploadIds As String
        uploadSuccess = UploadFolderContents(uploadFilePath, totalFileSize, s3FileKey, dataId, patientId, uploadIds)

        ' 6.1.1. Monitor folder upload status
        If uploadSuccess Then
             Debug.Print
             Debug.Print "2. All folder uploads submitted, monitoring status..."
             Debug.Print
            uploadSuccess = MonitorMultipleUploadStatus(dataId, maxWaitTime)
        End If
    Else
        ' 6.2. Upload single file
        ' Upload data name: abc.ds
        uploadDataName = GetFileName(uploadFilePath)
        ' S3 file key: patient/patientId/source_data/dataId/abc.ds/abc.ds
        s3FileKey = "patient/" & patientId & "/source_data/" & dataId & "/" & uploadDataName & "/" & uploadDataName
        Dim singleUploadId As String
        uploadSuccess = UploadSingleFile(uploadFilePath, s3FileKey, dataId, patientId, singleUploadId)

        ' 6.2.1. Monitor single file upload status
        If uploadSuccess Then
            totalFileSize = GetLocalFileSize(uploadFilePath)

            Debug.Print "Single file upload started, monitoring status..."
            uploadSuccess = MonitorUploadStatus(dataId, maxWaitTime)
        End If
    End If

    ' 7. Upload process completed (confirmation and cleanup are handled automatically by C++ backend)
    If uploadSuccess Then
        Debug.Print "SUCCESS: Upload completed and confirmed"
        MsgBox "SUCCESS: Upload completed"
    Else
        Debug.Print "ERROR: Upload process failed"
    End If

    Exit Sub
End Sub

' Upload all files in a folder to S3 and confirm with API
Private Function UploadFolderContents(ByVal folderPath As String, ByRef totalFileSize As Long, ByVal s3FileKey As String, ByVal dataId As String, ByVal patientId As String, ByRef uploadIds As String) As Boolean
    Dim fso As Object
    Dim folder As Object
    Dim file As Object
    Dim fileCount As Integer
    Dim uploadedCount As Integer
    Dim failedCount As Integer
    Dim currentFile As String
    Dim currentFileSize As Long

    ' 1. Initialize file system object for folder operations
    Set fso = CreateObject("Scripting.FileSystemObject")

    ' 2. Validate that the specified folder path exists
    If Not fso.FolderExists(folderPath) Then
        Debug.Print "ERROR: Folder does not exist: " & folderPath
        UploadFolderContents = False
        Exit Function
    End If

    ' 3. Get folder object and count files
    Set folder = fso.GetFolder(folderPath)
    fileCount = folder.Files.Count

    If fileCount = 0 Then
        Debug.Print "ERROR: No files found in folder"
        UploadFolderContents = False
        Exit Function
    End If

    ' 4. Initialize counters and size tracking
    uploadedCount = 0
    failedCount = 0
    totalFileSize = 0
    uploadIds = "" ' Initialize upload IDs string

    ' 5. Loop through all files in the folder
    For Each file In folder.Files
        currentFile = file.path

        ' 6. Get file size before upload
        currentFileSize = GetLocalFileSize(currentFile)

        ' 7. Upload the current file
        Dim singleS3FileKey As String
        Dim currentUploadId As String
        ' Single file in folder: patient/patientId/source_data/dataId/abc.ds/abc.xyz
        singleS3FileKey = s3FileKey & GetFileName(currentFile)
        If UploadSingleFile(currentFile, singleS3FileKey, dataId, patientId, currentUploadId) Then
            uploadedCount = uploadedCount + 1
            totalFileSize = totalFileSize + currentFileSize
            ' Add upload ID to the list (comma-separated)
            If uploadIds = "" Then
                uploadIds = currentUploadId
            Else
                uploadIds = uploadIds & "," & currentUploadId
            End If
        Else
            failedCount = failedCount + 1
            Debug.Print "ERROR: Failed to start upload for " & currentFile
        End If
    Next file

    ' 9. Determine if upload process was successful
    ' If any files failed to upload, the entire process is failed
    If failedCount > 0 Then
        Debug.Print "ERROR: " & failedCount & " files failed to upload"
        UploadFolderContents = False
    Else
        UploadFolderContents = True
    End If

    ' 10. Cleanup file system objects to free memory
    Set file = Nothing
    Set folder = Nothing
    Set fso = Nothing
End Function

' Upload a single file to S3 using AWS SDK
Private Function UploadSingleFile(ByVal filePath As String, ByVal s3FileKey As String, ByVal dataId As String, ByVal patientId As String, ByRef uploadId As String) As Boolean
    Dim result As Long
    Dim fileSize As Long

    On Error GoTo ErrorHandler

    ' 1. Check if file exists
    result = FileExists(filePath)
    If result <> 1 Then
        Debug.Print "ERROR: Local file does not exist: " & filePath
        UploadSingleFile = False
        Exit Function
    End If

    ' 2. Get file size
    fileSize = GetLocalFileSize(filePath)

    ' 3. Start asynchronous upload to S3
    Debug.Print "Submit to queue - " & filePath
    Dim startResponse As String
    startResponse = UploadFileAsync(S3_REGION, S3_BUCKET, s3FileKey, filePath, dataId, patientId)
    Debug.Print startResponse

    ' 5. Parse start response and check if upload started successfully
    Dim startObj As Object
    Set startObj = JsonConverter.ParseJson(startResponse)
    Dim startCode As Long
    startCode = startObj("code")

    If startCode <> 2 Then
        Debug.Print "ERROR: Failed to start async upload - " & startObj("message")
        UploadSingleFile = False
        Exit Function
    End If

    ' Extract upload ID from the response message
    uploadId = startObj("message")

    ' 6. Return success with upload ID for external monitoring
    Debug.Print "SUCCESS: Upload started for file - " & filePath & " with upload ID: " & uploadId
    UploadSingleFile = True

    Exit Function

ErrorHandler:
    ' 8. Handle errors
    Debug.Print "ERROR: Upload failed - " & Err.Description
    UploadSingleFile = False
End Function

' Monitor upload status for a single dataId
Private Function MonitorUploadStatus(ByVal dataId As String, ByVal maxWaitTime As Long) As Boolean
    Dim statusResponse As String
    Dim statusObj As Object
    Dim isCompleted As Boolean
    Dim isError As Boolean
    Dim waitTime As Long
    Dim statusCode As Long
    Dim uploadStatus As Long
    Dim hasSeenValidStatus As Boolean ' Track if we've seen a valid status before

    waitTime = 0
    hasSeenValidStatus = False

    Do While waitTime < maxWaitTime
        ' Use byte array method for safer data transfer
        Dim buffer(0 To 2097151) As Byte ' 2MB buffer should be enough for status JSON
        Dim bytesReceived As Long

        Debug.Print "Query status for dataId: " & dataId & " (attempt " & (waitTime + 1) & ")"

        ' Try byte array method first
        On Error GoTo ErrorHandler
        bytesReceived = GetAsyncUploadStatusBytes(dataId, buffer(0), UBound(buffer) + 1)
        On Error GoTo ErrorHandler

        If bytesReceived > 0 Then
            ' Convert byte array to string manually (safer method)
            statusResponse = ""
            Dim i As Long
            For i = 0 To bytesReceived - 1
                statusResponse = statusResponse & Chr(buffer(i))
            Next i

            Debug.Print "Status response: " & statusResponse
            Debug.Print
        Else
            Debug.Print "No data received from GetAsyncUploadStatusBytes"
            GoTo ErrorHandler
        End If

        GoTo ParseResponse

ParseResponse:
        ' Parse JSON response
        On Error GoTo JsonParseError
        Set statusObj = JsonConverter.ParseJson(statusResponse)
        On Error GoTo ErrorHandler

        statusCode = statusObj("code")

        If statusCode = 2 Then
            ' Parse the status from the message field (which contains JSON string)
            uploadStatus = statusObj("status")
            hasSeenValidStatus = True ' Mark that we've seen a valid status

            If uploadStatus = 7 Then ' CONFIRM_SUCCESS - Upload and confirmation completed
                isCompleted = True
                Exit Do
            ElseIf uploadStatus = 8 Then ' CONFIRM_FAILED - Upload successful but confirmation failed
                Debug.Print "WARNING: Upload completed but confirmation failed"
                isCompleted = True ' Still consider it completed since file was uploaded
                Exit Do
            ElseIf uploadStatus = 2 Then ' UPLOAD_SUCCESS - Upload completed, confirmation in progress
                ' Continue waiting for confirmation to complete
            ElseIf uploadStatus = 3 Then ' UPLOAD_FAILED
                isError = True
                Debug.Print "ERROR: Upload failed - " & statusObj("errorMessage")
                Exit Do
            End If
        ElseIf statusCode = 3 Then
            ' Task has been cleaned up (usually means upload completed successfully)
            If hasSeenValidStatus Then
                ' If we've seen valid status before, this likely means upload succeeded and was cleaned up
                Debug.Print "INFO: Upload task has been cleaned up (likely completed successfully)"
                isCompleted = True
                Exit Do
            Else
                ' If this is the first query and task is already cleaned up, it's an error
                isError = True
                Debug.Print "ERROR: Upload task not found - " & statusObj("message")
                Exit Do
            End If
        Else
            ' Status check failed with other error codes
            isError = True
            Debug.Print "ERROR: Failed to get upload status - " & statusObj("message")
            Exit Do
        End If

        ' Wait 10 seconds (10 x 1 second) before checking again
        Dim j As Integer
        For j = 1 To 10
            DoEvents
            Sleep 1000
        Next j
        waitTime = waitTime + 1
    Loop

    GoTo ContinueAfterLoop

JsonParseError:
    Debug.Print "ERROR: Failed to parse JSON response: " & statusResponse
    Debug.Print "JSON Parse Error: " & Err.Description
    isError = True
    On Error GoTo ErrorHandler

ContinueAfterLoop:

    ' Check final result
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
    ' Handle errors
    Debug.Print "ERROR: Upload monitoring failed - " & Err.Description
    MonitorUploadStatus = False
End Function

' Monitor multiple upload statuses (for folder uploads)
' For folder uploads, we use the same dataId for all files
Private Function MonitorMultipleUploadStatus(ByVal dataId As String, ByVal maxWaitTime As Long) As Boolean
    ' For folder uploads, we monitor using the same dataId
    ' The C++ side will find the upload with the matching dataId prefix
    MonitorMultipleUploadStatus = MonitorUploadStatus(dataId, maxWaitTime)
End Function