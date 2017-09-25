// storage_clnt.hpp - wrapper for storage_prot RPC interface.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef _STORAGE_CLNT_HPP
#define _STORAGE_CLNT_HPP

#include <string>
#include <json/json.h>
#include <rpc/rpc.h>
#include "storage_prot.h"

using namespace std;

class storage_clnt
{
private:
    CLIENT* _cl;

public:
    storage_clnt(string serverAddr, time_t timeoutSec = 5);
    ~storage_clnt();

    // Update storage QoS parameters for a client
    void updateClient(const Json::Value& flowInfo);
    // Get occupancy of a client
    double getOccupancy(unsigned long clientAddr);
};

#endif // _STORAGE_CLNT_HPP
