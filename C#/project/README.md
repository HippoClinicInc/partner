# C# S3 Upload Client

This is the C# version of the HippoClinic S3 upload client.

## Prerequisites

- .NET 9.0 SDK **(x64)** for building (project validated on net9; earlier versions require manual csproj edits)
- .NET 9.0 Runtime **or SDK (x86)** for running the app (the app is x86 because it loads a 32-bit C++ DLL)
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

## Configuration

Edit the constants in `BatchMain.cs` or `RealTimeFileAppendMain.cs`:

```csharp
const string HIPPO_ACCOUNT = "2546566177@qq.com";
const string HIPPO_PASSWORD = "u3LJ2lXv";
const string DEFAULT_MRN = "123";
const string DEFAULT_PATIENT_NAME = "Test api";
```

## Building the Project

> Compatibility: the solution works with `net6.0` and above, and has been fully validated with the `net9.0` SDK. If you use a different SDK version, edit the `TargetFramework` (or `TargetFrameworks`) element in both `BatchUpload.csproj` and `RealTimeUpload.csproj` so it matches your local installation.

### Environment Prep (one-time before running)

1. Install `.NET 9.0 SDK (x64)`: `winget install Microsoft.DotNet.SDK.9`
2. Install `.NET 9.0 Runtime (x86)` or `.NET 9.0 SDK (x86)`: `winget install Microsoft.DotNet.Runtime.9 --architecture x86`
3. Install **Microsoft Visual C++ Redistributable x86**: `winget install Microsoft.VCRedist.2015+.x86`
4. Clone/copy the repo to `D:\code\partner` and ensure `C#/project/lib/` still contains all DLLs
5. Open a fresh PowerShell and run `dotnet --info`, confirming an `SDK: 9.0.xxx (x64)` entry plus `RID: win-x86`

## Running the Programs

### Run Batch Upload Program

```powershell
cd D:/code/partner/C#/project
dotnet run --project BatchUpload.csproj -c Release
```

The program will:

1. Prompt for a file or folder path to upload
2. Create a patient record
3. Generate a data ID
4. Upload files using BATCH_CREATE mode
5. Monitor upload status until completion

### Run Real-Time Upload Program

```powershell
cd D:/code/partner/C#/project
dotnet run --project RealTimeUpload.csproj -c Release
```

The program will:

1. Prompt for a file path to upload (folders not supported)
2. Create a patient record
3. Ask to choose between new or append mode
4. Upload file using REAL_TIME_APPEND mode
5. Monitor upload status until completion

## Notes

- The project relies on a 32-bit (x86) C++ `S3UploadLib.dll`, so every .NET project/solution configuration must target `x86` to load the native library correctly.
- The DLL files must be in the `lib/` directory relative to the project root
- The program will automatically set the DLL search path
- All DLL dependencies (AWS SDK, etc.) must be present in the `lib/` directory
- The program uses the same C++ library (`S3UploadLib.dll`) as the VB6 version

## Troubleshooting

1. **DLL not found**: Ensure all DLL files are in the `lib/` directory
2. **Build errors**: Make sure you have .NET 9.0 SDK (x64) installed (or update the `TargetFramework` to match an installed SDK)
3. **Runtime errors**: Check that:
   - The DLL files match the architecture (x86)
   - .NET 9.0 Runtime or SDK (x86) is installed (the error message will usually say “Architecture: x86” if this is missing)
   - You can directly download the missing x86 runtime using the official link from the error message:  
     `https://aka.ms/dotnet-core-applaunch?missing_runtime=true&arch=x86&rid=win-x86&os=win10&apphost_version=9.0.11`

