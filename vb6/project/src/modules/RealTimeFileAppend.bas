Attribute VB_Name = "RealTimeFileAppend"
Option Explicit

' NOTE: 与 `BatchMain.bas` 的整体流程一致，唯一区别：
' - 这里调用上传接口时传入的 FileOperationType 为 REAL_TIME_SIGNAL_APPEND（增量实时追加）
' - 批量场景在 `BatchMain.bas` 里传入 BATCH_CREATE。

' Required references:
' Project -> References -> Add the following libraries
' - Microsoft WinHTTP Services 5.1
' - Microsoft Scripting Runtime (for Dictionary object)

' Required files:
' Project -> Add file -> Add the following files
' - JsonConverter.bas
' - S3UploadLib.bas
' - HippoBackend.bas
' - FileLib.bas
' - Common.bas

' S3 configuration常量在 Common.bas 中统一维护

' Real-time signal append upload flow
Public Sub Main()
    Dim patientId As String
    Dim dataId As String
    Dim uploadFilePath As String
    Dim isFolder As Boolean
    Dim uploadSuccess As Boolean
    Dim uploadDataName As String
    Dim totalFileSize As Long
    Dim sdkInitResult As String
    Dim s3FileKey As String

    If Not SetDllSearchPath() Then
        Debug.Print "ERROR: Failed to set DLL search path"
        MsgBox "ERROR: Failed to set DLL search path. Please check if lib directory exists and contains S3UploadLib.dll.", vbCritical, "DLL Path Error"
        Exit Sub
    End If

    uploadFilePath = InputBox("Please enter the file path to upload (real-time):", "File Upload (Real-time)", "")
    uploadFilePath = Trim(uploadFilePath)
    If Len(uploadFilePath) > 1 Then
        If (Left(uploadFilePath, 1) = "\"" And Right(uploadFilePath, 1) = "\"") Or _
           (Left(uploadFilePath, 1) = "'" And Right(uploadFilePath, 1) = "'") Then
            uploadFilePath = Mid(uploadFilePath, 2, Len(uploadFilePath) - 2)
        End If
        If Not FileOrFolderExists(uploadFilePath) Then
            Debug.Print "ERROR: Path does not exist: " & uploadFilePath
            Exit Sub
        End If
    End If

    If uploadFilePath = "" Then
        Debug.Print "ERROR: No file path provided"
        Exit Sub
    End If

    If Not CreatePatient(patientId) Then
        Debug.Print "ERROR: Failed to create patient"
        Exit Sub
    End If

    If Not GenerateDataId(dataId) Then
        Debug.Print "ERROR: Failed to generate data ID"
        Exit Sub
    End If

    sdkInitResult = SetCredential(ENV_URL, LOGIN_ACCOUNT, LOGIN_ACCOUNT_PASSWORD)
    Dim jsonResponse As Object
    Set jsonResponse = JsonConverter.ParseJson(sdkInitResult)
    If jsonResponse("code") <> SDK_INIT_SUCCESS Then
        Debug.Print "ERROR: AWS SDK initialization failed - code: " & sdkInitResult
        Exit Sub
    End If

    isFolder = IsPathFolder(uploadFilePath)
    Dim maxWaitTime As Long
    maxWaitTime = 600

    If isFolder Then
        Dim fso As Object
        Set fso = CreateObject("Scripting.FileSystemObject")
        uploadDataName = fso.GetFolder(uploadFilePath).Name
        s3FileKey = "patient/" & patientId & "/source_data/" & dataId & "/" & uploadDataName & "/"
        Dim uploadIds As String
        uploadSuccess = UploadFolderContents(uploadFilePath, totalFileSize, s3FileKey, dataId, patientId, REAL_TIME_SIGNAL_APPEND, uploadIds)

        If uploadSuccess Then
            Debug.Print
            Debug.Print "2. All folder uploads submitted (real-time), monitoring status..."
            Debug.Print
            uploadSuccess = MonitorMultipleUploadStatus(dataId, maxWaitTime)
        End If
    Else
        uploadDataName = GetFileName(uploadFilePath)
        s3FileKey = "patient/" & patientId & "/source_data/" & dataId & "/" & uploadDataName & "/" & uploadDataName
        Dim singleUploadId As String
        uploadSuccess = UploadSingleFile(uploadFilePath, s3FileKey, dataId, patientId, REAL_TIME_SIGNAL_APPEND, singleUploadId)

        If uploadSuccess Then
            totalFileSize = GetLocalFileSize(uploadFilePath)
            Debug.Print "Single file upload started (real-time), monitoring status..."
            uploadSuccess = MonitorUploadStatus(dataId, maxWaitTime)
        End If
    End If

    If uploadSuccess Then
        Debug.Print "SUCCESS: Real-time upload completed and confirmed"
        MsgBox "SUCCESS: Real-time upload completed"
    Else
        Debug.Print "ERROR: Real-time upload process failed"
    End If
End Sub
