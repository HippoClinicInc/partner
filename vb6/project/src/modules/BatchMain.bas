Attribute VB_Name = "BatchMain"
Option Explicit

' Required references:
' Project -> References -> Add the following libraries
' - Microsoft WinHTTP Services 5.1
' - Microsoft Scripting Runtime (for Dictionary object)

' Windows API declaration moved to Common.bas

' Required files:
' Project -> Add file -> Add the following files
' - JsonConverter.bas
' - S3UploadLib.bas
' - HippoBackend.bas
' - FileLib.bas
' - Common.bas
' - BatchMain.bas
' - DllPathManager.bas

' S3 configuration constants moved to Common.bas

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

    ' 0. Initialize HippoBackend with configuration
    ' Parameters are defined in Common.bas (HIPPO_BASE_URL, HIPPO_ACCOUNT, HIPPO_PASSWORD)
    HippoBackend.Initialize HIPPO_BASE_URL, HIPPO_ACCOUNT, HIPPO_PASSWORD
    
    ' 1. Set DLL search path and validate DLL files
    If Not SetDllSearchPath() Then
        Debug.Print "ERROR: Failed to set DLL search path"
        MsgBox "ERROR: Failed to set DLL search path. Please check if lib directory exists and contains S3UploadLib.dll.", vbCritical, "DLL Path Error"
        Exit Sub
    End If

    ' 2. Get file path from user input and validate existence
    uploadFilePath = InputBox("Please enter the file path to upload:", "File Upload", "")
    uploadFilePath = Trim(uploadFilePath)
    If Len(uploadFilePath) > 1 Then
        If (Left(uploadFilePath, 1) = Chr(34) And Right(uploadFilePath, 1) = Chr(34)) Or _
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

    ' 3. Create patient record (token managed internally)
    If Not CreatePatient(patientId) Then
        Debug.Print "ERROR: Failed to create patient"
        Exit Sub
    End If

    ' 4. Generate unique data ID (token managed internally)
    If Not GenerateDataId(dataId) Then
        Debug.Print "ERROR: Failed to generate data ID"
        Exit Sub
    End If

    ' 5. Set credentials and initialize AWS SDK
    sdkInitResult = SetCredential(gHippoBaseUrl, gHippoAccount, gHippoPassword)

    Dim jsonResponse As Object
    Set jsonResponse = JsonConverter.ParseJson(sdkInitResult)

    If jsonResponse("code") <> SDK_INIT_SUCCESS Then
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
        uploadSuccess = UploadSingleFile(uploadFilePath, s3FileKey, dataId, patientId, BATCH_CREATE, singleUploadId)

        ' 6.2.1. Monitor single file upload status
        If uploadSuccess Then
            totalFileSize = GetLocalFileSize(uploadFilePath)

            Debug.Print "Single file upload started, monitoring status..."
            uploadSuccess = MonitorUploadStatus(dataId, maxWaitTime)
        End If
    End If

    ' 7. Upload process completed (confirmation and cleanup are handled automatically by C++ bckend)
    If uploadSuccess Then
        Debug.Print "SUCCESS: Upload completed and confirmed"
        MsgBox "SUCCESS: Upload completed"
    Else
        Debug.Print "ERROR: Upload process failed"
    End If

    Exit Sub
End Sub
