Attribute VB_Name = "HippoBackend"
Option Explicit

' HippoBackend.bas - HippoClinic backend API functions
' This module contains functions that interact with the HippoClinic backend API

Public Const ENV_URL As String = "https://hippoclinic.com"
' You need to change to your account when testing this.
Public Const LOGIN_ACCOUNT As String = "2546566177@qq.com"
Public Const LOGIN_ACCOUNT_PASSWORD As String = "u3LJ2lXv"

' Module-level constant for default Medical Record Number (MRN)
' This hardcoded value is used for demonstration purposes only
Private Const DEFAULT_MRN As String = "123"
Private Const DEFAULT_PATIENT_NAME As String = "Test api"

' Module-level auth state (managed internally)
Private gJwtToken As String
Private gHospitalId As String

' Ensure we have a valid token and hospital id
Private Function EnsureLoggedIn() As Boolean
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
    Dim didRetry As Boolean

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

    ' Handle 401 Unauthorized -> refresh token once and retry
    If http.Status = 401 Then
        If Not didRetry Then
            didRetry = True
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
Public Function LoginAndGetToken(ByRef jwtToken As String, ByRef hospitalId As String) As Boolean
    Dim http As Object
    Dim url As String
    Dim requestBody As String
    Dim response As String

    ' 1. Initialize HTTP client
    Set http = CreateObject("WinHttp.WinHttpRequest.5.1")

    ' 2. Build login request
    url = ENV_URL & "/hippo/thirdParty/user/login"
    requestBody = "{""userMessage"":{""email"":""" & LOGIN_ACCOUNT & """},""password"":""" & LOGIN_ACCOUNT_PASSWORD & """}"

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

    ' 2. Build patient creation request
    url = ENV_URL & "/hippo/thirdParty/queryOrCreatePatient"
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

    ' 2. Build data ID generation request
    url = ENV_URL & "/hippo/thirdParty/file/generateUniqueKey" & "/1"

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