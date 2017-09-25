// AdmissionController_clnt.hpp - wrapper for AdmissionController_prot RPC interface.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef _ADMISSION_CONTROLLER_CLNT_HPP
#define _ADMISSION_CONTROLLER_CLNT_HPP

#include <string>
#include <json/json.h>
#include <rpc/rpc.h>
#include "AdmissionController_prot.h"

using namespace std;

class AdmissionController_clnt
{
private:
    CLIENT* _cl;

public:
    AdmissionController_clnt(string serverAddr, time_t timeoutSec = 36000);
    ~AdmissionController_clnt();

    // Add a queue to AdmissionController
    void addQueue(const Json::Value& queueInfo);
    // Add queues to AdmissionController
    void addQueues(const Json::Value& queueInfos);
    // Delete a queue from AdmissionController
    void delQueue(string name);
    // Try to admit a new client
    bool addClient(const Json::Value& clientInfo, bool fastFirstFit);
    // Try to admit a new set of clients
    bool addClients(const Json::Value& clientInfos, bool fastFirstFit);
    // Delete a client from AdmissionController
    void delClient(string name);
};

#endif // _ADMISSION_CONTROLLER_CLNT_HPP
