#ifndef MNE_DATA_HANDLER_H
#define MNE_DATA_HANDLER_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "edf_reader.h"
#include "hippo-cpp/hippo/common/s3_file.pb.h"
#include "hippo-cpp/hippo/web/device_data.pb.h"

using MeegData = hippo::common::s3_file::MeegData;
using DeviceData = hippo::web::DeviceData;

/**
 * @brief C++ implementation of MNE data handler, similar to Python's MneDataHandler
 *
 * This class processes MEEG data files and generates protobuf partitions.
 * For this test version, all operations are done locally without S3.
 */
class MneDataHandler
{
public:
    /**
     * @brief Constructor
     * @param file_path Path to the local BDF/EDF file
     * @param patient_id Patient ID
     * @param raw_file_id Raw file ID
     * @param raw_file_name Raw file name
     */
    MneDataHandler(const std::string& file_path, const std::string& patient_id, const std::string& raw_file_id,
                   const std::string& raw_file_name);

    /**
     * @brief Main processing function, equivalent to Python's process() method
     * @return Vector of DeviceData objects
     */
    std::vector<DeviceData> Process();

private:
    // Constants
    // Note: Python uses DOWN_SAMPLE_FREQUENCY = 250
    // If raw frequency <= 250Hz, no downsampling occurs (down_sample_rate = 1)
    // To force downsampling, set this to a lower value (e.g., 50 or 100)
    static constexpr int kDownSampleFrequency = 250;
    static constexpr int kPaddingTimeSeconds = 1;
    static constexpr int kExpectedRawDataPartitionMB = 8;
    static constexpr int kFloatValueCountChannel327Frequency6001Second = 327 * 600;
    static constexpr int kMBToBytes = 1024 * 1024;
    static constexpr int kSecondsToMicroseconds = 1000000;

    // Member variables
    std::string file_path_;
    std::string patient_id_;
    std::string raw_file_id_;
    std::string raw_file_name_;
    std::string download_path_;
    std::unique_ptr<EDFRaw> raw_data_;
    bool use_detrend_;
    int batch_partition_count_ = 30;  // Number of partitions per batch

    // Helper methods
    std::unique_ptr<EDFRaw> GenerateRawData();

    struct ExtractSharedMeegDataResult
    {
        std::vector<std::string> raw_channel_names;
        MeegData shared_meeg_raw_data;
        int64_t raw_start_time_microseconds;
        int64_t raw_end_time_microseconds;
    };
    ExtractSharedMeegDataResult ExtractSharedMeegData(const EDFRaw& raw_data);

    std::pair<int, int> GetDownSampleFrequency(int raw_data_frequency);

    struct PartitionIndices
    {
        std::vector<std::vector<int64_t>> group_partition_indices;
        std::vector<std::vector<int64_t>> down_sampled_group_partition_indices;
    };
    PartitionIndices CalculateGroupPartitionIndices(int64_t n_times, int channel_count, int raw_data_frequency,
                                                    int down_sample_rate);

    void SetChannelData(MeegData& shared_meeg_data, const EDFRaw& raw_data,
                        const std::vector<std::string>& raw_channel_names);

    std::vector<DeviceData>
    GeneratePartitionsFromArray2(const EDFRaw& raw_data,
                                 const std::vector<std::vector<int64_t>>& group_partition_indices,
                                 const std::vector<std::vector<int64_t>>& down_sampled_group_partition_indices,
                                 const std::vector<std::string>& channel_names, const MeegData& shared_meeg_data,
                                 int down_sample_rate, int64_t raw_start_time_microseconds);

    DeviceData UploadTopMapData(const MeegData& shared_meeg_raw_data);

    int64_t GetStartEndMicrosecondsFromRawData(const EDFRaw& raw_data, int64_t& start_time, int64_t& end_time);

    void SaveProtobufToLocal(const std::string& file_path, const std::string& serialized_data);

    DeviceData InitializeDeviceData();

    std::string CreateLocalDirectory(const std::string& patient_id, const std::string& raw_file_id);

    void DeleteMeegUploadFolder(int raw_data_frequency, int down_sample_frequency);

    // New helper methods for Python compatibility
    std::unordered_map<std::string, int> CreateDataUnitTypeMap(
        const std::vector<std::string>& channel_names,
        const MeegData& shared_meeg_data);

    struct NormalizeResult {
        std::vector<std::vector<float>> normalized_data;
        hippo::common::s3_file::DataStorageType storage_type;
    };
    NormalizeResult NormalizeRawDataUnit(
        const std::vector<std::vector<float>>& raw_data_array2,
        const MeegData& shared_meeg_metadata,
        const std::vector<std::string>& channel_names);

    std::vector<std::vector<std::vector<int64_t>>> GetBatchList(
        const std::vector<std::vector<int64_t>>& global_partition_indices);

    std::vector<int64_t> GetBatchLocalPartitionList(
        const std::vector<int64_t>& partition_global_indices,
        int64_t batch_left_data_global_start_index);

    void ProcessBatchPartitionList(
        const std::vector<std::vector<float>>& batch_raw_data_array2,
        const std::vector<std::vector<int64_t>>& original_batch_partition_list,
        const std::vector<std::vector<int64_t>>& down_sampled_batch_partition_list,
        const std::vector<std::string>& channel_names,
        int64_t batch_starting_time_microseconds,
        const MeegData& shared_meeg_metadata,
        int down_sample_rate,
        hippo::common::s3_file::DataStorageType raw_data_storage_type,
        const std::unordered_map<std::string, int>& data_unit_type_map,
        std::vector<DeviceData>& device_data_list);

    std::vector<std::vector<float>> CropRawData(
        const EDFRaw& raw_data,
        const std::vector<std::string>& channel_names,
        int64_t start_sample,
        int64_t end_sample);

    std::vector<std::vector<float>> ResampleData(
        const std::vector<std::vector<float>>& input_data,
        int down_sample_rate);
};

#endif // MNE_DATA_HANDLER_H
