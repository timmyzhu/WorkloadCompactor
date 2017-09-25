// StorageSSDEstimator.cpp - Code for estimating SSD traffic based on request sizes.
// See Estimator.hpp for details.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include "Estimator.hpp"

#include <assert.h>
#include <json/json.h>

StorageSSDEstimator::StorageSSDEstimator(const Json::Value& estimatorInfo)
{
    const Json::Value& bwTable = estimatorInfo["bandwidthTable"];
    _readBandwidthTable.resize(bwTable.size());
    _writeBandwidthTable.resize(bwTable.size());
    for (unsigned int entry = 0; entry < bwTable.size(); entry++) {
        const Json::Value& bwTableEntry = bwTable[entry];
        _readBandwidthTable[entry].requestSize = bwTableEntry["requestSize"].asInt();
        _readBandwidthTable[entry].bandwidth = bwTableEntry["readBandwidth"].asDouble();
        _writeBandwidthTable[entry].requestSize = bwTableEntry["requestSize"].asInt();
        _writeBandwidthTable[entry].bandwidth = bwTableEntry["writeBandwidth"].asDouble();
    }
}

double StorageSSDEstimator::estimateWork(int requestSize, bool isReadRequest)
{
    const vector<StorageBandwidth>& bandwidthTable = isReadRequest ? _readBandwidthTable : _writeBandwidthTable;
    double bandwidth = bandwidthTable.back().bandwidth; // max bw
    for (unsigned int i = 1; i < bandwidthTable.size(); i++) {
        if (requestSize < bandwidthTable[i].requestSize) {
            bandwidth = linearInterpolate(static_cast<double>(requestSize),
                                          static_cast<double>(bandwidthTable[i-1].requestSize), static_cast<double>(bandwidthTable[i].requestSize),
                                          bandwidthTable[i-1].bandwidth, bandwidthTable[i].bandwidth);

            break;
        }
    }
    assert(bandwidth > 0);
    return static_cast<double>(requestSize) / bandwidth;
}
