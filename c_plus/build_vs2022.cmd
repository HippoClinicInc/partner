@echo off
echo Building S3UploadLib.dll with Visual Studio 2022
echo.

REM Set VS 2022 environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"

echo Build environment setup complete
echo.

REM Create build directory if it doesn't exist
if not exist build mkdir build
echo Created build directory
echo.

REM Compile object files to build directory
echo Step 1: Compiling common source file
cl /std:c++14 /EHsc /MD /c /DS3UPLOAD_EXPORTS /I"aws-sdk-cpp\include" /I"vcpkg\installed\x86-windows\include" /Fo"build\S3Common.obj" src\common\S3Common.cpp

if %ERRORLEVEL% neq 0 (
    echo Compilation of S3Common.cpp failed!
    pause
    exit /b 1
)

echo Step 2: Compiling sync upload source file
cl /std:c++14 /EHsc /MD /c /DS3UPLOAD_EXPORTS /I"aws-sdk-cpp\include" /I"vcpkg\installed\x86-windows\include" /Fo"build\S3UploadSync.obj" src\uploadSync\S3UploadSync.cpp

if %ERRORLEVEL% neq 0 (
    echo Compilation of S3UploadSync.cpp failed!
    pause
    exit /b 1
)

echo Step 3: Compiling async upload source file
cl /std:c++14 /EHsc /MD /c /DS3UPLOAD_EXPORTS /I"aws-sdk-cpp\include" /I"vcpkg\installed\x86-windows\include" /Fo"build\S3UploadAsync.obj" src\uploadAsync\S3UploadAsync.cpp

if %ERRORLEVEL% neq 0 (
    echo Compilation of S3UploadAsync.cpp failed!
    pause
    exit /b 1
)

echo Step 4: Compiling HippoClient source file
cl /std:c++14 /EHsc /MD /c /DS3UPLOAD_EXPORTS /I"aws-sdk-cpp\include" /I"vcpkg\installed\x86-windows\include" /Fo"build\hippo_client.obj" src\common\request\hippo_client.cpp

if %ERRORLEVEL% neq 0 (
    echo Compilation of hippo_client.cpp failed!
    pause
    exit /b 1
)

echo Step 5: Compiling S3ClientManager source file
cl /std:c++14 /EHsc /MD /c /DS3UPLOAD_EXPORTS /I"aws-sdk-cpp\include" /I"vcpkg\installed\x86-windows\include" /Fo"build\s3_client_manager.obj" src\common\request\s3_client_manager.cpp

if %ERRORLEVEL% neq 0 (
    echo Compilation of s3_client_manager.cpp failed!
    pause
    exit /b 1
)

echo Step 6: Compiling main source file
cl /std:c++14 /EHsc /MD /c /DS3UPLOAD_EXPORTS /I"aws-sdk-cpp\include" /I"vcpkg\installed\x86-windows\include" /Fo"build\main.obj" src\main.cpp

if %ERRORLEVEL% neq 0 (
    echo Compilation of main.cpp failed!
    pause
    exit /b 1
)

echo.
echo Step 7: Linking to create DLL...
link /DLL /OUT:"build\S3UploadLib.dll" "build\S3Common.obj" "build\S3UploadSync.obj" "build\S3UploadAsync.obj" "build\hippo_client.obj" "build\s3_client_manager.obj" "build\main.obj" /LIBPATH:"aws-sdk-cpp\lib" /LIBPATH:"vcpkg\installed\x86-windows\lib" aws-cpp-sdk-core.lib aws-cpp-sdk-s3.lib aws-c-common.lib aws-c-auth.lib aws-c-cal.lib aws-c-compression.lib aws-c-event-stream.lib aws-c-http.lib aws-c-io.lib aws-c-mqtt.lib aws-c-s3.lib aws-c-sdkutils.lib aws-checksums.lib aws-crt-cpp.lib zlib.lib libcurl.lib kernel32.lib user32.lib advapi32.lib ws2_32.lib /DEF:S3UploadLib.def

if %ERRORLEVEL% neq 0 (
    echo Linking failed!
    pause
    exit /b 1
)

echo Step 8: Copying AWS SDK DLLs to build directory...
copy "aws-sdk-cpp\bin\*.dll" "build\" >nul 2>&1
copy "vcpkg\installed\x86-windows\bin\*.dll" "build\" >nul 2>&1
echo DLLs copied to build directory

echo.
echo Build successful! Generated files in build directory:
if exist "build\S3UploadLib.dll" echo build\S3UploadLib.dll - Build successful!
if exist "build\S3UploadLib.lib" echo build\S3UploadLib.lib - Import library generated!
if exist "build\S3UploadLib.exp" echo build\S3UploadLib.exp - Export file generated!

echo.
echo Build directory contents:
dir build\*.dll
dir build\*.lib
dir build\*.exp

echo.
echo Next steps:
echo 1. Copy all files from build\ directory to your VB6 program directory
echo 2. Use S3UploadLib.bas declarations in your VB6 project
echo.
pause
