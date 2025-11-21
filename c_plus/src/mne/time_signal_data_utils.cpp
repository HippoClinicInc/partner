#include "time_signal_data_utils.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace std;
using namespace hippo::common::s3_file;

// Helper function to create min-max projection segments
vector<uint8_t> createMinMaxProjectionSegments(
    const vector<float>& channelData,
    size_t segmentSize = kDataNumberInSingleSegment) {
    
    vector<uint8_t> projectedData;
    
    if (channelData.empty()) {
        return projectedData;
    }

    // Calculate number of segments
    size_t numSegments = (channelData.size() + segmentSize - 1) / segmentSize;
    projectedData.reserve(numSegments * 2); // Each segment has min and max

    for (size_t segIdx = 0; segIdx < numSegments; ++segIdx) {
        size_t startIdx = segIdx * segmentSize;
        size_t endIdx = min(startIdx + segmentSize, channelData.size());

        // Find min and max in this segment
        float minVal = channelData[startIdx];
        float maxVal = channelData[startIdx];

        for (size_t i = startIdx + 1; i < endIdx; ++i) {
            minVal = min(minVal, channelData[i]);
            maxVal = max(maxVal, channelData[i]);
        }

        // Project to [0, 255] range
        // Assuming the data is already normalized or we use a simple mapping
        uint8_t minByte = static_cast<uint8_t>(max(0.0f, min(255.0f, (minVal + 1.0f) * 127.5f)));
        uint8_t maxByte = static_cast<uint8_t>(max(0.0f, min(255.0f, (maxVal + 1.0f) * 127.5f)));

        projectedData.push_back(minByte);
        projectedData.push_back(maxByte);
    }

    return projectedData;
}

// Helper function to fill min-max projection channel data
void fillMinMaxProjectionChannelData(
    const optional<vector<vector<float>>>& dataArrayOpt,
    const vector<string>& channelNames,
    google::protobuf::Map<string, SignalSegmentProjectionData>& targetMap,
    const unordered_map<string, int>& dataUnitTypeMap) {
    
    if (!dataArrayOpt.has_value()) return;
    const auto& dataArray = *dataArrayOpt;

    for (size_t channelId = 0; channelId < channelNames.size(); ++channelId) {
        const string& channelName = channelNames[channelId];
        const auto& srcVec = dataArray[channelId];
        auto& projectionData = targetMap[channelName];

        // Set data unit
        auto it = dataUnitTypeMap.find(channelName);
        if (it != dataUnitTypeMap.end()) {
            projectionData.set_dataunit(static_cast<SignalDataUnit>(it->second));
        } else {
            projectionData.set_dataunit(SignalDataUnit::VOLT);
        }

        // Create min-max projection
        auto projectedBytes = createMinMaxProjectionSegments(srcVec);
        
        // Set projection metadata
        projectionData.set_maxprojectionvalue(kUint8MaxValue);
        projectionData.set_datanumberinsinglesegment(kDataNumberInSingleSegment);
        projectionData.set_totaldatanumber(srcVec.size());

        // Set the byte data
        if (!projectedBytes.empty()) {
            projectionData.set_databyte(projectedBytes.data(), projectedBytes.size());
        }
    }
}

// Implementation of generateSinglePartitionMinMaxProjection
MeegData generateSinglePartitionMinMaxProjection(
    const vector<vector<float>>& rawDataArray,
    int leftPaddingStartIndex,
    int partitionDataStartIndex,
    int partitionDataEndIndex,
    int rightPaddingEndIndex,
    const vector<string>& channelNames,
    int64_t partitionGroupStartingTimeMicroseconds,
    const MeegData& sharedMeegMetaData,
    const unordered_map<string, int>& dataUnitTypeMap) {
    
    // 1. Copy shared meta data
    MeegData partitionData;
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
        MeegChannelDataVersion::MIN_MAX_PROJECTED_DATA_ARRAY);

    // 3. Fill main signal data with min-max projection
    auto rawDataSlice = extractDataSlice(rawDataArray, partitionDataStartIndex, partitionDataEndIndex);
    if (rawDataSlice.has_value()) {
        fillMinMaxProjectionChannelData(
            rawDataSlice, channelNames,
            *partitionData.mutable_meegchanneldata()
                 ->mutable_minmaxprojecteddataarraychanneldata()
                 ->mutable_signaldata(),
            dataUnitTypeMap);
    }

    // 4. Left padding (if needed)
    if (leftPaddingStartIndex >= 0) {
        auto leftPaddingSlice = extractDataSlice(rawDataArray, leftPaddingStartIndex, partitionDataStartIndex);
        if (leftPaddingSlice.has_value()) {
            fillMinMaxProjectionChannelData(
                leftPaddingSlice, channelNames,
                *partitionData.mutable_meegchanneldata()
                     ->mutable_minmaxprojecteddataarraychanneldata()
                     ->mutable_leftpadding(),
                dataUnitTypeMap);
        }
    }

    // 5. Right padding (if needed)
    if (rightPaddingEndIndex >= 0) {
        auto rightPaddingSlice = extractDataSlice(rawDataArray, partitionDataEndIndex, rightPaddingEndIndex);
        if (rightPaddingSlice.has_value()) {
            fillMinMaxProjectionChannelData(
                rightPaddingSlice, channelNames,
                *partitionData.mutable_meegchanneldata()
                     ->mutable_minmaxprojecteddataarraychanneldata()
                     ->mutable_rightpadding(),
                dataUnitTypeMap);
        }
    }

    return partitionData;
}
