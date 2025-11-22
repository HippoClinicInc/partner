#include "mne_data_handler.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <unordered_map>
#include <zstd.h>
#include "hippo-cpp/hippo/common/common.pb.h"
#include "hippo-cpp/hippo/common/data_type.pb.h"

// Include FFT resampler
#include "fft_resampler.h"

// Include our own time_signal_data_utils for C++ usage
#include "time_signal_data_utils.h"
#if defined(_WIN32)
#define timegm _mkgmtime
#else
#include <sys/time.h>
#endif

// Include protobuf headers - adjust paths as needed
// Assuming protobuf files are generated and available
// #include "hippo/common/s3_file.pb.h"
// #include "hippo/web/device_data.pb.h"
// #include "hippo/common/data_type.pb.h"
// #include "hippo/common/common.pb.h"

// For now, we'll use a simplified approach without full protobuf
// In a real implementation, you would include the actual protobuf headers

namespace fs = std::filesystem;

// Helper function to compress data using zstd
std::string CompressWithZstd(const std::string& data, int compression_level = 3) {
    size_t const compressed_bound = ZSTD_compressBound(data.size());
    std::string compressed_data(compressed_bound, '\0');
    
    size_t const compressed_size = ZSTD_compress(
        &compressed_data[0], compressed_bound,
        data.data(), data.size(),
        compression_level);
    
    if (ZSTD_isError(compressed_size)) {
        std::cerr << "Zstd compression error: " << ZSTD_getErrorName(compressed_size) << std::endl;
        return data; // Return uncompressed data on error
    }
    
    compressed_data.resize(compressed_size);
    return compressed_data;
}

MneDataHandler::MneDataHandler(const std::string& file_path, const std::string& patient_id,
                               const std::string& raw_file_id, const std::string& raw_file_name) :
    file_path_(file_path), patient_id_(patient_id), raw_file_id_(raw_file_id), raw_file_name_(raw_file_name),
    use_detrend_(false)
{
}

std::unique_ptr<EDFRaw> MneDataHandler::GenerateRawData()
{
    std::cout << "Reading raw data from file: " << file_path_ << std::endl;
    auto raw = read_raw(file_path_, true, true);
    if (!raw)
    {
        throw std::runtime_error("Failed to read raw data from file: " + file_path_);
    }
    return raw;
}

MneDataHandler::ExtractSharedMeegDataResult MneDataHandler::ExtractSharedMeegData(const EDFRaw& raw_data)
{
    ExtractSharedMeegDataResult result;

    // 1. Get channel names
    result.raw_channel_names = raw_data.ch_names;

    // 2. Get start and end times
    int64_t start_time, end_time;
    GetStartEndMicrosecondsFromRawData(raw_data, start_time, end_time);
    result.raw_start_time_microseconds = start_time;
    result.raw_end_time_microseconds = end_time;

    // 3. Create MeegData structure
    result.shared_meeg_raw_data.set_rawfilename(raw_file_name_);
    result.shared_meeg_raw_data.set_patientid(patient_id_);
    result.shared_meeg_raw_data.set_samplefrequency(static_cast<int>(raw_data.sfreq[0]));
    auto use_detrend_enum = use_detrend_ ? hippo::common::BooleanType::BOOLEAN_TRUE :
        hippo::common::BooleanType::BOOLEAN_FALSE;
    result.shared_meeg_raw_data.set_usedetrend(use_detrend_enum);

    std::cout << "Extracted shared MEEG data:" << std::endl;
    std::cout << "  Channels: " << result.raw_channel_names.size() << std::endl;
    std::cout << "  Sample frequency: " << raw_data.sfreq[0] << " Hz" << std::endl;
    std::cout << "  Start time: " << result.raw_start_time_microseconds << " microseconds" << std::endl;
    std::cout << "  End time: " << result.raw_end_time_microseconds << " microseconds" << std::endl;

    return result;
}

int64_t MneDataHandler::GetStartEndMicrosecondsFromRawData(const EDFRaw& raw_data, int64_t& start_time,
                                                           int64_t& end_time)
{
    // Convert EDF start time to microseconds
    // EDF stores time as year, month, day, hour, minute, second, subsecond
    struct tm timeinfo = {};
    timeinfo.tm_year = raw_data.startdate_year - 1900;
    timeinfo.tm_mon = raw_data.startdate_month - 1;
    timeinfo.tm_mday = raw_data.startdate_day;
    timeinfo.tm_hour = raw_data.starttime_hour;
    timeinfo.tm_min = raw_data.starttime_minute;
    timeinfo.tm_sec = raw_data.starttime_second;

    // Convert to UTC timestamp
    time_t base_time = timegm(&timeinfo);
    if (base_time == -1)
    {
        // Fallback to current time if conversion fails
        base_time = std::time(nullptr);
    }

    // Add subsecond (in 100 nanosecond units, convert to microseconds)
    int64_t subsecond_microseconds = raw_data.starttime_subsecond / 10;

    // Calculate start timestamp in microseconds
    int64_t start_timestamp_microseconds =
        static_cast<int64_t>(base_time) * kSecondsToMicroseconds + subsecond_microseconds;

    // Calculate start and end times based on file duration
    auto times = raw_data.get_times();
    if (times.empty())
    {
        start_time = start_timestamp_microseconds;
        end_time = start_timestamp_microseconds;
        return start_timestamp_microseconds;
    }

    // Start time: base timestamp + first time point offset
    start_time = start_timestamp_microseconds + static_cast<int64_t>(times[0] * kSecondsToMicroseconds);

    // End time: base timestamp + last time point offset
    end_time = start_timestamp_microseconds + static_cast<int64_t>(times.back() * kSecondsToMicroseconds);

    return start_timestamp_microseconds;
}

std::pair<int, int> MneDataHandler::GetDownSampleFrequency(int raw_data_frequency)
{
    int down_sample_rate = static_cast<int>(std::floor(static_cast<double>(raw_data_frequency) / kDownSampleFrequency));
    int down_sample_frequency;

    if (raw_data_frequency > kDownSampleFrequency)
    {
        if (raw_data_frequency % down_sample_rate == 0)
        {
            down_sample_frequency = raw_data_frequency / down_sample_rate;
        }
        else if (raw_data_frequency % (down_sample_rate - 1) == 0)
        {
            down_sample_frequency = raw_data_frequency / (down_sample_rate - 1);
            down_sample_rate = down_sample_rate - 1;
        }
        else
        {
            down_sample_rate = 1;
            down_sample_frequency = raw_data_frequency;
        }
    }
    else
    {
        down_sample_frequency = raw_data_frequency;
        down_sample_rate = 1;
    }

    return std::make_pair(down_sample_frequency, down_sample_rate);
}

MneDataHandler::PartitionIndices MneDataHandler::CalculateGroupPartitionIndices(int64_t total_sample_count, int channel_count,
                                                                                int raw_data_frequency,
                                                                                int down_sample_rate)
{

    PartitionIndices result;

    // Calculate partition file size
    int partition_file_size = static_cast<int>((channel_count * raw_data_frequency) /
                                               static_cast<double>(kFloatValueCountChannel327Frequency6001Second) *
                                               kExpectedRawDataPartitionMB);

    if (partition_file_size < kExpectedRawDataPartitionMB)
    {
        partition_file_size = kExpectedRawDataPartitionMB;
    }

    // Calculate partition data number
    int partition_data_num = (partition_file_size * kMBToBytes) / (channel_count * 4);

    if (partition_data_num % down_sample_rate != 0)
    {
        partition_data_num = partition_data_num - (partition_data_num % down_sample_rate);
    }

    // Get group partition indices
    std::vector<std::vector<int64_t>> part_group_partition_indices;
    if (total_sample_count / partition_data_num <= 1)
    {
        part_group_partition_indices.push_back({0, total_sample_count});
    }
    else
    {
        int partition_num = static_cast<int>(total_sample_count / partition_data_num);
        for (int partition_index = 0; partition_index < partition_num; partition_index++)
        {
            int64_t start = partition_index * partition_data_num;
            part_group_partition_indices.push_back({start, start + partition_data_num});
        }
    }

    int partition_length = static_cast<int>(part_group_partition_indices.size());
    result.group_partition_indices.resize(partition_length);
    result.down_sampled_group_partition_indices.resize(partition_length);

    int partition_data_padding_length = raw_data_frequency * kPaddingTimeSeconds;

    for (int partition_index = 0; partition_index < partition_length; partition_index++)
    {
        int64_t partition_data_start_index = part_group_partition_indices[partition_index][0];
        int64_t partition_data_end_index = part_group_partition_indices[partition_index][1];

        int64_t left_padding_start_index;
        int64_t right_padding_end_index;

        if (partition_length == 1)
        {
            left_padding_start_index = -1 * down_sample_rate;
            right_padding_end_index = -1 * down_sample_rate;
        }
        else if (partition_index == 0)
        {
            // First partition
            left_padding_start_index = -1 * down_sample_rate;
            right_padding_end_index = part_group_partition_indices[partition_index + 1][0] + partition_data_padding_length;
        }
        else if (partition_index == partition_length - 1)
        {
            // Final partition
            left_padding_start_index = part_group_partition_indices[partition_index - 1][1] - partition_data_padding_length;
            right_padding_end_index = -1 * down_sample_rate;
            partition_data_end_index = total_sample_count;
        }
        else
        {
            left_padding_start_index = part_group_partition_indices[partition_index - 1][1] - partition_data_padding_length;
            right_padding_end_index = part_group_partition_indices[partition_index + 1][0] + partition_data_padding_length;
        }

        result.group_partition_indices[partition_index] = {left_padding_start_index, partition_data_start_index,
                                             partition_data_end_index, right_padding_end_index};

        result.down_sampled_group_partition_indices[partition_index] = {
            static_cast<int64_t>(left_padding_start_index / down_sample_rate),
            static_cast<int64_t>(partition_data_start_index / down_sample_rate),
            static_cast<int64_t>(partition_data_end_index / down_sample_rate),
            static_cast<int64_t>(right_padding_end_index / down_sample_rate)};
    }

    return result;
}

void MneDataHandler::SetChannelData(MeegData& shared_meeg_data, const EDFRaw& raw_data,
                                    const std::vector<std::string>& raw_channel_names)
{
    // In Python, this is a pass (no-op) for the base class
    // Subclasses can override this to set channel-specific data
    // For now, we'll just log
    std::cout << "Setting channel data for " << raw_channel_names.size() << " channels" << std::endl;
}

std::vector<DeviceData> MneDataHandler::GeneratePartitionsFromArray2(
    const EDFRaw& raw_data, const std::vector<std::vector<int64_t>>& group_partition_indices,
    const std::vector<std::vector<int64_t>>& down_sampled_group_partition_indices,
    const std::vector<std::string>& channel_names, const MeegData& shared_meeg_data, int down_sample_rate,
    int64_t raw_start_time_microseconds)
{
    std::vector<DeviceData> device_data_list;

    std::cout << "Generating partitions using batch processing..." << std::endl;
    std::cout << "  Number of partitions: " << group_partition_indices.size() << std::endl;
    
    // 1. Copy shared meeg metadata
    MeegData shared_meeg_metadata = shared_meeg_data;
    shared_meeg_metadata.clear_channelrelabels();
    shared_meeg_metadata.mutable_point2data()->clear();
    
    // 2. Create data unit type map (distinguish MEG/EEG channels)
    auto data_unit_type_map = CreateDataUnitTypeMap(channel_names, shared_meeg_data);
    
    // 3. Group partition indices into batches
    auto original_batch_list = GetBatchList(group_partition_indices);
    auto down_sampled_batch_list = GetBatchList(down_sampled_group_partition_indices);
    
    std::cout << "  Number of batches: " << original_batch_list.size() << std::endl;
    
    // 4. Process all batches
    int raw_data_frequency = shared_meeg_metadata.samplefrequency();
    for (size_t batch_index = 0; batch_index < original_batch_list.size(); batch_index++)
    {
        const auto& current_batch_partition_list = original_batch_list[batch_index];
        const auto& current_down_sampled_batch_partition_list = down_sampled_batch_list[batch_index];
        
        // Get batch data range
        const auto& first_partition = current_batch_partition_list[0];
        const auto& last_partition = current_batch_partition_list.back();
        
        int64_t batch_left_padding_global_start_index = first_partition[0];
        int64_t batch_partition_data_global_start_index = first_partition[1];
        int64_t batch_partition_data_global_end_index = last_partition[2];
        int64_t batch_right_padding_global_end_index = last_partition[3];
        
        // Calculate batch data indices
        int64_t batch_data_left_global_start_index = 
            (batch_left_padding_global_start_index >= 0) ? 
            batch_left_padding_global_start_index : batch_partition_data_global_start_index;
        int64_t batch_data_right_global_end_index = 
            (batch_right_padding_global_end_index >= 0) ? 
            batch_right_padding_global_end_index : batch_partition_data_global_end_index;
        
        // Crop raw data for this batch
        auto batch_raw_data_array2 = CropRawData(
            raw_data, channel_names, 
            batch_data_left_global_start_index, 
            batch_data_right_global_end_index);
        
        // Normalize data (physical to digital conversion)
        auto normalize_result = NormalizeRawDataUnit(
            batch_raw_data_array2, shared_meeg_metadata, channel_names);
        batch_raw_data_array2 = normalize_result.normalized_data;
        auto raw_data_storage_type = normalize_result.storage_type;
        
        // Calculate batch starting time
        int64_t batch_starting_time_microseconds = raw_start_time_microseconds + 
            static_cast<int64_t>((static_cast<double>(batch_data_left_global_start_index) / raw_data_frequency) * kSecondsToMicroseconds);
        
        // Convert global indices to local indices
        std::vector<std::vector<int64_t>> local_batch_partition_list;
        for (const auto& partition : current_batch_partition_list) {
            std::vector<int64_t> local_partition;
            for (int64_t index : partition) {
                local_partition.push_back(index - batch_data_left_global_start_index);
            }
            local_batch_partition_list.push_back(local_partition);
        }
        
        std::vector<std::vector<int64_t>> local_down_sampled_batch_partition_list;
        int64_t batch_down_sampled_left_global_start_index = 
            (current_down_sampled_batch_partition_list[0][0] >= 0) ? 
            current_down_sampled_batch_partition_list[0][0] : current_down_sampled_batch_partition_list[0][1];
        for (const auto& partition : current_down_sampled_batch_partition_list) {
            std::vector<int64_t> local_partition;
            for (int64_t index : partition) {
                local_partition.push_back(index - batch_down_sampled_left_global_start_index);
            }
            local_down_sampled_batch_partition_list.push_back(local_partition);
        }
        
        // Process this batch
        ProcessBatchPartitionList(
            batch_raw_data_array2,
            local_batch_partition_list,
            local_down_sampled_batch_partition_list,
            channel_names,
            batch_starting_time_microseconds,
            shared_meeg_metadata,
            down_sample_rate,
            raw_data_storage_type,
            data_unit_type_map,
            device_data_list);
        
        std::cout << "  Processed batch " << (batch_index + 1) << "/" << original_batch_list.size() << std::endl;
    }
    
    return device_data_list;
}

DeviceData MneDataHandler::UploadTopMapData(const MeegData& shared_meeg_raw_data)
{
    std::cout << "Generating and saving top map data..." << std::endl;

    // Initialize device data
    auto device_data = InitializeDeviceData();

    // Generate file path (local instead of S3)
    // Format: {raw_file_id}_MEEG.TopMapData
    std::string top_map_file_name = raw_file_id_ + "_MEEG.TopMapData";

    // Create output directory structure: output/{patient_id}/{raw_file_id}/TopMapData/
    fs::path output_dir = fs::path("output") / patient_id_ / raw_file_id_ / "TopMapData";
    fs::create_directories(output_dir);
    fs::path top_map_file_path = output_dir / top_map_file_name;

    // In real implementation, we would:
    // 1. Create TopMapData protobuf
    // 2. Set point2Data and channelRelabels
    // 3. Serialize with compression
    // For now, create a placeholder protobuf file
    std::string placeholder_data = "TopMapData protobuf placeholder for " + raw_file_id_;
    SaveProtobufToLocal(top_map_file_path.string(), placeholder_data);

    // Set device_data fields
    device_data.set_frequency(shared_meeg_raw_data.samplefrequency());
    device_data.set_dataid(raw_file_id_);
    device_data.set_dataname(top_map_file_name);
    device_data.set_filename(top_map_file_path.string());
    device_data.set_datasize(static_cast<int64_t>(placeholder_data.size()));
    device_data.set_datatype(static_cast<hippo::common::device::data::DataType>(1)); // ENERGE_MATRIX placeholder

    std::cout << "  Top map saved to: " << top_map_file_path << std::endl;

    return device_data;
}

DeviceData MneDataHandler::InitializeDeviceData()
{
    DeviceData device_data;
    device_data.set_dataid(raw_file_id_);
    device_data.set_dataname("");
    device_data.set_filename("");
    device_data.set_datasize(0);
    device_data.set_frequency(0);
    device_data.set_datatype(static_cast<hippo::common::device::data::DataType>(0));
    return device_data;
}

void MneDataHandler::SaveProtobufToLocal(const std::string& file_path, const std::string& serialized_data)
{
    // Create directory if it doesn't exist
    fs::path path(file_path);
    fs::path parent_dir = path.parent_path();
    
    if (!parent_dir.empty() && !fs::exists(parent_dir)) {
        std::error_code ec;
        fs::create_directories(parent_dir, ec);
        if (ec) {
            throw std::runtime_error("Failed to create directory: " + parent_dir.string() + 
                                   " Error: " + ec.message());
        }
        std::cout << "  Created directory: " + parent_dir.string() << std::endl;
    }

    // Write serialized data to file
    std::ofstream out(file_path, std::ios::binary);
    if (!out)
    {
        // Additional diagnostics
        std::string error_msg = "Failed to open file for writing: " + file_path + 
                               "\n  Directory: " + parent_dir.string() +
                               "\n  Path length: " + std::to_string(file_path.length()) +
                               "\n  Directory exists: " + (fs::exists(parent_dir) ? "YES" : "NO");
        
        // Check if it's a path length issue
        if (file_path.length() > 260) {
            error_msg += "\n  WARNING: Path exceeds Windows MAX_PATH (260 characters)!";
            error_msg += "\n  Consider using shorter file names or enabling long path support.";
        }
        
        throw std::runtime_error(error_msg);
    }
    
    out.write(serialized_data.data(), serialized_data.size());
    if (!out) {
        throw std::runtime_error("Failed to write data to file: " + file_path);
    }
    
    out.close();
    
    std::cout << "Saved file: " << file_path << " (" << serialized_data.size() << " bytes)" << std::endl;
}

std::string MneDataHandler::CreateLocalDirectory(const std::string& patient_id, const std::string& raw_file_id)
{
    // Generate a random 32-character hex suffix similar to uuid4 hex string
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(0, 15);

    std::stringstream suffix_stream;
    for (int hex_char_index = 0; hex_char_index < 32; hex_char_index++)
    {
        suffix_stream << std::hex << distribution(generator);
    }

    fs::path download_path = fs::temp_directory_path() / patient_id / raw_file_id / suffix_stream.str();
    fs::create_directories(download_path);
    return download_path.string();
}

void MneDataHandler::DeleteMeegUploadFolder(int raw_data_frequency, int down_sample_frequency)
{
    fs::path base_output_dir = fs::path("output") / patient_id_ / raw_file_id_;
    std::vector<fs::path> target_directories = {
        base_output_dir / ("RawData_" + std::to_string(raw_data_frequency)),
        base_output_dir / ("DownSampledData_" + std::to_string(down_sample_frequency)),
        base_output_dir / "TopMapData"};

    for (const auto& dir : target_directories)
    {
        if (fs::exists(dir))
        {
            std::error_code error_code;
            fs::remove_all(dir, error_code);
            if (error_code)
            {
                std::cerr << "Warning: failed to remove directory " << dir.string() << ": " << error_code.message()
                          << std::endl;
            }
        }
    }
}

std::vector<DeviceData> MneDataHandler::Process()
{
    std::cout << "=== MNE file process start ===" << std::endl;

    // 1. Build the local download directory and get the path to the directory.
    // Note that this folder will be deleted uniformly after the function has finished processing it.
    download_path_ = CreateLocalDirectory(patient_id_, raw_file_id_);
    std::cout << "The download path: " << download_path_ << std::endl;

    // 2. Read the raw data from local file (instead of downloading)
    raw_data_ = GenerateRawData();
    if (!raw_data_)
    {
        throw std::runtime_error("Failed to generate raw data");
    }

    // 3. Extract shared information from raw_data and encapsulate as meegData
    auto extract_result = ExtractSharedMeegData(*raw_data_);
    auto raw_channel_names = extract_result.raw_channel_names;
    auto shared_meeg_raw_data = extract_result.shared_meeg_raw_data;
    int64_t raw_start_time_microseconds = extract_result.raw_start_time_microseconds;
    int64_t raw_end_time_microseconds = extract_result.raw_end_time_microseconds;

    // Get down sample frequency
    int sample_frequency = shared_meeg_raw_data.samplefrequency();
    auto [resample_frequency, down_sample_rate] = GetDownSampleFrequency(sample_frequency);
    std::cout << "Resample frequency: " << resample_frequency << " Hz, Down sample rate: " << down_sample_rate
              << std::endl;

    // 4. Delete the meeg upload folder if it exists (local version - delete local directory)
    DeleteMeegUploadFolder(sample_frequency, resample_frequency);

    // 5. Calculate group partition indices
    auto [group_partition_indices, down_sampled_group_partition_indices] = CalculateGroupPartitionIndices(
        raw_data_->n_samples, static_cast<int>(raw_channel_names.size()), sample_frequency, down_sample_rate);
    std::cout << "Calculated " << group_partition_indices.size() << " partition groups" << std::endl;

    // 6. Set channel data
    SetChannelData(shared_meeg_raw_data, *raw_data_, raw_channel_names);

    // 7. Generate partitions raw proto data and sample proto data
    std::vector<DeviceData> device_data_list;
    auto partition_device_data = GeneratePartitionsFromArray2(
        *raw_data_, group_partition_indices, down_sampled_group_partition_indices, raw_channel_names,
        shared_meeg_raw_data, down_sample_rate, raw_start_time_microseconds);
    device_data_list.insert(device_data_list.end(), partition_device_data.begin(), partition_device_data.end());
    std::cout << "The partition mne data generate success. Total partitions: " << device_data_list.size() << std::endl;

    // 8. Generate and save top map (locally instead of uploading to S3)
    device_data_list.push_back(UploadTopMapData(shared_meeg_raw_data));
    std::cout << "The top map data saved to local file success." << std::endl;

    // 9. Print device_data_list summary
    std::cout << "\n=== Device Data List Summary ===" << std::endl;
    std::cout << "Total device data items: " << device_data_list.size() << std::endl;
    for (size_t device_data_index = 0; device_data_index < device_data_list.size(); device_data_index++)
    {
        const auto& device_data = device_data_list[device_data_index];
        std::cout << "  DeviceData[" << device_data_index << "]:" << std::endl;
        std::cout << "    data_id: " << device_data.dataid() << std::endl;
        std::cout << "    data_name: " << device_data.dataname() << std::endl;
        std::cout << "    file_name: " << device_data.filename() << std::endl;
        std::cout << "    data_size: " << device_data.datasize() << " bytes" << std::endl;
        std::cout << "    frequency: " << device_data.frequency() << " Hz" << std::endl;
        std::cout << "    data_type: " << device_data.datatype() << std::endl;
    }

    return device_data_list;
}

// ==================== New implementations for Python compatibility ====================

// 1. Create data unit type map (distinguish MEG/EEG channels)
std::unordered_map<std::string, int> MneDataHandler::CreateDataUnitTypeMap(
    const std::vector<std::string>& channel_names,
    const MeegData& shared_meeg_data)
{
    std::unordered_map<std::string, int> data_unit_type_map;
    
    // Check for MEG channels
    const auto& channel_names_map = shared_meeg_data.channelnames();
    if (channel_names_map.contains("MEG")) {
        const auto& meg_channels = channel_names_map.at("MEG");
        for (const auto& channel_name : meg_channels.name()) {
            data_unit_type_map[channel_name] = 
                static_cast<int>(hippo::common::s3_file::SignalDataUnit::TESLA);
        }
    }
    
    // Check for EEG channels
    if (channel_names_map.contains("EEG")) {
        const auto& eeg_channels = channel_names_map.at("EEG");
        for (const auto& channel_name : eeg_channels.name()) {
            data_unit_type_map[channel_name] = 
                static_cast<int>(hippo::common::s3_file::SignalDataUnit::VOLT);
        }
    }
    
    // Check for Reference channels
    if (channel_names_map.contains("REFERENCE")) {
        const auto& ref_channels = channel_names_map.at("REFERENCE");
        for (const auto& channel_name : ref_channels.name()) {
            data_unit_type_map[channel_name] = 
                static_cast<int>(hippo::common::s3_file::SignalDataUnit::VOLT);
        }
    }
    
    // For uncategorized channels, default to VOLT
    for (const auto& channel_name : channel_names) {
        if (data_unit_type_map.find(channel_name) == data_unit_type_map.end()) {
            data_unit_type_map[channel_name] = 
                static_cast<int>(hippo::common::s3_file::SignalDataUnit::VOLT);
        }
    }
    
    return data_unit_type_map;
}

// 2. Normalize raw data unit (physical to digital conversion)
MneDataHandler::NormalizeResult MneDataHandler::NormalizeRawDataUnit(
    const std::vector<std::vector<float>>& raw_data_array2,
    const MeegData& shared_meeg_metadata,
    const std::vector<std::string>& channel_names)
{
    NormalizeResult result;
    result.normalized_data = raw_data_array2;
    result.storage_type = hippo::common::s3_file::DataStorageType::DATA_STORAGE_INT32;
    
    // Get channel numeric factors
    const auto& channel_numeric_factors = shared_meeg_metadata.channelnumericfactors();
    
    // Convert each channel's data
    // For EDF/BDF: float32_value = (int32_value * calibrateFactor + offset) / unitFactor
    // We need to reverse this: int32_value = (float32_value * unitFactor - offset) / calibrateFactor
    for (size_t channel_index = 0; channel_index < channel_names.size(); channel_index++) {
        const auto& channel_name = channel_names[channel_index];
        
        // Find numeric factor for this channel
        if (channel_numeric_factors.contains(channel_name)) {
            const auto& numeric_factor = channel_numeric_factors.at(channel_name);
            
            // Use Case 2 parameters for EDF/BDF format
            double unit_factor = numeric_factor.unitfactor();
            double offset = numeric_factor.offset();
            double calibrate_factor = numeric_factor.calibratefactor();
            
            // Convert physical values to digital values
            // digital = (physical * unitFactor - offset) / calibrateFactor
            if (calibrate_factor != 0.0) {
                for (auto& value : result.normalized_data[channel_index]) {
                    value = static_cast<float>((value * unit_factor - offset) / calibrate_factor);
                }
            }
        }
    }
    
    return result;
}

// 3. Get batch list (group partitions into batches)
std::vector<std::vector<std::vector<int64_t>>> MneDataHandler::GetBatchList(
    const std::vector<std::vector<int64_t>>& global_partition_indices)
{
    std::vector<std::vector<std::vector<int64_t>>> batch_list;
    
    for (size_t partition_index = 0; partition_index < global_partition_indices.size(); 
         partition_index += batch_partition_count_) {
        std::vector<std::vector<int64_t>> current_batch_partition_list;
        
        size_t end_index = std::min(partition_index + batch_partition_count_, 
                                     global_partition_indices.size());
        for (size_t batch_index = partition_index; batch_index < end_index; batch_index++) {
            current_batch_partition_list.push_back(global_partition_indices[batch_index]);
        }
        
        batch_list.push_back(current_batch_partition_list);
    }
    
    return batch_list;
}

// 4. Get batch local partition list (convert global indices to relative indices)
std::vector<int64_t> MneDataHandler::GetBatchLocalPartitionList(
    const std::vector<int64_t>& partition_global_indices,
    int64_t batch_left_data_global_start_index)
{
    std::vector<int64_t> relative_indices;
    for (int64_t global_index : partition_global_indices) {
        relative_indices.push_back(global_index - batch_left_data_global_start_index);
    }
    return relative_indices;
}

// 5. Crop raw data
std::vector<std::vector<float>> MneDataHandler::CropRawData(
    const EDFRaw& raw_data,
    const std::vector<std::string>& channel_names,
    int64_t start_sample,
    int64_t end_sample)
{
    int64_t num_channels = static_cast<int64_t>(channel_names.size());
    int64_t num_samples = end_sample - start_sample;
    
    std::vector<std::vector<float>> cropped_data(num_channels);
    
    for (int64_t channel_index = 0; channel_index < num_channels; channel_index++) {
        cropped_data[channel_index].reserve(num_samples);
        
        // EDFRaw.data is [n_channels][n_samples]
        const auto& channel_data = raw_data.data[channel_index];
        
        for (int64_t sample_index = start_sample; sample_index < end_sample; sample_index++) {
            if (sample_index >= 0 && sample_index < static_cast<int64_t>(channel_data.size())) {
                cropped_data[channel_index].push_back(static_cast<float>(channel_data[sample_index]));
            } else {
                cropped_data[channel_index].push_back(0.0f);
            }
        }
    }
    
    return cropped_data;
}

// 6. Resample data using FFT-based resampling (production-ready)
std::vector<std::vector<float>> MneDataHandler::ResampleData(
    const std::vector<std::vector<float>>& input_data,
    int down_sample_rate)
{
    if (down_sample_rate == 1) {
        return input_data;
    }
    
    // Use FFT-based resampling for high quality
    // This is equivalent to scipy.signal.resample or mne.filter.resample in Python
    std::cout << "  Using FFT-based resampling (down_sample_rate=" << down_sample_rate << ")" << std::endl;
    
    return FFTResampler::ResampleMultiChannel(input_data, down_sample_rate);
}

// 7. Process batch partition list (main batch processing logic)
void MneDataHandler::ProcessBatchPartitionList(
    const std::vector<std::vector<float>>& batch_raw_data_array2,
    const std::vector<std::vector<int64_t>>& original_batch_partition_list,
    const std::vector<std::vector<int64_t>>& down_sampled_batch_partition_list,
    const std::vector<std::string>& channel_names,
    int64_t batch_starting_time_microseconds,
    const MeegData& shared_meeg_metadata,
    int down_sample_rate,
    hippo::common::s3_file::DataStorageType raw_data_storage_type,
    const std::unordered_map<std::string, int>& data_unit_type_map,
    std::vector<DeviceData>& device_data_list)
{
    // Create output directories
    int raw_frequency = shared_meeg_metadata.samplefrequency();
    int down_sampled_frequency = raw_frequency / down_sample_rate;
    
    fs::path raw_output_dir = fs::path("output") / patient_id_ / raw_file_id_ / 
        ("RawData_" + std::to_string(raw_frequency));
    fs::create_directories(raw_output_dir);
    
    fs::path down_sampled_output_dir = fs::path("output") / patient_id_ / raw_file_id_ / 
        ("DownSampledData_" + std::to_string(down_sampled_frequency));
    fs::create_directories(down_sampled_output_dir);
    
    // Process original partitions
    for (size_t partition_index = 0; partition_index < original_batch_partition_list.size(); partition_index++) {
        const auto& partition_indices = original_batch_partition_list[partition_index];
        int64_t left_padding_start = partition_indices[0];
        int64_t partition_start = partition_indices[1];
        int64_t partition_end = partition_indices[2];
        int64_t right_padding_end = partition_indices[3];
        
        auto device_data = InitializeDeviceData();
        
        // Python format (commented out due to path length issues on Windows):
        // int64_t partition_start_time = batch_starting_time_microseconds + 
        //     static_cast<int64_t>((partition_start * 1000000.0) / raw_frequency);
        // auto now = std::chrono::system_clock::now();
        // auto now_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        //     now.time_since_epoch()).count();
        // std::string partition_file_name = raw_file_id_ + "_" + 
        //     std::to_string(partition_start_time) + "_" + 
        //     std::to_string(now_microseconds) + ".MeegData.zstd";
        // std::string folder_name = raw_file_id_ + "_original_" + std::to_string(raw_frequency) + "_zstd";
        
        // Short path version (works on Windows):
        std::string partition_file_name = "raw_" + std::to_string(partition_index) + ".MeegData.zstd";
        std::string folder_name = "raw_" + std::to_string(raw_frequency);
        fs::path partition_file_path = raw_output_dir / folder_name / partition_file_name;
        
        // Generate partition data
        MeegData partition_meeg_data = generateSinglePartitionPaddingLoselessData<float>(
            batch_raw_data_array2,
            static_cast<int>(left_padding_start),
            static_cast<int>(partition_start),
            static_cast<int>(partition_end),
            static_cast<int>(right_padding_end),
            channel_names,
            batch_starting_time_microseconds,
            shared_meeg_metadata,
            raw_data_storage_type,
            data_unit_type_map);
        
        partition_meeg_data.set_currentfilename(partition_file_name);
        
        // Serialize
        std::string partition_data;
        if (!partition_meeg_data.SerializeToString(&partition_data)) {
            std::cerr << "Failed to serialize partition " << partition_index << std::endl;
            continue;
        }
        
        // Compress with zstd (original data only)
        std::cout << "  Compressing with zstd (original size: " << partition_data.size() << " bytes)..." << std::endl;
        std::string compressed_data = CompressWithZstd(partition_data, 3);
        std::cout << "  Compressed size: " << compressed_data.size() << " bytes" << std::endl;
        
        // Save compressed data
        SaveProtobufToLocal(partition_file_path.string(), compressed_data);
        
        // Set device_data fields
        device_data.set_dataid(raw_file_id_);
        device_data.set_dataname(partition_file_name);
        device_data.set_filename(partition_file_path.string());
        device_data.set_datasize(static_cast<int64_t>(compressed_data.size()));
        device_data.set_frequency(raw_frequency);
        device_data.set_datatype(static_cast<hippo::common::device::data::DataType>(0));
        
        device_data_list.push_back(device_data);
    }
    
    // Resample data for down-sampled partitions
    auto down_sampled_data = ResampleData(batch_raw_data_array2, down_sample_rate);
    
    // Process down-sampled partitions
    for (size_t partition_index = 0; partition_index < down_sampled_batch_partition_list.size(); partition_index++) {
        const auto& partition_indices = down_sampled_batch_partition_list[partition_index];
        int64_t left_padding_start = partition_indices[0];
        int64_t partition_start = partition_indices[1];
        int64_t partition_end = partition_indices[2];
        int64_t right_padding_end = partition_indices[3];
        
        auto device_data = InitializeDeviceData();
        
        // Python format (commented out due to path length issues on Windows):
        // int64_t partition_start_time = batch_starting_time_microseconds + 
        //     static_cast<int64_t>((partition_start * 1000000.0) / down_sampled_frequency);
        // auto now = std::chrono::system_clock::now();
        // auto now_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        //     now.time_since_epoch()).count();
        // std::string partition_file_name = raw_file_id_ + "_" + 
        //     std::to_string(partition_start_time) + "_" + 
        //     std::to_string(now_microseconds) + ".MeegData";
        // std::string folder_name = raw_file_id_ + "_down_sample_" + std::to_string(down_sampled_frequency);
        
        // Short path version (works on Windows):
        std::string partition_file_name = "down_" + std::to_string(partition_index) + ".MeegData";
        std::string folder_name = "down_" + std::to_string(down_sampled_frequency);
        fs::path partition_file_path = down_sampled_output_dir / folder_name / partition_file_name;
        
        // Generate partition data with min-max projection
        MeegData partition_meeg_data = generateSinglePartitionMinMaxProjection(
            down_sampled_data,
            static_cast<int>(left_padding_start),
            static_cast<int>(partition_start),
            static_cast<int>(partition_end),
            static_cast<int>(right_padding_end),
            channel_names,
            batch_starting_time_microseconds,
            shared_meeg_metadata,
            data_unit_type_map);
        
        partition_meeg_data.set_samplefrequency(down_sampled_frequency);
        partition_meeg_data.set_currentfilename(partition_file_name);
        
        // Set correct time range
        int64_t partition_start_time_microseconds = batch_starting_time_microseconds + 
            static_cast<int64_t>((static_cast<double>(partition_start) / down_sampled_frequency) * kSecondsToMicroseconds);
        int64_t partition_end_time_microseconds = batch_starting_time_microseconds + 
            static_cast<int64_t>((static_cast<double>(partition_end - 1) / down_sampled_frequency) * kSecondsToMicroseconds);
        
        partition_meeg_data.set_currentstarttimemicroseconds(partition_start_time_microseconds);
        partition_meeg_data.set_currentendtimemicroseconds(partition_end_time_microseconds);
        
        // Serialize and save
        std::string partition_data;
        if (!partition_meeg_data.SerializeToString(&partition_data)) {
            std::cerr << "Failed to serialize down-sampled partition " << partition_index << std::endl;
            continue;
        }
        
        SaveProtobufToLocal(partition_file_path.string(), partition_data);
        
        // Set device_data fields
        device_data.set_dataid(raw_file_id_);
        device_data.set_dataname(partition_file_name);
        device_data.set_filename(partition_file_path.string());
        device_data.set_datasize(static_cast<int64_t>(partition_data.size()));
        device_data.set_frequency(down_sampled_frequency);
        device_data.set_datatype(static_cast<hippo::common::device::data::DataType>(2));
        
        device_data_list.push_back(device_data);
    }
}
