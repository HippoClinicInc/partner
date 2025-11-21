using System;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace HippoClinic.S3Upload
{
/// <summary>
/// HippoBackend.cs - HippoClinic backend API functions
/// This class contains functions that interact with the HippoClinic backend API
///
/// Usage pattern
/// 1. Call HippoBackend.Initialize(baseUrl, account, password) at application startup
/// 2. Then call other functions like CreatePatientAsync(), GenerateDataIdAsync(), etc.
///
/// Example:
///   HippoBackend.Initialize("https://hippoclinic.com", "user@example.com", "password");
///   string patientId;
///   var (success, patientId) = await HippoBackend.CreatePatientAsync("123", "Test Patient");
///   if (success)
///   {
///       Console.WriteLine("Patient created: " + patientId);
///   }
/// </summary>
public static class HippoBackend
{
    // Module-level auth state
    private static string _jwtToken = string.Empty;
    private static string _hospitalId = string.Empty;
    private static bool _isInitialized;
    private static string _hippoBaseUrl = string.Empty;
    private static string _hippoAccount = string.Empty;
    private static string _hippoPassword = string.Empty;
    private static readonly HttpClient HttpClient = new HttpClient();

    /// <summary>
    /// Initialize HippoBackend with configuration parameters
    /// This function must be called before using any other HippoBackend functions
    ///
    /// Example:
    ///   HippoBackend.Initialize("https://hippoclinic.com", "user@example.com", "password");
    /// </summary>
    public static void Initialize(string baseUrl, string account, string password)
    {
        _hippoBaseUrl = baseUrl;
        _hippoAccount = account;
        _hippoPassword = password;
        _isInitialized = true;

        // Clear any existing auth state
        _jwtToken = string.Empty;
        _hospitalId = string.Empty;

        Console.WriteLine($"[HippoBackend] Initialized with account={account}, baseUrl={baseUrl}");
    }

    /// <summary>
    /// Check if HippoBackend has been initialized
    /// </summary>
    private static bool CheckInitialized()
    {
        if (!_isInitialized)
        {
            Console.WriteLine("ERROR: HippoBackend not initialized. Call HippoBackend.Initialize() first.");
            return false;
        }
        return true;
    }

    /// <summary>
    /// Ensure we have a valid token and hospital id
    /// </summary>
    private static async Task<bool> EnsureLoggedInAsync()
    {
        // Check if module has been initialized
        if (!CheckInitialized())
        {
            return false;
        }

        if (!string.IsNullOrEmpty(_jwtToken) && !string.IsNullOrEmpty(_hospitalId))
        {
            return true;
        }

        var (success, token, hospital) = await LoginAndGetTokenAsync();
        if (success)
        {
            _jwtToken = token;
            _hospitalId = hospital;
            return true;
        }
        else
        {
            return false;
        }
    }

    /// <summary>
    /// Unified request with automatic token handling and 401 retry
    /// </summary>
    private static async Task<(bool success, string responseText)> RequestWithTokenAsync(string method, string url,
                                                                                         string body)
    {
        const int MAX_RETRIES = 3;
        int retryCount = 0;

        if (!await EnsureLoggedInAsync())
        {
            return (false, string.Empty);
        }

        while (retryCount < MAX_RETRIES)
        {
            try
            {
                var request = new HttpRequestMessage(new HttpMethod(method), url);
                if (!string.IsNullOrEmpty(_jwtToken))
                {
                    request.Headers.Authorization =
                        new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", _jwtToken);
                }

                if (method == "POST" || method == "PUT")
                {
                    request.Content = new StringContent(body, Encoding.UTF8, "application/json");
                }

                var response = await HttpClient.SendAsync(request);
                string responseText = await response.Content.ReadAsStringAsync();

                // Handle 401 Unauthorized -> refresh token and retry
                if (response.StatusCode == System.Net.HttpStatusCode.Unauthorized)
                {
                    retryCount++;
                    _jwtToken = string.Empty;
                    if (await EnsureLoggedInAsync())
                    {
                        continue;
                    }
                }

                return (response.IsSuccessStatusCode, responseText);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR: Request failed - {ex.Message}");
                return (false, string.Empty);
            }
        }

        return (false, string.Empty);
    }

    /// <summary>
    /// Expose hospital id for callers that need it
    /// </summary>
    public static async Task<string> GetHospitalIdAsync()
    {
        return await EnsureLoggedInAsync().ConfigureAwait(false) ? _hospitalId : string.Empty;
    }

    /// <summary>
    /// Authenticate user and retrieve JWT token and hospital ID
    /// </summary>
    public static async Task<(bool success, string jwtToken, string hospitalId)> LoginAndGetTokenAsync()
    {
        string jwtToken = string.Empty;
        string hospitalId = string.Empty;

        try
        {
            // Build login request using module configuration
            string url = $"{_hippoBaseUrl}/hippo/thirdParty/user/login";
            string requestBody =
                $"{{\"userMessage\":{{\"email\":\"{_hippoAccount}\"}},\"password\":\"{_hippoPassword}\"}}";

            var request = new HttpRequestMessage(HttpMethod.Post, url);
            request.Content = new StringContent(requestBody, Encoding.UTF8, "application/json");

            var response = await HttpClient.SendAsync(request);
            string responseText = await response.Content.ReadAsStringAsync();

            if (!response.IsSuccessStatusCode)
            {
                Console.WriteLine($"ERROR: Login failed with status {response.StatusCode}");
                return (false, string.Empty, string.Empty);
            }

            // Extract token and hospital ID from JSON response
            using (JsonDocument jsonDocument = JsonDocument.Parse(responseText))
            {
                var root = jsonDocument.RootElement;
                if (root.TryGetProperty("data", out var dataElement))
                {
                    if (dataElement.TryGetProperty("jwtToken", out var tokenElement))
                    {
                        jwtToken = tokenElement.GetString() ?? string.Empty;
                    }

                    if (dataElement.TryGetProperty("userInfo", out var userInfoElement))
                    {
                        if (userInfoElement.TryGetProperty("hospitalId", out var hospitalIdElement))
                        {
                            hospitalId = hospitalIdElement.GetString() ?? string.Empty;
                        }
                    }
                }
            }

            // Validate extracted data
            bool success = !string.IsNullOrEmpty(jwtToken) && !string.IsNullOrEmpty(hospitalId);
            if (success)
            {
                Console.WriteLine($"[HippoBackend] Login success, jwt_token obtained, hospital_id={hospitalId}");
            }
            return (success, jwtToken, hospitalId);
        }
        catch (JsonException ex)
        {
            Console.WriteLine($"ERROR: JSON parsing failed - {ex.Message}");
            return (false, string.Empty, string.Empty);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"ERROR: Login failed - {ex.Message}");
            return (false, string.Empty, string.Empty);
        }
    }

    /// <summary>
    /// Create patient record and return patient ID
    /// </summary>
    public static async Task<(bool success, string patientId)> CreatePatientAsync(string mrn, string patientName)
    {
        try
        {
            // Ensure auth
            if (!await EnsureLoggedInAsync())
            {
                return (false, string.Empty);
            }

            // Build patient creation request using module configuration
            string url = $"{_hippoBaseUrl}/hippo/thirdParty/queryOrCreatePatient";
            string requestBody =
                $"{{\"hospitalId\":\"{_hospitalId}\",\"user\":{{\"name\":\"{patientName}\",\"roles\":[3],\"hospitalId\":\"{_hospitalId}\",\"mrn\":\"{mrn}\"}}}}";

            // Send patient creation request with auto token and retry
            var (success, responseText) = await RequestWithTokenAsync("POST", url, requestBody);
            if (!success)
            {
                return (false, string.Empty);
            }

            // Extract patient ID from JSON response
            string patientId = string.Empty;
            using (JsonDocument jsonDocument = JsonDocument.Parse(responseText))
            {
                var root = jsonDocument.RootElement;
                if (root.TryGetProperty("data", out var dataElement))
                {
                    if (dataElement.TryGetProperty("patientId", out var patientIdElement))
                    {
                        patientId = patientIdElement.GetString() ?? string.Empty;
                    }
                }
            }

            // Validate patient ID
            return (!string.IsNullOrEmpty(patientId), patientId);
        }
        catch (JsonException ex)
        {
            Console.WriteLine($"ERROR: JSON parsing failed - {ex.Message}");
            return (false, string.Empty);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"ERROR: Patient creation failed - {ex.Message}");
            return (false, string.Empty);
        }
    }

    /// <summary>
    /// Generate unique data ID for file upload
    /// </summary>
    public static async Task<(bool success, string dataId)> GenerateDataIdAsync()
    {
        try
        {
            // Ensure auth
            if (!await EnsureLoggedInAsync())
            {
                return (false, string.Empty);
            }

            // Build data ID generation request using module configuration
            string url = $"{_hippoBaseUrl}/hippo/thirdParty/file/generateUniqueKey/1";

            // Send data ID generation request
            var (success, responseText) = await RequestWithTokenAsync("GET", url, string.Empty);
            if (!success)
            {
                return (false, string.Empty);
            }

            // Extract first key from keys array
            string dataId = string.Empty;
            if (!string.IsNullOrEmpty(responseText))
            {
                using (JsonDocument jsonDocument = JsonDocument.Parse(responseText))
                {
                    var root = jsonDocument.RootElement;
                    if (root.TryGetProperty("data", out var dataElement))
                    {
                        if (dataElement.TryGetProperty("keys", out var keysElement))
                        {
                            if (keysElement.ValueKind == JsonValueKind.Array && keysElement.GetArrayLength() > 0)
                            {
                                dataId = keysElement[0].GetString() ?? string.Empty;
                            }
                        }
                    }
                }
            }

            // Validate data ID
            return (!string.IsNullOrEmpty(dataId), dataId);
        }
        catch (JsonException ex)
        {
            Console.WriteLine($"ERROR: JSON parsing failed - {ex.Message}");
            return (false, string.Empty);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"ERROR: Data ID generation failed - {ex.Message}");
            return (false, string.Empty);
        }
    }

}
}
