Attribute VB_Name = "RealTimeFileAppendMain"
Option Explicit

' NOTE: The overall flow is the same as in `BatchMain.bas`.
' - The only difference: the upload interface here uses FileOperationType as REAL_TIME_SIGNAL_APPEND (real-time incremental append).
' - In batch scenarios (`BatchMain.bas`), BATCH_CREATE is used.

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

' S3 configuration constants are maintained centrally in Common.bas

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

    ' 0. Set DLL search path and validate DLL files
    If Not SetDllSearchPath() Then
        Debug.Print "ERROR: Failed to set DLL search path"
        MsgBox "ERROR: Failed to set DLL search path. Please check if lib directory exists and contains S3UploadLib.dll.", vbCritical, "DLL Path Error"
        Exit Sub
    End If

    ' 1. Get file path from user input and validate existence
    uploadFilePath = InputBox("Please enter the file path to upload (real-time):", "File Upload (Real-time)", "")
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

    ' 2. Create patient record (token managed internally)
    If Not CreatePatient(patientId) Then
        Debug.Print "ERROR: Failed to create patient"
        Exit Sub
    End If

    ' 3. Determine if upload is folder or single file
    isFolder = IsPathFolder(uploadFilePath)
    
    ' 3.1. For single file upload, ask user to choose between new or append
    If Not isFolder Then
        Dim userChoice As String
        userChoice = InputBox("Please choose upload mode:" & vbCrLf & _
                              "1 - New (create new dataId)" & vbCrLf & _
                              "2 - Append (use existing dataId)", _
                              "Upload Mode", "1")
        
        If userChoice = "" Then
            Debug.Print "ERROR: User cancelled upload mode selection"
            Exit Sub
        End If
        
        If userChoice = "2" Then
            ' 3.1.1. Append mode: ask user to input existing dataId
            dataId = InputBox("Please enter the existing dataId to append:", "Append Mode", "")
            dataId = Trim(dataId)
            If dataId = "" Then
                Debug.Print "ERROR: No dataId provided for append mode"
                Exit Sub
            End If
            Debug.Print "Using existing dataId for append: " & dataId
        Else
            ' 3.1.2. New mode: generate new dataId (default behavior)
            If Not GenerateDataId(dataId) Then
                Debug.Print "ERROR: Failed to generate data ID"
                Exit Sub
            End If
        End If
    Else
        ' 3.2. Folder upload: always generate new dataId
        If Not GenerateDataId(dataId) Then
            Debug.Print "ERROR: Failed to generate data ID"
            Exit Sub
        End If
    End If

    ' 4. Set credentials and initialize AWS SDK
    sdkInitResult = SetCredential(ENV_URL, LOGIN_ACCOUNT, LOGIN_ACCOUNT_PASSWORD)
    Dim jsonResponse As Object
    Set jsonResponse = JsonConverter.ParseJson(sdkInitResult)
    If jsonResponse("code") <> SDK_INIT_SUCCESS Then
        Debug.Print "ERROR: AWS SDK initialization failed - code: " & sdkInitResult
        Exit Sub
    End If

    Dim maxWaitTime As Long
    maxWaitTime = 600 ' Maximum wait time in seconds (10 minutes)

    If isFolder Then
        ' 5.1. Upload folder contents
        ' 5.1.1. Validate folder contents first
        Dim validationError As String
        If Not ValidateFolderContents(uploadFilePath, validationError) Then
            Debug.Print "ERROR: Folder validation failed - " & validationError
            MsgBox "ERROR: " & validationError, vbCritical, "Folder Validation Failed"
            Exit Sub
        End If
        
        Dim fso As Object
        Set fso = CreateObject("Scripting.FileSystemObject")
        ' Upload data name: abc.ds
        uploadDataName = fso.GetFolder(uploadFilePath).Name
        ' S3 file key: patient/patientId/source_data/dataId/abc.ds/
        s3FileKey = "patient/" & patientId & "/source_data/" & dataId & "/" & uploadDataName & "/"
        Dim uploadIds As String
        uploadSuccess = UploadFolderContents(uploadFilePath, totalFileSize, s3FileKey, dataId, patientId, REAL_TIME_SIGNAL_APPEND, uploadIds)

        ' 5.1.1. Monitor folder upload status
        If uploadSuccess Then
            Debug.Print
            Debug.Print "2. All folder uploads submitted (real-time), monitoring status..."
            Debug.Print
            uploadSuccess = MonitorMultipleUploadStatus(dataId, maxWaitTime)
        End If
    Else
        ' 5.2. Upload single file
        ' Upload data name: abc.ds
        uploadDataName = GetFileName(uploadFilePath)
        ' S3 file key: patient/patientId/source_data/dataId/abc.ds/abc.ds
        s3FileKey = "patient/" & patientId & "/source_data/" & dataId & "/" & uploadDataName & "/" & uploadDataName
        Dim singleUploadId As String
        uploadSuccess = UploadSingleFile(uploadFilePath, s3FileKey, dataId, patientId, REAL_TIME_SIGNAL_APPEND, singleUploadId)

        ' 5.2.1. Monitor single file upload status
        If uploadSuccess Then
            totalFileSize = GetLocalFileSize(uploadFilePath)
            Debug.Print "Single file upload started (real-time), monitoring status..."
            uploadSuccess = MonitorUploadStatus(dataId, maxWaitTime)
        End If
    End If

    ' 6. Upload process completed (confirmation and cleanup are handled automatically by C++ backend)
    If uploadSuccess Then
        Debug.Print "SUCCESS: Real-time upload completed and confirmed"
        MsgBox "SUCCESS: Real-time upload completed"
    Else
        Debug.Print "ERROR: Real-time upload process failed"
    End If
End Sub
