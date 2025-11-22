#ifndef EDF_TIME_SIGNAL_DATA_UTILS_H
#define EDF_TIME_SIGNAL_DATA_UTILS_H

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "hippo-cpp/hippo/common/s3_file.pb.h"

using namespace std;

// Constants
const int64_t kSecondsToMicroseconds = 1000000;
const size_t kDataNumberInSingleSegment = 200;
const int kUint8MaxValue = 255;

/**
 * Fill partition generic channel data from a 2D array
 * This is equivalent to fillPartitionGenericChannelData in time_signal_data_utils.cpp
 */
template <typename T>
void fillPartitionGenericChannelData(
    const optional<vector<vector<T>>>& dataArrayOpt,
    const vector<string>& channelNames,
    google::protobuf::Map<string, hippo::common::s3_file::GenericDataArray>& targetMap,
    const unordered_map<string, int>& dataUnitTypeMap,
    const hippo::common::s3_file::DataStorageType dataStorageType);

/**
 * Generate a single partition with padding and lossless data
 * This is equivalent to generateSinglePartitionPaddingLoselessData in time_signal_data_utils.cpp
 * 
 * @param rawDataArray 2D array [channel][samples] containing the raw data
 * @param leftPaddingStartIndex Start index for left padding (-1 if no left padding)
 * @param partitionDataStartIndex Start index for partition data (inclusive)
 * @param partitionDataEndIndex End index for partition data (exclusive)
 * @param rightPaddingEndIndex End index for right padding (-1 if no right padding)
 * @param channelNames List of channel names
 * @param partitionGroupStartingTimeMicroseconds Starting time in microseconds
 * @param sharedMeegMetaData Shared MEEG metadata
 * @param dataStorageType Data storage type (INT32 or FLOAT32)
 * @param dataUnitTypeMap Map of channel name to data unit type
 * @return MeegData protobuf for this partition
 */
template <typename T>
hippo::common::s3_file::MeegData generateSinglePartitionPaddingLoselessData(
    const vector<vector<T>>& rawDataArray,
    int leftPaddingStartIndex,
    int partitionDataStartIndex,
    int partitionDataEndIndex,
    int rightPaddingEndIndex,
    const vector<string>& channelNames,
    int64_t partitionGroupStartingTimeMicroseconds,
    const hippo::common::s3_file::MeegData& sharedMeegMetaData,
    const hippo::common::s3_file::DataStorageType dataStorageType,
    const unordered_map<string, int>& dataUnitTypeMap);

/**
 * Generate a single partition with min-max projection for down-sampled data
 * 
 * @param rawDataArray 2D array [channel][samples] containing the down-sampled data
 * @param leftPaddingStartIndex Start index for left padding (-1 if no left padding)
 * @param partitionDataStartIndex Start index for partition data (inclusive)
 * @param partitionDataEndIndex End index for partition data (exclusive)
 * @param rightPaddingEndIndex End index for right padding (-1 if no right padding)
 * @param channelNames List of channel names
 * @param partitionGroupStartingTimeMicroseconds Starting time in microseconds
 * @param sharedMeegMetaData Shared MEEG metadata
 * @param dataUnitTypeMap Map of channel name to data unit type
 * @return MeegData protobuf for this partition with min-max projection
 */
hippo::common::s3_file::MeegData generateSinglePartitionMinMaxProjection(
    const vector<vector<float>>& rawDataArray,
    int leftPaddingStartIndex,
    int partitionDataStartIndex,
    int partitionDataEndIndex,
    int rightPaddingEndIndex,
    const vector<string>& channelNames,
    int64_t partitionGroupStartingTimeMicroseconds,
    const hippo::common::s3_file::MeegData& sharedMeegMetaData,
    const unordered_map<string, int>& dataUnitTypeMap);

// Include template implementations
#include "time_signal_data_utils_impl.hpp"

#endif // EDF_TIME_SIGNAL_DATA_UTILS_H
