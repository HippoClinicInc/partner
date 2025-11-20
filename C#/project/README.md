# C# S3 Upload Client

This is the C# version of the HippoClinic S3 upload client.

## Prerequisites

- .NET 9.0 SDK (project validated on net9; earlier versions require manual csproj edits)
- Windows OS (for DLL interop)
- All required DLL files in the `lib/` directory

## Project Structure

```
C#/project/
├── lib/                    # DLL files (S3UploadLib.dll and dependencies)
├── src/modules/
│   ├── BatchMain.cs                    # Batch upload main program
│   ├── RealTimeFileAppendMain.cs       # Real-time append upload main program
│   ├── Common.cs                       # Shared constants and functions
│   ├── S3UploadLib.cs                  # C++ DLL P/Invoke declarations
│   ├── HippoBackend.cs                 # HippoClinic API functions
│   ├── FileLib.cs                      # File system operations
│   └── DllPathManager.cs               # DLL path management
├── BatchUpload.csproj                   # Project file for batch upload
└── RealTimeUpload.csproj                # Project file for real-time upload
```

## Building the Project

> Compatibility: the solution works with `net6.0` and above, and has been fully validated with the `net9.0` SDK. If you use a different SDK version, edit the `TargetFramework` (or `TargetFrameworks`) element in both `BatchUpload.csproj` and `RealTimeUpload.csproj` so it matches your local installation.

### Option 1: Build Batch Upload Program

```bash
cd C#/project
dotnet build BatchUpload.csproj
```

### Option 2: Build Real-Time Upload Program

```bash
cd C#/project
dotnet build RealTimeUpload.csproj
```

## Running the Programs

### Run Batch Upload Program

```bash
# Build and run
dotnet run --project BatchUpload.csproj
```

The program will:

1. Prompt for a file or folder path to upload
2. Create a patient record
3. Generate a data ID
4. Upload files using BATCH_CREATE mode
5. Monitor upload status until completion

### Run Real-Time Upload Program

```bash
# Build and run
dotnet run --project RealTimeUpload.csproj
```

The program will:

1. Prompt for a file path to upload (folders not supported)
2. Create a patient record
3. Ask to choose between new or append mode
4. Upload file using REAL_TIME_APPEND mode
5. Monitor upload status until completion

## Configuration

Edit the constants in `BatchMain.cs` or `RealTimeFileAppendMain.cs`:

```csharp
const string HIPPO_ACCOUNT = "2546566177@qq.com";
const string HIPPO_PASSWORD = "u3LJ2lXv";
const string DEFAULT_MRN = "123";
const string DEFAULT_PATIENT_NAME = "Test api";
```

## Notes

- The project relies on a 32-bit (x86) C++ `S3UploadLib.dll`, so every .NET project/solution configuration must target `x86` to load the native library correctly.
- The DLL files must be in the `lib/` directory relative to the project root
- The program will automatically set the DLL search path
- All DLL dependencies (AWS SDK, etc.) must be present in the `lib/` directory
- The program uses the same C++ library (`S3UploadLib.dll`) as the VB6 version

## Troubleshooting

1. **DLL not found**: Ensure all DLL files are in the `lib/` directory
2. **Build errors**: Make sure you have .NET 8.0 SDK installed
3. **Runtime errors**: Check that the DLL files match the architecture (x86)

## Set Up on a Fresh Windows PC (with .NET 9)

1. Install prerequisites  
   - Download **.NET SDK 9.0 (x64)** from Microsoft or run `winget install Microsoft.DotNet.SDK.9`.  
   - After installation, verify with `dotnet --list-sdks` and make sure a `9.0.xxx` entry appears.
   - **Install Microsoft Visual C++ Redistributable (x86)**: Since the C++ DLL (`S3UploadLib.dll`) is compiled with `/MD` flag (dynamic runtime linking), you need to install the **x86 (32-bit) version** of Visual C++ Redistributable. Download from [Microsoft's official page](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) or run `winget install Microsoft.VCRedist.2015+.x86`. This is required for the C++ DLL to run properly.
2. Get the project  
   - Clone or copy this repo to the machine, e.g., `D:\code\partner`.
3. Restore the native DLLs  
   - The repo already ships with `C#/project/lib/` containing the 32-bit `S3UploadLib.dll` and dependencies, so normally no action is required unless you removed or replaced them.
4. Build  
   ```powershell
   cd D:\code\partner\C#\project
   dotnet build RealTimeUpload.csproj -c Release
   ```
   - To build the batch variant, switch to `BatchUpload.csproj`.
5. Run  
   ```powershell
   dotnet run --project RealTimeUpload.csproj -c Release
   ```
   - Follow the prompts to enter account and patient data, then confirm uploads succeed.
