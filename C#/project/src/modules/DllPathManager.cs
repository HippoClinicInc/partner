using System;
using System.IO;
using System.Runtime.InteropServices;

namespace HippoClinic.S3Upload
{
/// <summary>
/// DLL Path Management Module
/// Used to validate DLL file paths and existence, ensuring DLL files can be found correctly
/// </summary>
public static class DllPathManager
{
    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    private static extern bool SetDllDirectory(string lpPathName);

    /// <summary>
    /// Get project root directory path
    /// </summary>
    private static string GetProjectRootPath()
    {
        string appPath = AppDomain.CurrentDomain.BaseDirectory;

        // If currently in src\modules directory, go back to project root
        if (appPath.EndsWith("src\\modules\\", StringComparison.OrdinalIgnoreCase) ||
            appPath.EndsWith("src/modules/", StringComparison.OrdinalIgnoreCase))
        {
            appPath = appPath.Substring(0, appPath.Length - 12);
        }

        // Ensure path ends with directory separator
        if (!appPath.EndsWith(Path.DirectorySeparatorChar.ToString()) &&
            !appPath.EndsWith(Path.AltDirectorySeparatorChar.ToString()))
        {
            appPath += Path.DirectorySeparatorChar;
        }

        return appPath;
    }

    /// <summary>
    /// Check if DLL file exists
    /// </summary>
    private static bool IsDllExists(string dllPath)
    {
        try
        {
            return File.Exists(dllPath);
        }
        catch
        {
            return false;
        }
    }

    /// <summary>
    /// Set DLL search path and validate DLL files exist
    /// </summary>
    public static bool SetDllSearchPath()
    {
        try
        {
            // Get lib directory path
            string libPath = Path.Combine(GetProjectRootPath(), "lib");

            // Check if lib directory exists and contains S3UploadLib.dll
            string dllPath = Path.Combine(libPath, "S3UploadLib.dll");
            if (!IsDllExists(dllPath))
            {
                Console.WriteLine($"ERROR: lib directory not found or S3UploadLib.dll missing: {libPath}");
                return false;
            }

            // Set DLL search path
            if (SetDllDirectory(libPath))
            {
                Console.WriteLine($"SUCCESS: DLL search path set to: {libPath}");
                Console.WriteLine("SUCCESS: S3UploadLib.dll found and validated");
                return true;
            }
            else
            {
                Console.WriteLine($"ERROR: Failed to set DLL search path to: {libPath}");
                return false;
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"ERROR: SetDllSearchPath failed - {ex.Message}");
            return false;
        }
    }
}
}
