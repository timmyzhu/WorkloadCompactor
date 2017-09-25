// NCConfig.cpp - Helper functions for configuring our specific system setup.
// Should be updated based on the system environment.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <json/json.h>
#include "../DNC-Library/DNC.hpp"
#include "../DNC-Library/WorkloadCompactor.hpp"
#include "../common/common.hpp"
#include "NCConfig.hpp"

const double NETWORK_BANDWIDTH = 125000000; // bytes/sec
const double STORAGE_BANDWIDTH = 1; // work secs/sec
const string profileFilename = "profileSSD.txt";

// Return a name for flow into server based on the client name
string getFlowNetworkInName(string clientName)
{
    return "F" + clientName + "In";
}

// Return a name for flow out of server based on the client name
string getFlowNetworkOutName(string clientName)
{
    return "F" + clientName + "Out";
}

// Return a name for storage flow at server based on the client name
string getFlowStorageName(string clientName)
{
    return "F" + clientName + "Storage";
}

// Return a name for queue into host machine
string getQueueInName(string host)
{
    return host + "-in";
}

// Return a name for queue out of host machine
string getQueueOutName(string host)
{
    return host + "-out";
}

// Return a name for a server
string getServerName(string host, string VM)
{
    return host + "vm" + VM;
}

// Return the hostname of a particular VM
string getAddr(string prefix, string host, string VM)
{
    return prefix + "-" + getServerName(host, VM);
}

// Return the arrival curve file
string getArrivalCurveFilename(string trace, string estimatorType)
{
    ostringstream oss;
    oss << "arrivalCurves/arrivalCurve" << trace.substr(trace.find_last_of("/\\") + 1) << estimatorType << ".txt";
    return oss.str();
}

// Set the arrivalInfo in a flow
void setArrivalInfo(Json::Value& flowInfo, string trace, const Json::Value& estimatorInfo, double maxRate)
{
    string arrivalCurveFilename = getArrivalCurveFilename(trace, estimatorInfo["type"].asString());
    DNC::setArrivalInfo(flowInfo, trace, estimatorInfo, maxRate, arrivalCurveFilename);
}

// Generate config for a client
void configGenClient(Json::Value& clientInfo, string clientName, string prefix, bool enforce)
{
    clientInfo["name"] = Json::Value(clientName);
    string clientHost = clientInfo["clientHost"].asString();
    string clientVM = clientInfo["clientVM"].asString();
    string serverHost = clientInfo["serverHost"].asString();
    string serverVM = clientInfo["serverVM"].asString();
    clientInfo.removeMember("clientHost");
    clientInfo.removeMember("clientVM");
    clientInfo.removeMember("serverHost");
    clientInfo.removeMember("serverVM");
    string clientAddr = getAddr(prefix, clientHost, clientVM);
    string serverAddr = getAddr(prefix, serverHost, serverVM);
    clientInfo["clientAddr"] = Json::Value(clientAddr);
    clientInfo["serverAddr"] = Json::Value(serverAddr);
    bool networkOnly = (clientInfo.isMember("networkOnly") && clientInfo["networkOnly"].asBool());
    bool storageOnly = (clientInfo.isMember("storageOnly") && clientInfo["storageOnly"].asBool());
    Json::Value& clientFlows = clientInfo["flows"];
    clientFlows = Json::arrayValue;
    if (!storageOnly) {
        // Setup flow from client to server
        Json::Value& flowInInfo = clientFlows[clientFlows.size()];
        flowInInfo["name"] = Json::Value(getFlowNetworkInName(clientName));
        if (enforce) {
            flowInInfo["enforcerType"] = Json::Value("network");
            flowInInfo["enforcerAddr"] = Json::Value(clientHost);
            flowInInfo["dstAddr"] = Json::Value(serverAddr);
            flowInInfo["srcAddr"] = Json::Value(clientAddr);
        }
        Json::Value& flowInQueues = flowInInfo["queues"];
        flowInQueues = Json::arrayValue;
        flowInQueues.resize(2);
        flowInQueues[0] = Json::Value(getQueueOutName(clientHost));
        flowInQueues[1] = Json::Value(getQueueInName(serverHost));
        Json::Value networkInEstimatorInfo;
        networkInEstimatorInfo["type"] = Json::Value("networkIn");
        networkInEstimatorInfo["nonDataConstant"] = Json::Value(200.0);
        networkInEstimatorInfo["nonDataFactor"] = Json::Value(0.025);
        networkInEstimatorInfo["dataConstant"] = Json::Value(200.0);
        networkInEstimatorInfo["dataFactor"] = Json::Value(1.1);
        setArrivalInfo(flowInInfo, clientInfo["trace"].asString(), networkInEstimatorInfo, NETWORK_BANDWIDTH);
    }
    if (!networkOnly) {
        // Setup storage flow at server
        Json::Value& flowStorageInfo = clientFlows[clientFlows.size()];
        flowStorageInfo["name"] = Json::Value(getFlowStorageName(clientName));
        if (enforce) {
            flowStorageInfo["enforcerType"] = Json::Value("storage");
            flowStorageInfo["enforcerAddr"] = Json::Value(serverAddr);
            flowStorageInfo["clientAddr"] = Json::Value(clientAddr);
        }
        Json::Value& flowStorageQueues = flowStorageInfo["queues"];
        flowStorageQueues = Json::arrayValue;
        flowStorageQueues.resize(1);
        flowStorageQueues[0] = Json::Value(getServerName(serverHost, serverVM));
        Json::Value profileCfg;
        if (!readJson(profileFilename, profileCfg)) {
            return;
        }
        Json::Value storageEstimatorInfo;
        storageEstimatorInfo["type"] = Json::Value("storageSSD");
        storageEstimatorInfo["bandwidthTable"] = profileCfg["bandwidthTable"];
        setArrivalInfo(flowStorageInfo, clientInfo["trace"].asString(), storageEstimatorInfo, STORAGE_BANDWIDTH);
    }
    if (!storageOnly) {
        // Setup flow from server to client
        Json::Value& flowOutInfo = clientFlows[clientFlows.size()];
        flowOutInfo["name"] = Json::Value(getFlowNetworkOutName(clientName));
        if (enforce) {
            flowOutInfo["enforcerType"] = Json::Value("network");
            flowOutInfo["enforcerAddr"] = Json::Value(serverHost);
            flowOutInfo["dstAddr"] = Json::Value(clientAddr);
            flowOutInfo["srcAddr"] = Json::Value(serverAddr);
        }
        Json::Value& flowOutQueues = flowOutInfo["queues"];
        flowOutQueues = Json::arrayValue;
        flowOutQueues.resize(2);
        flowOutQueues[0] = Json::Value(getQueueOutName(serverHost));
        flowOutQueues[1] = Json::Value(getQueueInName(clientHost));
        Json::Value networkOutEstimatorInfo;
        networkOutEstimatorInfo["type"] = Json::Value("networkOut");
        networkOutEstimatorInfo["nonDataConstant"] = Json::Value(200.0);
        networkOutEstimatorInfo["nonDataFactor"] = Json::Value(0.025);
        networkOutEstimatorInfo["dataConstant"] = Json::Value(200.0);
        networkOutEstimatorInfo["dataFactor"] = Json::Value(1.1);
        setArrivalInfo(flowOutInfo, clientInfo["trace"].asString(), networkOutEstimatorInfo, NETWORK_BANDWIDTH);
    }
}

// Generate network in queue info
void configGenNetworkInQueue(Json::Value& queueInfo, string host)
{
    queueInfo["name"] = Json::Value(getQueueInName(host));
    queueInfo["bandwidth"] = Json::Value(NETWORK_BANDWIDTH);
}

// Generate network out queue info
void configGenNetworkOutQueue(Json::Value& queueInfo, string host)
{
    queueInfo["name"] = Json::Value(getQueueOutName(host));
    queueInfo["bandwidth"] = Json::Value(NETWORK_BANDWIDTH);
}

// Generate storage queue info
void configGenStorageQueue(Json::Value& queueInfo, string serverName)
{
    queueInfo["name"] = Json::Value(serverName);
    queueInfo["bandwidth"] = Json::Value(STORAGE_BANDWIDTH);
}

// Set latency, priority, and rate limit parameters in flowInfo
void setFlowParameters(Json::Value& flowInfo, NC* nc)
{
    FlowId flowId = nc->getFlowIdByName(flowInfo["name"].asString());
    const Flow* f = nc->getFlow(flowId);
    assert(f->name == flowInfo["name"].asString());
    // Assign latency
    flowInfo["latency"] = Json::Value(f->latency);
    // Assign priority
    flowInfo["priority"] = Json::Value(f->priority);
    // Assign rate limiters
    DNC* dnc = dynamic_cast<DNC*>(nc);
    if (dnc) {
        setRateLimits(flowInfo, dnc->getShaperCurve(flowId));
        return;
    }
}
void setRateLimits(Json::Value& flowInfo, const Curve& arrivalCurve, double maxRate)
{
    // Skip the first rate limit if it's equal to the speed of the device (i.e., maxRate)
    // since the device is already limited by its speed
    unsigned int end = 1;
    if (arrivalCurve[end].slope == maxRate) {
        end++;
    }
    for (unsigned int i = arrivalCurve.size() - 1; i >= end; i--) {
        const PointSlope& p = arrivalCurve[i];
        Json::Value& rateLimit = flowInfo["rateLimiters"][static_cast<unsigned int>(arrivalCurve.size() - 1 - i)];
        rateLimit["rate"] = Json::Value(p.slope);
        rateLimit["burst"] = Json::Value(yIntercept(p.x, p.y, p.slope));
    }
}
void setRateLimits(Json::Value& flowInfo, const SimpleArrivalCurve& shaperCurve)
{
    Json::Value& rateLimit = flowInfo["rateLimiters"][0u];
    rateLimit["rate"] = Json::Value(shaperCurve.r);
    rateLimit["burst"] = Json::Value(shaperCurve.b);
}
