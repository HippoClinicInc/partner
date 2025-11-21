#ifndef EDF_TIME_SIGNAL_DATA_UTILS_IMPL_HPP
#define EDF_TIME_SIGNAL_DATA_UTILS_IMPL_HPP

#include <cstring>
#include <algorithm>

// Helper function to set channel data unit
inline void setChannelDataUnit(
    const string& channelName,
    google::protobuf::Map<string, hippo::common::s3_file::GenericDataArray>& targetMap,
    const hippo::common::s3_file::DataStorageType dataStorageType,
    const unordered_map<string, int>& dataUnitTypeMap) {
    
    auto& signalData = targetMap[channelName];
    signalData.set_datatype(dataStorageType);
    
    auto it = dataUnitTypeMap.find(channelName);
    if (it != dataUnitTypeMap.end()) {
        signalData.set_dataunit(static_cast<hippo::common::s3_file::SignalDataUnit>(it->second));
    } else {
        signalData.set_dataunit(hippo::common::s3_file::SignalDataUnit::VOLT);
    }
}

// Template implementation of fillPartitionGenericChannelData
template <typename T>
void fillPartitionGenericChannelData(
    const optional<vector<vector<T>>>& dataArrayOpt,
    const vector<string>& channelNames,
    google::protobuf::Map<string, hippo::common::s3_file::GenericDataArray>& targetMap,
    const unordered_map<string, int>& dataUnitTypeMap,
    const hippo::common::s3_file::DataStorageType dataStorageType) {
    
    if (!dataArrayOpt.has_value()) return;
    const auto& dataArray = *dataArrayOpt;

    for (size_t channelId = 0; channelId < channelNames.size(); ++channelId) {
        const string& channelName = channelNames[channelId];
        const auto& srcVec = dataArray[channelId];
        auto& signalData = targetMap[channelName];

        if (dataStorageType == hippo::common::s3_file::DATA_STORAGE_FLOAT32) {
            auto* dataVec = signalData.mutable_datafloat32();
            dataVec->Resize(srcVec.size(), 0);
            if (!srcVec.empty()) {
                memcpy(dataVec->mutable_data(), srcVec.data(),
                       srcVec.size() * sizeof(float));
            }
        } else if (dataStorageType == hippo::common::s3_file::DATA_STORAGE_INT32) {
            auto* dataVec = signalData.mutable_dataint32();
            dataVec->Resize(srcVec.size(), 0);
            if (!srcVec.empty()) {
                memcpy(dataVec->mutable_data(), srcVec.data(),
                       srcVec.size() * sizeof(int32_t));
            }
        }

        setChannelDataUnit(channelName, targetMap, dataStorageType, dataUnitTypeMap);
    }
}

// Helper function to extract data slice from 2D array
template <typename T>
optional<vector<vector<T>>> extractDataSlice(
    const vector<vector<T>>& rawDataArray,
    int startIndex,
    int endIndex) {
    
    if (startIndex < 0 || endIndex < 0 || startIndex >= endIndex) {
        return nullopt;
    }

    vector<vector<T>> result;
    result.reserve(rawDataArray.size());

    for (const auto& channelData : rawDataArray) {
        int actualStart = max(0, min(startIndex, static_cast<int>(channelData.size())));
        int actualEnd = max(0, min(endIndex, static_cast<int>(channelData.size())));
        
        if (actualStart >= actualEnd) {
            result.push_back(vector<T>());
        } else {
            vector<T> slice(channelData.begin() + actualStart, channelData.begin() + actualEnd);
            result.push_back(slice);
        }
    }

    return result;
}

// Template implementation of generateSinglePartitionPaddingLoselessData
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
    const unordered_map<string, int>& dataUnitTypeMap) {
    
    // 1. Copy shared meta data
    hippo::common::s3_file::MeegData partitionData;
    partitionData.CopyFrom(sharedMeegMetaData);

    // 2. Setting the time data field for a partition
    partitionData.set_currentstarttimemicroseconds(static_cast<int64_t>(
        partitionGroupStartingTimeMicroseconds +
        static_cast<double>(partitionDataStartIndex) /
            partitionData.samplefrequency() * kSecondsToMicroseconds));

    partitionData.set_currentendtimemicroseconds(static_cast<int64_t>(
        partitionGroupStartingTimeMicroseconds +
        static_cast<double>(partitionDataEndIndex - 1) /
            partitionData.samplefrequency() * kSecondsToMicroseconds));

    auto* meegChannelData = partitionData.mutable_meegchanneldata();
    meegChannelData->set_meegchanneldataversion(
        hippo::common::s3_file::MeegChannelDataVersion::GENERIC_DATA_ARRAY);

    // 3. Fill main signal data
    auto rawDataSlice = extractDataSlice(rawDataArray, partitionDataStartIndex, partitionDataEndIndex);
    if (rawDataSlice.has_value()) {
        fillPartitionGenericChannelData<T>(
            rawDataSlice, channelNames,
            *partitionData.mutable_meegchanneldata()
                 ->mutable_genericdataarraychanneldata()
                 ->mutable_signaldata(),
            dataUnitTypeMap, dataStorageType);
    }

    // 4. Left padding
    if (leftPaddingStartIndex >= 0) {
        auto leftPaddingSlice = extractDataSlice(rawDataArray, leftPaddingStartIndex, partitionDataStartIndex);
        if (leftPaddingSlice.has_value()) {
            fillPartitionGenericChannelData<T>(
                leftPaddingSlice, channelNames,
                *partitionData.mutable_meegchanneldata()
                     ->mutable_genericdataarraychanneldata()
                     ->mutable_leftpadding(),
                dataUnitTypeMap, dataStorageType);
        }
    }

    // 5. Right padding
    if (rightPaddingEndIndex >= 0) {
        auto rightPaddingSlice = extractDataSlice(rawDataArray, partitionDataEndIndex, rightPaddingEndIndex);
        if (rightPaddingSlice.has_value()) {
            fillPartitionGenericChannelData<T>(
                rightPaddingSlice, channelNames,
                *partitionData.mutable_meegchanneldata()
                     ->mutable_genericdataarraychanneldata()
                     ->mutable_rightpadding(),
                dataUnitTypeMap, dataStorageType);
        }
    }
    
    return partitionData;
}

#endif // EDF_TIME_SIGNAL_DATA_UTILS_IMPL_HPP
