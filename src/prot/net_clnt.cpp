// net_clnt.cpp - wrapper for net_prot RPC interface.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <iostream>
#include <cstdlib>
#include <string>
#include <json/json.h>
#include <rpc/rpc.h>
#include "net_prot.h"
#include "../common/common.hpp"
#include "net_clnt.hpp"

using namespace std;

net_clnt::net_clnt(string serverAddr, time_t timeoutSec)
{
    // Connect to NetEnforcer server
    _cl = clnt_create(serverAddr.c_str(), NET_ENFORCER_PROGRAM, NET_ENFORCER_V1, "tcp");
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

net_clnt::~net_clnt()
{
    // Destroy client
    clnt_destroy(_cl);
}

// Update network QoS parameters for a client
void net_clnt::updateClient(const Json::Value& flowInfo)
{
    NetClientUpdate arg;
    arg.client.s_dstAddr = addrInfo(flowInfo["dstAddr"].asString());
    arg.client.s_srcAddr = addrInfo(flowInfo["srcAddr"].asString());
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
    NetUpdateClientsArgs args = {1, &arg};
    enum clnt_stat status = net_enforcer_update_clients_1(args, NULL, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed network RPC");
    }
    // Free memory
    delete[] arg.rateLimitRates.rateLimitRates_val;
    delete[] arg.rateLimitBursts.rateLimitBursts_val;
}

// Remove a client and revert its network QoS settings to defaults
void net_clnt::removeClient(const Json::Value& flowInfo)
{
    NetClient arg;
    arg.s_dstAddr = addrInfo(flowInfo["dstAddr"].asString());
    arg.s_srcAddr = addrInfo(flowInfo["srcAddr"].asString());
    NetRemoveClientsArgs args = {1, &arg};
    enum clnt_stat status = net_enforcer_remove_clients_1(args, NULL, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed network RPC");
    }
}

// Get occupancy of a client
double net_clnt::getOccupancy(unsigned long dstAddr, unsigned long srcAddr)
{
    NetGetOccupancyArgs arg;
    arg.s_dstAddr = dstAddr;
    arg.s_srcAddr = srcAddr;
    NetGetOccupancyRes result;
    enum clnt_stat status = net_enforcer_get_occupancy_1(arg, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed network RPC");
        return 0;
    } else {
        return result.occupancy;
    }
}
