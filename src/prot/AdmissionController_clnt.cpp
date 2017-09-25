// AdmissionController_clnt.cpp - wrapper for AdmissionController_prot RPC interface.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <iostream>
#include <cstdlib>
#include <string>
#include <json/json.h>
#include <rpc/rpc.h>
#include "AdmissionController_prot.h"
#include "../common/common.hpp"
#include "AdmissionController_clnt.hpp"

using namespace std;

AdmissionController_clnt::AdmissionController_clnt(string serverAddr, time_t timeoutSec)
{
    // Connect to AdmissionController server
    _cl = clnt_create(serverAddr.c_str(), ADMISSION_CONTROLLER_PROGRAM, ADMISSION_CONTROLLER_V1, "tcp");
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

AdmissionController_clnt::~AdmissionController_clnt()
{
    // Destroy client
    clnt_destroy(_cl);
}

// Add a queue to AdmissionController
void AdmissionController_clnt::addQueue(const Json::Value& queueInfo)
{
    AdmissionAddQueueArgs args;
    string queueInfoStr = jsonToString(queueInfo);
    args.queueInfo = new char[queueInfoStr.length() + 1];
    strcpy(args.queueInfo, queueInfoStr.c_str());
    AdmissionAddQueueRes result;
    enum clnt_stat status = admission_controller_add_queue_1(args, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed AdmissionController RPC");
    } else if (result.status != ADMISSION_SUCCESS) {
        cerr << "AddQueue failed with status " << result.status << endl;
    }
    delete[] args.queueInfo;
}

// Add queues to AdmissionController
void AdmissionController_clnt::addQueues(const Json::Value& queueInfos)
{
    for (unsigned int queueInfoIndex = 0; queueInfoIndex < queueInfos.size(); queueInfoIndex++) {
        const Json::Value& queueInfo = queueInfos[queueInfoIndex];
        addQueue(queueInfo);
    }
}

// Delete a queue from AdmissionController
void AdmissionController_clnt::delQueue(string name)
{
    AdmissionDelQueueArgs args;
    args.name = new char[name.length() + 1];
    strcpy(args.name, name.c_str());
    AdmissionDelQueueRes result;
    enum clnt_stat status = admission_controller_del_queue_1(args, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed AdmissionController RPC");
    } else if (result.status != ADMISSION_SUCCESS) {
        cerr << "DelQueue failed with status " << result.status << endl;
    }
    delete[] args.name;
}

// Try to admit a new client
bool AdmissionController_clnt::addClient(const Json::Value& clientInfo, bool fastFirstFit)
{
    Json::Value singleClientInfos = Json::arrayValue;
    singleClientInfos.append(clientInfo);
    return addClients(singleClientInfos, fastFirstFit);
}

// Try to admit a new set of clients
bool AdmissionController_clnt::addClients(const Json::Value& clientInfos, bool fastFirstFit)
{
    bool admitted = false;
    // Build RPC parameters
    AdmissionAddClientsArgs args;
    string clientInfosStr = jsonToString(clientInfos);
    args.clientInfos = new char[clientInfosStr.length() + 1];
    strcpy(args.clientInfos, clientInfosStr.c_str());
    args.fastFirstFit = fastFirstFit;
    AdmissionAddClientsRes result;
    enum clnt_stat status = admission_controller_add_clients_1(args, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed AdmissionController RPC");
    } else if (result.status != ADMISSION_SUCCESS) {
        cerr << "AddClients failed with status " << result.status << endl;
    } else {
        admitted = result.admitted;
    }
    delete[] args.clientInfos;
    return admitted;
}

// Delete a client from AdmissionController
void AdmissionController_clnt::delClient(string name)
{
    AdmissionDelClientArgs args;
    args.name = new char[name.length() + 1];
    strcpy(args.name, name.c_str());
    AdmissionDelClientRes result;
    enum clnt_stat status = admission_controller_del_client_1(args, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed AdmissionController RPC");
    } else if (result.status != ADMISSION_SUCCESS) {
        cerr << "DelClient failed with status " << result.status << endl;
    }
    delete[] args.name;
}
