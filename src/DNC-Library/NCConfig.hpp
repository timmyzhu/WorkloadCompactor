// NCConfig.hpp - Helper functions for configuring our specific system setup.
// Should be updated based on the system environment.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef _NCCONFIG_HPP
#define _NCCONFIG_HPP

#include <string>
#include <set>
#include <json/json.h>
#include "NC.hpp"
#include "../DNC-Library/DNC.hpp"

using namespace std;

// Return a name for flow into server based on the client name
string getFlowNetworkInName(string clientName);
// Return a name for flow out of server based on the client name
string getFlowNetworkOutName(string clientName);
// Return a name for storage flow at server based on the client name
string getFlowStorageName(string clientName);
// Return a name for queue into host machine
string getQueueInName(string host);
// Return a name for queue out of host machine
string getQueueOutName(string host);
// Return a name for a server
string getServerName(string host, string VM);
// Return the hostname of a particular VM
string getAddr(string prefix, string host, string VM);
// Return the arrival curve file
string getArrivalCurveFilename(string trace, string estimatorType);
// Set the arrivalInfo in a flow
void setArrivalInfo(Json::Value& flowInfo, string trace, const Json::Value& estimatorInfo, double maxRate);
// Generate config for a client
void configGenClient(Json::Value& clientInfo, string clientName, string prefix, bool enforce);
// Generate network in queue info
void configGenNetworkInQueue(Json::Value& queueInfo, string host);
// Generate network out queue info
void configGenNetworkOutQueue(Json::Value& queueInfo, string host);
// Generate storage queue info
void configGenStorageQueue(Json::Value& queueInfo, string serverName);
// Set latency, priority, and rate limit parameters in flowInfo
void setFlowParameters(Json::Value& flowInfo, NC* nc);
// Set rate limit parameters in flowInfo
void setRateLimits(Json::Value& flowInfo, const Curve& arrivalCurve, double maxRate);
// Set rate limit parameters in flowInfo
void setRateLimits(Json::Value& flowInfo, const SimpleArrivalCurve& shaperCurve);

#endif // _NCCONFIG_HPP
