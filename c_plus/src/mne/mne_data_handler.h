#ifndef MNE_DATA_HANDLER_H
#define MNE_DATA_HANDLER_H

#include <memory>
#include <string>
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
};

#endif // MNE_DATA_HANDLER_H
