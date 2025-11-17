#include "mne_data_handler.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include "hippo-cpp/hippo/common/common.pb.h"
#include "hippo-cpp/hippo/common/data_type.pb.h"
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

MneDataHandler::PartitionIndices MneDataHandler::CalculateGroupPartitionIndices(int64_t n_times, int channel_count,
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
    if (n_times / partition_data_num <= 1)
    {
        part_group_partition_indices.push_back({0, n_times});
    }
    else
    {
        int partition_num = static_cast<int>(n_times / partition_data_num);
        for (int i = 0; i < partition_num; i++)
        {
            int64_t start = i * partition_data_num;
            part_group_partition_indices.push_back({start, start + partition_data_num});
        }
    }

    int partition_length = static_cast<int>(part_group_partition_indices.size());
    result.group_partition_indices.resize(partition_length);
    result.down_sampled_group_partition_indices.resize(partition_length);

    int partition_data_padding_length = raw_data_frequency * kPaddingTimeSeconds;

    for (int i = 0; i < partition_length; i++)
    {
        int64_t partition_data_start_index = part_group_partition_indices[i][0];
        int64_t partition_data_end_index = part_group_partition_indices[i][1];

        int64_t left_padding_start_index;
        int64_t right_padding_end_index;

        if (partition_length == 1)
        {
            left_padding_start_index = -1 * down_sample_rate;
            right_padding_end_index = -1 * down_sample_rate;
        }
        else if (i == 0)
        {
            // First partition
            left_padding_start_index = -1 * down_sample_rate;
            right_padding_end_index = part_group_partition_indices[i + 1][0] + partition_data_padding_length;
        }
        else if (i == partition_length - 1)
        {
            // Final partition
            left_padding_start_index = part_group_partition_indices[i - 1][1] - partition_data_padding_length;
            right_padding_end_index = -1 * down_sample_rate;
            partition_data_end_index = n_times;
        }
        else
        {
            left_padding_start_index = part_group_partition_indices[i - 1][1] - partition_data_padding_length;
            right_padding_end_index = part_group_partition_indices[i + 1][0] + partition_data_padding_length;
        }

        result.group_partition_indices[i] = {left_padding_start_index, partition_data_start_index,
                                             partition_data_end_index, right_padding_end_index};

        result.down_sampled_group_partition_indices[i] = {
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

    std::cout << "Generating partitions from array2..." << std::endl;
    std::cout << "  Number of partitions: " << group_partition_indices.size() << std::endl;

    // Create output directory for raw partitions
    fs::path raw_output_dir = fs::path("output") / patient_id_ / raw_file_id_ /
        ("RawData_" + std::to_string(shared_meeg_data.samplefrequency()));
    fs::create_directories(raw_output_dir);

    // Create output directory for down-sampled partitions
    int down_sampled_frequency = shared_meeg_data.samplefrequency() / down_sample_rate;
    fs::path down_sampled_output_dir =
        fs::path("output") / patient_id_ / raw_file_id_ / ("DownSampledData_" + std::to_string(down_sampled_frequency));
    fs::create_directories(down_sampled_output_dir);

    // Generate raw data partitions
    for (size_t i = 0; i < group_partition_indices.size(); i++)
    {
        const auto& partition_indices = group_partition_indices[i];
        int64_t partition_start = partition_indices[1];
        int64_t partition_end = partition_indices[2];

        auto device_data = InitializeDeviceData();

        // Generate partition file name
        std::string partition_file_name = raw_file_id_ + "_RawPartition_" + std::to_string(i) + ".pb";
        fs::path partition_file_path = raw_output_dir / partition_file_name;

        // In real implementation, you would:
        // 1. Extract partition data from raw_data (from partition_start to partition_end)
        // 2. Generate MeegData protobuf for this partition
        // 3. Serialize and save to local file
        // For now, create a placeholder protobuf file
        std::string partition_data = "RawPartition_" + std::to_string(i) + " start:" + std::to_string(partition_start) +
            " end:" + std::to_string(partition_end);
        SaveProtobufToLocal(partition_file_path.string(), partition_data);

        // Set device_data fields
        device_data.set_dataid(raw_file_id_);
        device_data.set_dataname(partition_file_name);
        device_data.set_filename(partition_file_path.string());
        device_data.set_datasize(static_cast<int64_t>(partition_data.size()));
        device_data.set_frequency(shared_meeg_data.samplefrequency());
        device_data.set_datatype(static_cast<hippo::common::device::data::DataType>(0)); // RAW_DATA placeholder

        device_data_list.push_back(device_data);

        std::cout << "  Raw Partition " << i << ": [" << partition_start << ", " << partition_end << "]" << std::endl;
    }

    // Generate down-sampled data partitions
    for (size_t i = 0; i < down_sampled_group_partition_indices.size(); i++)
    {
        const auto& partition_indices = down_sampled_group_partition_indices[i];
        int64_t partition_start = partition_indices[1];
        int64_t partition_end = partition_indices[2];

        auto device_data = InitializeDeviceData();

        // Generate partition file name
        std::string partition_file_name = raw_file_id_ + "_DownSampledPartition_" + std::to_string(i) + ".pb";
        fs::path partition_file_path = down_sampled_output_dir / partition_file_name;

        // In real implementation, you would:
        // 1. Extract and down-sample partition data from raw_data
        // 2. Generate MeegData protobuf for this down-sampled partition
        // 3. Serialize and save to local file
        // For now, create a placeholder protobuf file
        std::string partition_data = "DownSampledPartition_" + std::to_string(i) +
            " start:" + std::to_string(partition_start) + " end:" + std::to_string(partition_end);
        SaveProtobufToLocal(partition_file_path.string(), partition_data);

        // Set device_data fields
        device_data.set_dataid(raw_file_id_);
        device_data.set_dataname(partition_file_name);
        device_data.set_filename(partition_file_path.string());
        device_data.set_datasize(static_cast<int64_t>(partition_data.size()));
        device_data.set_frequency(down_sampled_frequency);
        device_data.set_datatype(static_cast<hippo::common::device::data::DataType>(2)); // DOWN_SAMPLED placeholder

        device_data_list.push_back(device_data);

        std::cout << "  Down-sampled Partition " << i << ": [" << partition_start << ", " << partition_end << "]"
                  << std::endl;
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
    fs::create_directories(path.parent_path());

    // Write serialized data to file
    std::ofstream out(file_path, std::ios::binary);
    if (!out)
    {
        throw std::runtime_error("Failed to open file for writing: " + file_path);
    }
    out.write(serialized_data.data(), serialized_data.size());
    out.close();
}

std::string MneDataHandler::CreateLocalDirectory(const std::string& patient_id, const std::string& raw_file_id)
{
    // Generate a random 32-character hex suffix similar to uuid4 hex string
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(0, 15);

    std::stringstream suffix_stream;
    for (int i = 0; i < 32; i++)
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
            std::error_code ec;
            fs::remove_all(dir, ec);
            if (ec)
            {
                std::cerr << "Warning: failed to remove directory " << dir.string() << ": " << ec.message()
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
    for (size_t i = 0; i < device_data_list.size(); i++)
    {
        const auto& dd = device_data_list[i];
        std::cout << "  DeviceData[" << i << "]:" << std::endl;
        std::cout << "    data_id: " << dd.dataid() << std::endl;
        std::cout << "    data_name: " << dd.dataname() << std::endl;
        std::cout << "    file_name: " << dd.filename() << std::endl;
        std::cout << "    data_size: " << dd.datasize() << " bytes" << std::endl;
        std::cout << "    frequency: " << dd.frequency() << " Hz" << std::endl;
        std::cout << "    data_type: " << dd.datatype() << std::endl;
    }

    return device_data_list;
}
