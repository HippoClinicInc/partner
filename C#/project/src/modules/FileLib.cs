using System;
using System.IO;

namespace HippoClinic.S3Upload
{
    /// <summary>
    /// FileLib.cs - System file operations used in demo
    /// This class contains only the file system functions actually used in the demo
    /// </summary>
    public static class FileLib
    {
        /// <summary>
        /// Check if file or folder exists
        /// </summary>
        public static bool FileOrFolderExists(string path)
        {
            try
            {
                if (Directory.Exists(path) || File.Exists(path))
                {
                    return true;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR: Path not found: {path} - {ex.Message}");
            }
            return false;
        }

        /// <summary>
        /// Check if path is a folder
        /// </summary>
        public static bool IsPathFolder(string path)
        {
            try
            {
                FileAttributes attr = File.GetAttributes(path);
                return (attr & FileAttributes.Directory) == FileAttributes.Directory;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR: Path check failed - {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Extract filename from full path
        /// </summary>
        public static string GetFileName(string filePath)
        {
            return Path.GetFileName(filePath);
        }

        /// <summary>
        /// Get file size in bytes
        /// </summary>
        public static long GetLocalFileSize(string filePath)
        {
            try
            {
                if (!File.Exists(filePath))
                {
                    Console.WriteLine($"ERROR: File does not exist: {filePath}");
                    return 0;
                }

                var fileInfo = new FileInfo(filePath);
                return fileInfo.Length;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR: Failed to get file size for {filePath} - {ex.Message}");
                return 0;
            }
        }

        /// <summary>
        /// Check if file exists
        /// </summary>
        public static bool FileExists(string filePath)
        {
            try
            {
                if (File.Exists(filePath))
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR: Failed to check file existence for {filePath} - {ex.Message}");
                return false;
            }
        }
    }
}

