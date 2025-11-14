# HippoClinic File Upload VB6 Project

## Project Structure

```
project/
├── src/                    # Source code directory
│   └── modules/           # Module files
│       ├── BatchMain.bas             # Batch upload main program logic
│       ├── RealTimeFileAppendMain.bas # Real-time file append upload main program
│       ├── Common.bas                # Common utilities and shared functions
│       ├── JsonConverter.bas         # JSON processing
│       ├── S3UploadLib.bas           # S3 upload library declarations
│       ├── HippoBackend.bas          # Backend API calls
│       ├── FileLib.bas               # File operation utilities
│       └── DllPathManager.bas        # DLL path management
├── lib/                   # Library files directory
│   ├── *.dll             # AWS SDK DLL files
│   └── S3UploadLib.dll   # Custom S3 upload library
├── Project1.vbp         # VB6 project file (Batch upload scenario - BatchMain.bas)
├── Project2.vbp         # VB6 project file (Real-time append scenario - RealTimeFileAppendMain.bas)
└── README.md            # Project documentation
```

## 📁 Module Description

### Core VB6 Modules

#### `BatchMain.bas`
**Batch Upload Main Module** - Contains the complete batch file upload workflow:
- **Main()** - Primary workflow function with 11-step process
- **StartUpload()** - GUI-based upload function with progress display

**Key Functions:**
- `Main()` - Primary workflow entry point
- `StartUpload()` - GUI-based upload with progress tracking
- `UploadSingleFile()` - Upload individual files to S3
- `UploadFolderContents()` - Batch upload entire folders

#### `RealTimeFileAppendMain.bas`
**Real-Time File Append Upload Module** - Handles real-time incremental append upload workflow:
- **Single File Upload** - Only supports uploading individual files (folders are not supported)
- **New or Append Mode** - Choose to create a new dataId or append to an existing dataId
- **Immediate Confirmation** - Each file upload is immediately confirmed after completion

**Key Functions:**
- `Main()` - Primary workflow entry point for real-time append upload
- Supports two modes: create new dataId or append to existing dataId
- Only single file uploads (folder upload is not supported for this mode)

#### `Common.bas`
**Common Utilities Module** - Contains shared functions and utilities used across modules:
- **S3 Configuration** - Centralized S3 constants shared across upload flows
- **Module-Level Variables** - Runtime configuration storage for application state
- **Utility Functions** - Common functions used by multiple modules (file upload, status monitoring)
- **Enums and Constants** - Shared enumerations and constants

**Key Features:**
- S3 configuration constants (S3_BUCKET, S3_REGION)
- Shared upload helpers (UploadSingleFile, MonitorUploadStatus)
- Cross-module shared logic and utilities
- File upload and monitoring functions (UploadSingleFile, MonitorUploadStatus)
- Cross-module shared logic and utilities

#### `HippoBackend.bas`
**HippoClinic API Integration** - Backend API communication module:
- **Initialization** - Configuration management with centralized parameter control
- **Authentication** - Login and JWT token management with automatic retry
- **Patient Management** - Patient record creation and management
- **API Communication** - HTTP requests to HippoClinic API endpoints
- **Credential Management** - Unified configuration for both API and S3

**Key Functions:**
- `Initialize(baseUrl, account, password)` - Initialize backend with custom configuration
- `LoginAndGetToken()` - Authenticate with HippoClinic API
- `CreatePatient()` - Create patient records
- `GenerateDataId()` - Generate unique data identifiers
- `GetHospitalId()` - Retrieve current hospital ID

**Design Pattern:**
Uses module-level variables for configuration state, requiring initialization before use.

#### `FileLib.bas`
**File System Operations** - File and folder management utilities:
- **File Validation** - File and folder existence checking
- **Path Processing** - File path validation and processing
- **System Integration** - Windows file system operations

**Key Functions:**
- `FileOrFolderExists()` - Check if file or folder exists
- `GetLocalFileSize()` - Get file size in bytes
- `IsPathFolder()` - Check if path is a directory

#### `S3UploadLib.bas`
**AWS S3 Library Interface** - VB6 declarations for S3 operations:
- **API Declarations** - Function prototypes for S3UploadLib.dll
- **Error Constants** - Standardized error codes and messages
- **Helper Functions** - Error message translation utilities

**Key Features:**
- Support for both permanent and temporary AWS credentials
- File existence and size validation
- Comprehensive error code system
- Asynchronous upload methods with status monitoring

#### `JsonConverter.bas`
**JSON Processing Library** - VBA-JSON v2.3.1 implementation:
- **JSON Parsing** - Convert JSON strings to VBA objects
- **JSON Generation** - Convert VBA objects to JSON strings
- **Error Handling** - Robust JSON parsing error management
- **Cross-Platform** - Support for both Windows and Mac VBA

#### `DllPathManager.bas`
**DLL Path Management Module** - Handles DLL loading and path resolution:
- **Dynamic DLL Path Resolution** - Automatic project root directory detection
- **DLL Loading Management** - Use SetDllDirectory API to set search path
- **Path Validation** - Validate DLL file existence
- **Error Handling** - Provide detailed error information

### AWS SDK Libraries

#### Core AWS C++ Libraries
- `aws-cpp-sdk-core.dll` (1.4MB) - AWS SDK core functionality
- `aws-cpp-sdk-s3.dll` (2.3MB) - S3-specific operations
- `aws-crt-cpp.dll` (424KB) - AWS Common Runtime C++

#### AWS C Libraries
- `aws-c-s3.dll` (170KB) - S3 C library
- `aws-c-auth.dll` (169KB) - Authentication library
- `aws-c-http.dll` (284KB) - HTTP client library
- `aws-c-io.dll` (248KB) - I/O operations library
- `aws-c-mqtt.dll` (272KB) - MQTT client library
- `aws-c-common.dll` (175KB) - Common utilities
- `aws-c-sdkutils.dll` (76KB) - SDK utilities
- `aws-c-event-stream.dll` (70KB) - Event streaming
- `aws-c-cal.dll` (48KB) - Cryptographic library
- `aws-checksums.dll` (77KB) - Checksum utilities
- `aws-c-compression.dll` (13KB) - Compression library

#### System Dependencies
- `zlib1.dll` (77KB) - Compression library
- `S3UploadLib.dll` (87KB) - Custom S3 wrapper library

## 📦 Project Files

This project includes two separate VB6 project files for different upload scenarios:

### `Project1.vbp` - Batch Upload Scenario
- **Main Module**: BatchMain.bas
- **Purpose**: Traditional batch file and folder upload
- **Use Case**: Upload single files or entire folders at once
- **Features**: Complete upload workflow with GUI progress tracking

### `Project2.vbp` - Real-Time File Append Scenario
- **Main Module**: RealTimeFileAppendMain.bas
- **Purpose**: Real-time incremental file append upload
- **Use Case**: Upload single files with option to create new or append to existing dataId
- **Features**: Single file upload only (folder upload not supported), immediate confirmation after upload

> **Note**: Both projects share the same common modules (Common.bas, HippoBackend.bas, FileLib.bas, S3UploadLib.bas, JsonConverter.bas, DllPathManager.bas) but have different entry points.

## 🚀 Quick Start

### Prerequisites
**VB6 Environment** - Visual Basic 6.0 or compatible IDE
> **Where to get Visual Basic 6.0?**  
> Visual Basic 6.0 is legacy software and is no longer officially distributed by Microsoft. For educational or legacy maintenance purposes, you can find installation media and instructions at [WinWorldPC - Visual Basic 6.0](https://winworldpc.com/product/microsoft-visual-bas/60).

**Visual C++ Redistributable Runtimes 32-bit**
If you having issue with the DLL files, install the Visual C++ Redistributable Runtimes 32-bit maybe helpful.
- [Visual C++ Redistributable Runtimes](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)

You can find the installer in the `cpp_runtime` folder. click the `install_all.bat` to install all the runtimes.

#### Visual Basic 6.0 Installation Steps

1. **Download the Installer**  
   Go to [WinWorldPC - Visual Basic 6.0](https://winworldpc.com/product/microsoft-visual-bas/60) and download the ISO file for the desired edition (e.g., Enterprise Edition).

2. **Mount or Extract the ISO**  
   - Use a tool like 7-Zip/WinRAR to extract the files.
   - On Windows 10/11, right-click the ISO and select "Mount".
3. **Run the Setup**  
   - Open the mounted/extracted folder and run `SETUP.EXE` as Administrator.
   - If you encounter compatibility issues, right-click `SETUP.EXE` → Properties → Compatibility, and set to "Windows XP (Service Pack 3)" or "Windows 98/Me".

4. **Follow the Installation Wizard**  
   - Enter the product key
   > **Note:**  
   > After extracting the ISO, you will find a file named `Visual Basic 6.0 Enterprise Edition - serial.txt` in the extracted folder.  
   > This file contains the product key (serial number) required during installation.  
   > Open this file with a text editor and copy the product key when prompted by the setup wizard.
   - Choose "Typical" or "Custom" install (default options are fine).
   - Complete the installation.

5. **First Launch**  
   - Start Visual Basic 6.0 from the Start Menu.
   - If prompted, allow any additional setup or registration steps.

### Setup Instructions

1. **Open Project with Visual Basic 6.0**
   
   Choose the appropriate project file based on your use case:
   
   **For Batch Upload:**
   ```
   Open Project1.vbp with Visual Basic 6.0
   ```
   
   **For Real-Time File Append Upload:**
   ```
   Open Project2.vbp with Visual Basic 6.0
   ```

2. **Verify All Modules Are Loaded**
   Check that the following modules appear in the Project Explorer:
   ```
   Project Explorer should show:
   - BatchMain.bas
   - RealTimeFileAppendMain.bas
   - Common.bas
   - HippoBackend.bas
   - FileLib.bas
   - S3UploadLib.bas  
   - JsonConverter.bas
   - DllPathManager.bas
   ```

   **If modules are missing:**
   ```
   Project → Add File → Module → Existing
   Browse to src/modules/ directory
   Add each missing .bas file individually
   ```

3. **Add Required References**
   ```
   Project → References → Add:
   - Microsoft WinHTTP Services 5.1
   - Microsoft Scripting Runtime
   ```

4. **Verify DLL Files Location**
   ```
   Ensure all .dll files are in:
   - lib/ directory (recommended), OR
   - System PATH directory
   ```

5. **Configure Constants**
```
- Edit Common.bas for shared S3 settings: S3_BUCKET, S3_REGION
- Edit the entry module you plan to run (BatchMain.bas or RealTimeFileAppendMain.bas) and update the HIPPO_* and patient constants declared at the top of Sub Main.
```

6. **Run Application**
   ```
   Press F5 or Run → Start
   ```

## 🔧 Configuration

### S3 Configuration (Common.bas)

Shared S3 configuration constants remain centralized in `Common.bas`:

```vb
' S3 configuration constants
Public Const S3_BUCKET As String = "hippoclinic-staging"
Public Const S3_REGION As String = "us-west-1"
```

### HippoClinic Environment Configuration

`HIPPO_BASE_URL` is centralized in `Common.bas`. Credentials can be configured in the entry module you plan to run (e.g., at the top of `Sub Main` in `BatchMain.bas`).

```vb
' Example from BatchMain.bas (replace with your own credentials)
Const HIPPO_ACCOUNT As String = "your-email@example.com"
Const HIPPO_PASSWORD As String = "your-password"

Const DEFAULT_MRN As String = "123"
Const DEFAULT_PATIENT_NAME As String = "Test api"
```

Update the above values (account, password, and patient info) in the entry module you plan to run. Do not redefine `HIPPO_BASE_URL` in entry modules.

### Initialization

The application automatically initializes HippoBackend with the constants defined in `Common.bas`:

```vb
' In BatchMain.bas or RealTimeFileAppendMain.bas
HippoBackend.Initialize HIPPO_BASE_URL, HIPPO_ACCOUNT, HIPPO_PASSWORD
```

This initialization uses the per-module constants you configured and provides:
1. HippoClinic API authentication (login, create patient, generate dataId)
2. S3 credentials retrieval via SetCredential()

## 📋 Usage

### Batch Upload Mode (BatchMain.bas)

#### Single File Upload
1. Run the application
2. Enter file path when prompted
3. System will:
   - Validate file existence
   - Authenticate with API
   - Create patient record
   - Get S3 credentials
   - Upload file to S3
   - Confirm upload with API

#### Folder Upload (BatchMain.bas only)
> **Note**: Folder upload is only supported in `BatchMain.bas` (BATCH_CREATE mode). `RealTimeFileAppendMain.bas` does not support folder uploads.

1. Run the application
2. Enter folder path when prompted
3. System will:
   - Process all files in folder
   - Upload each file individually
   - Provide batch upload summary
   - Confirm entire batch with API

### Real-Time File Append Mode (RealTimeFileAppendMain.bas)

#### Single File Upload (New or Append)
> **Note**: This mode only supports single file uploads. Folder uploads are not supported.

1. Run the real-time upload function
2. Enter the file path to upload
3. If the path is a folder, an error will be shown
4. Choose upload mode:
   - **Mode 1 (New)**: Create a new dataId for this file
   - **Mode 2 (Append)**: Append to an existing dataId (you'll be prompted to enter the dataId)
5. System will:
   - Upload the file to S3
   - Immediately confirm the upload with the backend API
   - Display upload status and results

## 🐛 Debugging

### Debug Output
All operations are logged to the VB6 Immediate Window:
- `ERROR:` - Error conditions
- `SUCCESS:` - Successful operations
- `WARNING:` - Warning conditions
- `SUMMARY:` - Batch operation summaries

### Error Codes
S3UploadLib provides standardized error codes:
- `0` - Success
- `-1` - Invalid parameters
- `-2` - AWS SDK not initialized
- `-3` - File does not exist
- `-4` - File read error
- `-5` - File open error
- `-6` - S3 upload failed
- `-7` - Exception occurred
- `-8` - Unknown error

## 🔒 Security Features

- **Temporary Credentials** - Uses AWS STS for secure S3 access
- **JWT Authentication** - Secure API authentication
- **Error Handling** - No sensitive data in error messages
- **Input Validation** - Comprehensive file and path validation

## 📊 Supported File Types

- **Any file type** - No restrictions on file extensions
- **Large files** - Supports files of any size
- **Batch uploads** - Multiple files in single operation
- **Mixed content** - Files and folders can be mixed

## 🛠️ Troubleshooting

### Common Issues

1. **DLL Not Found**
   - Ensure all .dll files are in the lib/ directory
   - Ensure C++ runtimes are installed
   - Do not create a new project in VB6, open Project1.vbp directly
   - Check Windows PATH environment variable as fallback

   **How to Add DLL Directory to Windows PATH(Not required if you meet the first three conditions above)**
   
   **Method 1: Using System Properties (GUI)**
   1. Press `Windows + R` to open Run dialog
   2. Type `sysdm.cpl` and press Enter
   3. Click "Advanced" → "Environment Variables..." button
   4. In "System variables" section, find and select "Path"
   5. Click "Edit..." → "New" → Add your DLL directory path
   6. Click "OK" to save changes
   
   **Method 2: Using Command Prompt**
   ```cmd
   # Check current PATH
   echo %PATH%
   
   # Add directory temporarily
   set PATH=%PATH%;C:\path\to\your\dll\files
   
   # Add directory permanently
   setx PATH "%PATH%;C:\path\to\your\dll\files"
   ```
   
   **Method 3: Using PowerShell**
   ```powershell
   # Check current PATH
   $env:PATH
   
   # Add directory temporarily
   $env:PATH += ";C:\path\to\your\dll\files"
   
   # Add directory permanently
   [Environment]::SetEnvironmentVariable("PATH", $env:PATH + ";C:\path\to\your\dll\files", "Machine")
   ```
   
   **Verification:**
   - Open new Command Prompt
   - Run `echo %PATH%` to confirm your DLL directory is listed
   - Test your VB6 application

2. **Authentication Failed**
   - Verify login credentials in HippoBackend.bas
   - Check network connectivity
   - Confirm API endpoint URL

3. **Upload Failed**
   - Check AWS S3 credentials
   - Verify S3 bucket permissions
   - Ensure file is not locked by another process

4. **JSON Parsing Errors**
   - Verify JsonConverter.bas is properly added
   - Check API response format

5. **Interface Freezing**
   - The application now includes DoEvents calls to prevent UI freezing
   - Use the Cancel button to stop long-running operations
   - Monitor progress in the output window

### Debug Steps
1. Enable VB6 Immediate Window (View → Immediate Window)
2. Run application and monitor debug output
3. Check error codes and messages
4. Verify file paths and permissions

## DLL Path Management

The project uses the `DllPathManager.bas` module to manage DLL file path issues:

### Problem Solution
- **Problem**: VB6 may not be able to correctly find DLL files in subdirectory structures
- **Solution**: Use Windows API to set DLL search path
- **Features**: 
  - Automatic project root directory detection
  - Use SetDllDirectory API to set search path
  - Validate DLL file existence
  - Provide detailed error information
  - DLL files remain in lib directory, no copying required

### DLL File Management
1. **DLL File Location**: All DLL files are stored in the `lib/` directory
2. **Search Path Setting**: Automatically set DLL search path when program starts
3. **No Copying Required**: DLL files remain in lib directory, maintaining clean project structure

```
lib/                    # DLL file directory (only location)
├── S3UploadLib.dll      # Custom S3 upload library
├── aws-c-*.dll         # AWS C SDK library
├── aws-cpp-sdk-*.dll   # AWS C++ SDK library
└── zlib1.dll           # Compression library
```

### Usage Instructions
1. Ensure all DLL files are in the `lib/` directory
2. Open and run the project in VB6
3. The program will automatically set DLL search path and validate file existence

## Cross-Computer Project Loading Issues

### Problem Description
When opening the project on a different computer, you may encounter the following issues:

1. **Missing Modules**: VB6 may only show 3 modules instead of all 6 modules
2. **Path Errors**: "The file could not be loaded" errors
3. **Invalid Key Errors**: "contains invalid key" errors in project file

### Root Causes
- **Path Differences**: Project file uses relative paths that may not match the new computer's directory structure
- **File Missing**: Some `.bas` files may not have been copied to the new computer
- **VB6 Cache**: VB6 may have cached incorrect path information

### Solutions

#### Method 1: Verify File Structure
Ensure the following directory structure exists on the new computer:
```
src/
└── modules/
    ├── BatchMain.bas
    ├── RealTimeFileAppendMain.bas
    ├── Common.bas
    ├── JsonConverter.bas
    ├── S3UploadLib.bas
    ├── HippoBackend.bas
    ├── FileLib.bas
    └── DllPathManager.bas
```

#### Method 2: Manual Module Addition
If files exist but VB6 doesn't load them:
1. Open VB6 and load the project
2. Right-click project → `Add` → `Module`
3. Select `Existing` tab
4. Browse to `src\modules\` directory
5. Add each missing `.bas` file individually

#### Method 3: Clean Project Loading
1. Close VB6 completely
2. Delete the `.vbw` file (VB6 workspace file)
3. Reopen VB6 and load `Project1.vbp`
4. VB6 will recreate the workspace file with correct paths

#### Method 4: Verify Correct Project File
Make sure you're opening the correct project file for your use case:
- `Project1.vbp` - For batch upload scenarios (BatchMain.bas)
- `Project2.vbp` - For real-time append scenarios (RealTimeFileAppendMain.bas)

### Prevention Tips
1. **Complete File Copy**: Always copy the entire project directory including all subdirectories
2. **Path Consistency**: Maintain the same directory structure on different computers
3. **Version Control**: Use version control systems to ensure file integrity across different environments

## 📝 API Endpoints

The system integrates with these HippoClinic API endpoints:
- `POST /hippo/thirdParty/user/login` - User authentication
- `POST /hippo/thirdParty/queryOrCreatePatient` - Patient management
- `POST /hippo/thirdParty/file/getS3Credentials` - S3 credentials
- `GET /hippo/thirdParty/file/generateUniqueKey` - Data ID generation
- `POST /hippo/thirdParty/file/confirmUploadRawFile` - Upload confirmation

## 📄 License

- **JsonConverter.bas** - MIT License (VBA-JSON v2.3.1)
- **S3UploadLib** - Custom implementation
- **AWS SDK** - Apache License 2.0
- **Demo Application** - Internal use only

## 🤝 Support

For technical support or questions:
1. Check debug output in VB6 Immediate Window
2. Review error codes and messages
3. Verify configuration settings
4. Contact development team with specific error details

## Important Notes

1. **DLL Files**: Ensure all DLL files are in lib directory and accessible
2. **Configuration**: Update S3 constants in `Common.bas`, and adjust `HIPPO_*` / patient constants inside the entry module (`BatchMain.bas`, `RealTimeFileAppendMain.bas`, etc.) before running
3. **Initialization Required**: The application automatically calls `HippoBackend.Initialize()` at startup using constants from `Common.bas`
4. **Upload Support**: Project supports single file and folder uploads (folder upload only in BatchMain.bas)
5. **Error Handling**: Upload process includes progress monitoring and error handling
6. **Path Issues**: If encountering DLL loading problems, check if lib directory exists and contains all required DLL files
7. **Cross-Computer Issues**: Follow the troubleshooting guide above when opening project on different computers
8. **UI Responsiveness**: The application now includes proper DoEvents calls to prevent interface freezing
9. **Cancel Functionality**: Users can cancel long-running upload operations using the Cancel button

