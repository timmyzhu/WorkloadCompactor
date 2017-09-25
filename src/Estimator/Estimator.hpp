// Estimator.hpp - Class definitions for estimators.
// To avoid handling different request types when analyzing traces, we consolidate all request sizes into a generic form, which we call "work".
// The units of work only need to relate to the bandwidth of the queues. For example, we represent network work in terms of bytes and network queues in terms of bytes per second.
// For other devices such as storage, we may choose to represent work in terms of storage time, in which case the storage queues would have a bandwidth of 1 storage second per second.
// Thus, we can abstract away the details of estimating for different request types into these estimators.
// Creating accurate estimators is an interesting area for future research, particularly for other types of devices and requests.
// Nevertheless, this abstraction allows the rest of the network calculus analysis can operate on "work" as estimated by the estimateWork function.
//
// As a concrete example, consider read and write requests in a storage system. Write requests send a lot of data to the server, but only get a small response.
// On the other hand, read requests have most of the network traffic from the server back to the VM.
// Thus, we have different estimators based on whether we are looking at the flow from the VM to the server or from the server back to the VM.
//
// Estimators are configured based on the estimatorInfo JSON dictionary. Network estimators have the following fields:
// "type": string - indicates the type of estimator
// "nonDataConstant": double - constant overhead for non-data heavy requests
// "nonDataFactor": double - effect of requestSize for non-data heavy requests; expected slightly above 0.0
// "dataConstant": double - constant overhead for data heavy requests
// "dataFactor": double - effect of requestSize for data heavy requests; expected slightly above 1.0
//
// SSD storage estimators have the following fields:
// "type": string - indicates the type of estimator
// "bandwidthTable": list of bandwidths - list of read/write bandwidths for various request sizes, sorted by request size
// where each bandwidth table entry consists of the following fields:
// "requestSize": int - request size (bytes)
// "readBandwidth": double - read bandwidth when accessing a given request size (bytes per second)
// "writeBandwidth": double - write bandwidth when accessing a given request size (bytes per second)
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef _ESTIMATOR_HPP
#define _ESTIMATOR_HPP

#include <vector>
#include <json/json.h>

using namespace std;

enum EstimatorType {
    ESTIMATOR_NETWORK_IN,
    ESTIMATOR_NETWORK_OUT,
    ESTIMATOR_STORAGE,
};

// Base class for estimators.
class Estimator
{
public:
    Estimator() {}
    virtual ~Estimator() {}

    // Create appropriate estimator as specified in estimatorInfo.
    static Estimator* create(const Json::Value& estimatorInfo);

    // Estimate work based on request size and type.
    // This is the main function that converts request size into "work" units.
    virtual double estimateWork(int requestSize, bool isReadRequest) = 0;
    // Returns type of estimator.
    virtual EstimatorType estimatorType() = 0;
    // Reset any estimator state, if any.
    virtual void reset() {}
};

// Estimator for network traffic from VM to server.
// Write requests generate a lot of traffic based on request size, whereas read requests are always small.
// For network estimators, "work" units remain in terms of bytes, but we translate the request size based on request type (i.e., read vs write).
// We perform the mapping using an affine function factor * requestSize + constant to represent both the effect of the request size as well as constant overheads.
// We have two sets of parameters for the data heavy requests (i.e., write) vs the non-data heavy requests (i.e., read).
class NetworkInEstimator : public Estimator
{
private:
    double _nonDataConstant;
    double _nonDataFactor;
    double _dataConstant;
    double _dataFactor;

public:
    NetworkInEstimator(const Json::Value& estimatorInfo)
        : _nonDataConstant(estimatorInfo["nonDataConstant"].asDouble()),
          _nonDataFactor(estimatorInfo["nonDataFactor"].asDouble()),
          _dataConstant(estimatorInfo["dataConstant"].asDouble()),
          _dataFactor(estimatorInfo["dataFactor"].asDouble())
    {}
    virtual ~NetworkInEstimator() {}

    virtual double estimateWork(int requestSize, bool isReadRequest);
    virtual EstimatorType estimatorType() { return ESTIMATOR_NETWORK_IN; }
};

// Estimator for network traffic from server back to VM.
// Read requests generate a lot of traffic based on request size, whereas write requests are always small.
// For network estimators, "work" units remain in terms of bytes, but we translate the request size based on request type (i.e., read vs write).
// We perform the mapping using an affine function factor * requestSize + constant to represent both the effect of the request size as well as constant overheads.
// We have two sets of parameters for the data heavy requests (i.e., read) vs the non-data heavy requests (i.e., write).
class NetworkOutEstimator : public Estimator
{
private:
    double _nonDataConstant;
    double _nonDataFactor;
    double _dataConstant;
    double _dataFactor;

public:
    NetworkOutEstimator(const Json::Value& estimatorInfo)
        : _nonDataConstant(estimatorInfo["nonDataConstant"].asDouble()),
          _nonDataFactor(estimatorInfo["nonDataFactor"].asDouble()),
          _dataConstant(estimatorInfo["dataConstant"].asDouble()),
          _dataFactor(estimatorInfo["dataFactor"].asDouble())
    {}
    virtual ~NetworkOutEstimator() {}

    virtual double estimateWork(int requestSize, bool isReadRequest);
    virtual EstimatorType estimatorType() { return ESTIMATOR_NETWORK_OUT; }
};

typedef struct {
    int requestSize; //B
    double bandwidth; // B/s
} StorageBandwidth;

// Estimator for SSD storage traffic at server.
// Read and write characteristics are different, so they are each profiled separately.
// Storage profiles look at bandwidth over a range of request sizes and interpolate to calculate the bandwidth of a request.
class StorageSSDEstimator : public Estimator
{
protected:
    vector<StorageBandwidth> _readBandwidthTable;
    vector<StorageBandwidth> _writeBandwidthTable;

public:
    StorageSSDEstimator(const vector<StorageBandwidth>& readBandwidthTable, const vector<StorageBandwidth>& writeBandwidthTable)
        : _readBandwidthTable(readBandwidthTable),
          _writeBandwidthTable(writeBandwidthTable)
    {}
    StorageSSDEstimator(const Json::Value& estimatorInfo);
    virtual ~StorageSSDEstimator() {}

    virtual double estimateWork(int requestSize, bool isReadRequest);
    virtual EstimatorType estimatorType() { return ESTIMATOR_STORAGE; }
};

inline double linearInterpolate(double x, double x0, double x1, double y0, double y1)
{
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

#endif // _ESTIMATOR_HPP
