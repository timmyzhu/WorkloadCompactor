// storage_clnt.cpp - wrapper for storage_prot RPC interface.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <iostream>
#include <cstdlib>
#include <string>
#include <json/json.h>
#include <rpc/rpc.h>
#include "storage_prot.h"
#include "../common/common.hpp"
#include "storage_clnt.hpp"

using namespace std;

storage_clnt::storage_clnt(string serverAddr, time_t timeoutSec)
{
    // Connect to NFSEnforcer server
    _cl = clnt_create(serverAddr.c_str(), STORAGE_ENFORCER_PROGRAM, STORAGE_ENFORCER_V1, "tcp");
    if (_cl == NULL) {
        clnt_pcreateerror(serverAddr.c_str());
        exit(-1);
    }
    // Set nearly infinite RPC timeout
    struct timeval timeout;
    timeout.tv_sec = timeoutSec;
    timeout.tv_usec = 0;
    clnt_control(_cl, CLSET_TIMEOUT, reinterpret_cast<char*>(&timeout));
}

storage_clnt::~storage_clnt()
{
    // Destroy client
    clnt_destroy(_cl);
}

// Update storage QoS parameters for a client
void storage_clnt::updateClient(const Json::Value& flowInfo)
{
    StorageClient arg;
    arg.s_addr = addrInfo(flowInfo["clientAddr"].asString());
    arg.priority = flowInfo["priority"].asUInt();
    if (flowInfo.isMember("rateLimiters")) {
        const Json::Value& rateLimiters = flowInfo["rateLimiters"];
        arg.rateLimitRates.rateLimitRates_len = rateLimiters.size();
        arg.rateLimitRates.rateLimitRates_val = new double[arg.rateLimitRates.rateLimitRates_len];
        arg.rateLimitBursts.rateLimitBursts_len = arg.rateLimitRates.rateLimitRates_len;
        arg.rateLimitBursts.rateLimitBursts_val = new double[arg.rateLimitBursts.rateLimitBursts_len];
        for (unsigned int i = 0; i < rateLimiters.size(); i++) {
            const Json::Value& rateLimit = rateLimiters[i];
            arg.rateLimitRates.rateLimitRates_val[i] = rateLimit["rate"].asDouble();
            arg.rateLimitBursts.rateLimitBursts_val[i] = rateLimit["burst"].asDouble();
        }
    } else {
        arg.rateLimitRates.rateLimitRates_len = 0;
        arg.rateLimitRates.rateLimitRates_val = NULL;
        arg.rateLimitBursts.rateLimitBursts_len = 0;
        arg.rateLimitBursts.rateLimitBursts_val = NULL;
    }
    StorageUpdateArgs args = {1, &arg};
    enum clnt_stat status = storage_enforcer_update_1(args, NULL, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed storage RPC");
    }
    // Free memory
    delete[] arg.rateLimitRates.rateLimitRates_val;
    delete[] arg.rateLimitBursts.rateLimitBursts_val;
}

// Get occupancy of a client
double storage_clnt::getOccupancy(unsigned long clientAddr)
{
    StorageGetOccupancyArgs arg;
    arg.s_addr = clientAddr;
    StorageGetOccupancyRes result;
    enum clnt_stat status = storage_enforcer_get_occupancy_1(arg, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed storage RPC");
        return 0;
    } else {
        return result.occupancy;
    }
}
