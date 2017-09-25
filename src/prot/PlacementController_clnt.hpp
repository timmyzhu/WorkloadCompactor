// PlacementController_clnt.hpp - wrapper for PlacementController_prot RPC interface.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef _PLACEMENT_CONTROLLER_CLNT_HPP
#define _PLACEMENT_CONTROLLER_CLNT_HPP

#include <vector>
#include <string>
#include <json/json.h>
#include <rpc/rpc.h>
#include "PlacementController_prot.h"

using namespace std;

class PlacementController_clnt
{
private:
    CLIENT* _cl;

public:
    PlacementController_clnt(string serverAddr, time_t timeoutSec = 36000);
    ~PlacementController_clnt();

    // Add a clientVM to PlacementController
    void addClientVM(string clientHost, string clientVM);
    // Delete a clientVM from PlacementController
    void delClientVM(string clientHost, string clientVM);
    // Add a serverVM to PlacementController
    void addServerVM(string serverHost, string serverVM);
    // Delete a serverVM from PlacementController
    void delServerVM(string serverHost, string serverVM);
    // Try to place a new client and update clientInfo with placement
    bool addClient(Json::Value& clientInfo, string addrPrefix, bool enforce);
    // Try to place a new set of clients and update clientInfos with placements
    bool addClients(Json::Value& clientInfos, string addrPrefix, bool enforce);
    // Delete a client from PlacementController
    void delClient(string name);
    // Delete a vector of clients from PlacementController
    void delClients(const vector<string>& names);
};

#endif // _PLACEMENT_CONTROLLER_CLNT_HPP
