Attribute VB_Name = "HippoBackend"
Option Explicit

' HippoBackend.bas - HippoClinic backend API functions
' This module contains functions that interact with the HippoClinic backend API
'
' Usage pattern
' 1. Call HippoBackend.Initialize(baseUrl, account, password) at application startup
' 2. Then call other functions like CreatePatient(), GenerateDataId(), etc.
'
' Example:
'   HippoBackend.Initialize "https://hippoclinic.com", "user@example.com", "password"
'   Dim patientId As String
'   If CreatePatient(patientId) Then
'       Debug.Print "Patient created: " & patientId
'   End If

' Module-level auth state 
Private gJwtToken As String
Private gHospitalId As String
Private gIsInitialized As Boolean

' Initialize HippoBackend with configuration parameters
' This function must be called before using any other HippoBackend functions
' Similar to HippoClient::Init() in C++
'
' Example:
'   HippoBackend.Initialize "https://hippoclinic.com", "user@example.com", "password"
Public Sub Initialize(ByVal baseUrl As String, ByVal account As String, ByVal password As String)
    gHippoBaseUrl = baseUrl
    gHippoAccount = account
    gHippoPassword = password
    gIsInitialized = True
    
    ' Clear any existing auth state
    gJwtToken = ""
    gHospitalId = ""
    
    Debug.Print "[HippoBackend] Initialized with account=" & account & ", baseUrl=" & baseUrl
End Sub

' Check if HippoBackend has been initialized
Private Function CheckInitialized() As Boolean
    If Not gIsInitialized Then
        Debug.Print "ERROR: HippoBackend not initialized. Call HippoBackend.Initialize() first."
        CheckInitialized = False
    Else
        CheckInitialized = True
    End If
End Function

' Ensure we have a valid token and hospital id
Private Function EnsureLoggedIn() As Boolean
    ' Check if module has been initialized
    If Not CheckInitialized() Then
        EnsureLoggedIn = False
        Exit Function
    End If
    
    If Len(gJwtToken) > 0 And Len(gHospitalId) > 0 Then
        EnsureLoggedIn = True
        Exit Function
    End If

    Dim token As String
    Dim hospital As String
    If LoginAndGetToken(token, hospital) Then
        gJwtToken = token
        gHospitalId = hospital
        EnsureLoggedIn = True
    Else
        EnsureLoggedIn = False
    End If
End Function

' Unified request with automatic token handling and 401 retry
Private Function RequestWithToken(ByVal method As String, ByVal url As String, ByVal body As String, ByRef responseText As String) As Boolean
    Dim http As Object
    Dim retryCount As Integer
    Const MAX_RETRIES As Integer = 3

    If Not EnsureLoggedIn() Then
        RequestWithToken = False
        Exit Function
    End If

RetryRequest:
    Set http = CreateObject("WinHttp.WinHttpRequest.5.1")
    On Error GoTo ErrorHandler
    http.Open method, url, False
    If Len(gJwtToken) > 0 Then http.SetRequestHeader "Authorization", "Bearer " & gJwtToken
    If method = "POST" Or method = "PUT" Then http.SetRequestHeader "Content-Type", "application/json"
    If method = "POST" Or method = "PUT" Then
        http.Send body
    Else
        http.Send
    End If

    responseText = http.ResponseText

    ' Handle 401 Unauthorized -> refresh token up to 3 times and retry
    If http.Status = 401 Then
        If retryCount < MAX_RETRIES Then
            retryCount = retryCount + 1
            gJwtToken = ""
            If EnsureLoggedIn() Then
                Set http = Nothing
                GoTo RetryRequest
            End If
        End If
        RequestWithToken = False
        Set http = Nothing
        Exit Function
    End If

    RequestWithToken = (http.Status = 200)
    Set http = Nothing
    Exit Function

ErrorHandler:
    RequestWithToken = False
    Set http = Nothing
End Function

' Expose hospital id for callers that need it
Public Function GetHospitalId() As String
    If EnsureLoggedIn() Then
        GetHospitalId = gHospitalId
    Else
        GetHospitalId = ""
    End If
End Function

' Authenticate user and retrieve JWT token and hospital ID
' Uses configuration from Common.bas (gHippoBaseUrl, gHippoAccount, gHippoPassword)
Public Function LoginAndGetToken(ByRef jwtToken As String, ByRef hospitalId As String) As Boolean
    Dim http As Object
    Dim url As String
    Dim requestBody As String
    Dim response As String

    ' 1. Initialize HTTP client
    Set http = CreateObject("WinHttp.WinHttpRequest.5.1")

    ' 2. Build login request using module configuration
    url = gHippoBaseUrl & "/hippo/thirdParty/user/login"
    requestBody = "{""userMessage"":{""email"":""" & gHippoAccount & """},""password"":""" & gHippoPassword & """}"

    On Error GoTo ErrorHandler

    ' 3. Send login request
    http.Open "POST", url, False
    http.SetRequestHeader "Content-Type", "application/json"
    http.Send requestBody

    ' 4. Process response
    response = http.ResponseText

    ' 5. Extract token and hospital ID from JSON response
    On Error GoTo JsonError
    Dim jsonResponse As Object
    Set jsonResponse = JsonConverter.ParseJson(response)

    jwtToken = jsonResponse("data")("jwtToken")
    hospitalId = jsonResponse("data")("userInfo")("hospitalId")
    On Error GoTo ErrorHandler

    ' 6. Validate extracted data
    LoginAndGetToken = (jwtToken <> "" And hospitalId <> "")
    Debug.Print "[HippoBackend] Login success, jwt_token obtained, hospital_id=" & hospitalId
    Set http = Nothing
    Exit Function

JsonError:
    ' 7. Handle JSON parsing errors
    Debug.Print "ERROR: JSON parsing failed"
    jwtToken = ""
    hospitalId = ""
    On Error GoTo ErrorHandler

ErrorHandler:
    ' 8. Handle general errors
    Debug.Print "ERROR: Login failed - " & Err.Description
    LoginAndGetToken = False
    Set http = Nothing
End Function

' Create patient record and return patient ID
' Uses default patient name and MRN from Common.bas constants
Public Function CreatePatient(ByRef patientId As String) As Boolean
    Dim http As Object
    Dim url As String
    Dim requestBody As String
    Dim response As String

    ' 1. Ensure auth and build request
    If Not EnsureLoggedIn() Then
        CreatePatient = False
        Exit Function
    End If

    ' 2. Build patient creation request using module configuration
    url = gHippoBaseUrl & "/hippo/thirdParty/queryOrCreatePatient"
    requestBody = "{""hospitalId"":""" & gHospitalId & """,""user"":{""name"":""" & DEFAULT_PATIENT_NAME & """,""roles"":[3],""hospitalId"":""" & gHospitalId & """,""mrn"":""" & DEFAULT_MRN & """}}"

    On Error GoTo ErrorHandler

    ' 3. Send patient creation request with auto token and retry
    If Not RequestWithToken("POST", url, requestBody, response) Then
        GoTo ErrorHandler
    End If

    ' 4. Process response
    ' response already filled

    ' 5. Extract patient ID from JSON response
    On Error GoTo JsonError
    Dim jsonResponse As Object
    Set jsonResponse = JsonConverter.ParseJson(response)

    patientId = jsonResponse("data")("patientId")
    On Error GoTo ErrorHandler

    ' 6. Validate patient ID
    CreatePatient = (patientId <> "")
    Set http = Nothing
    Exit Function

JsonError:
    ' 7. Handle JSON parsing errors
    Debug.Print "ERROR: JSON parsing failed - " & Err.Description
    patientId = ""
    On Error GoTo ErrorHandler

ErrorHandler:
    ' 8. Handle general errors
    Debug.Print "ERROR: Patient creation failed - " & Err.Description
    CreatePatient = False
    Set http = Nothing
End Function

' Generate unique data ID for file upload
Public Function GenerateDataId(ByRef dataId As String) As Boolean
    Dim http As Object
    Dim url As String
    Dim response As String

    ' 1. Ensure auth
    If Not EnsureLoggedIn() Then
        GenerateDataId = False
        Exit Function
    End If

    ' 2. Build data ID generation request using module configuration
    url = gHippoBaseUrl & "/hippo/thirdParty/file/generateUniqueKey" & "/1"

    On Error GoTo ErrorHandler

    ' 3. Send data ID generation request
    If Not RequestWithToken("GET", url, "", response) Then
        GoTo ErrorHandler
    End If

    ' 4. Process response
    ' response already filled

    ' 5. Extract first key from keys array
    If response <> "" Then
        On Error GoTo JsonError
        Dim jsonResponse As Object
        Set jsonResponse = JsonConverter.ParseJson(response)

        Dim keysArray As Object
        Set keysArray = jsonResponse("data")("keys")

        ' 6. Get first element from array
        dataId = keysArray(1)
        On Error GoTo ErrorHandler
    Else
        dataId = ""
    End If

    ' 7. Validate data ID
    GenerateDataId = (dataId <> "")
    Set http = Nothing
    Exit Function

JsonError:
    ' 8. Handle JSON parsing errors
    Debug.Print "ERROR: JSON parsing failed - " & Err.Description
    dataId = ""
    On Error GoTo ErrorHandler

ErrorHandler:
    ' 9. Handle general errors
    Debug.Print "ERROR: Data ID generation failed - " & Err.Description
    GenerateDataId = False
    Set http = Nothing
End Function

' Upload confirmation is now handled automatically by the C++ backend
' No manual confirmation needed in VB6 code