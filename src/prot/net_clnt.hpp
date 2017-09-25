// net_clnt.hpp - wrapper for net_prot RPC interface.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef _NET_CLNT_HPP
#define _NET_CLNT_HPP

#include <string>
#include <json/json.h>
#include <rpc/rpc.h>
#include "net_prot.h"

using namespace std;

class net_clnt
{
private:
    CLIENT* _cl;

public:
    net_clnt(string serverAddr, time_t timeoutSec = 5);
    ~net_clnt();

    // Update network QoS parameters for a client
    void updateClient(const Json::Value& flowInfo);
    // Remove a client and revert its network QoS settings to defaults
    void removeClient(const Json::Value& flowInfo);
    // Get occupancy of a client
    double getOccupancy(unsigned long dstAddr, unsigned long srcAddr);
};

#endif // _NET_CLNT_HPP
