// PlacementController_clnt.cpp - wrapper for PlacementController_prot RPC interface.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <iostream>
#include <cstdlib>
#include <vector>
#include <string>
#include <json/json.h>
#include <rpc/rpc.h>
#include "PlacementController_prot.h"
#include "../common/common.hpp"
#include "PlacementController_clnt.hpp"

using namespace std;

PlacementController_clnt::PlacementController_clnt(string serverAddr, time_t timeoutSec)
{
    // Connect to PlacementController server
    _cl = clnt_create(serverAddr.c_str(), PLACEMENT_CONTROLLER_PROGRAM, PLACEMENT_CONTROLLER_V1, "tcp");
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

PlacementController_clnt::~PlacementController_clnt()
{
    // Destroy client
    clnt_destroy(_cl);
}


// Add a clientVM to PlacementController
void PlacementController_clnt::addClientVM(string clientHost, string clientVM)
{
    PlacementAddClientVMArgs args;
    args.clientHost = new char[clientHost.length() + 1];
    args.clientVM = new char[clientVM.length() + 1];
    strcpy(args.clientHost, clientHost.c_str());
    strcpy(args.clientVM, clientVM.c_str());
    PlacementAddClientVMRes result;
    enum clnt_stat status = placement_controller_add_client_vm_1(args, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed PlacementController RPC");
    } else if (result.status != PLACEMENT_SUCCESS) {
        cerr << "AddClientVM failed with status " << result.status << endl;
    }
    delete[] args.clientHost;
    delete[] args.clientVM;
}

// Delete a clientVM from PlacementController
void PlacementController_clnt::delClientVM(string clientHost, string clientVM)
{
    PlacementDelClientVMArgs args;
    args.clientHost = new char[clientHost.length() + 1];
    args.clientVM = new char[clientVM.length() + 1];
    strcpy(args.clientHost, clientHost.c_str());
    strcpy(args.clientVM, clientVM.c_str());
    PlacementDelClientVMRes result;
    enum clnt_stat status = placement_controller_del_client_vm_1(args, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed PlacementController RPC");
    } else if (result.status != PLACEMENT_SUCCESS) {
        cerr << "DelClientVM failed with status " << result.status << endl;
    }
    delete[] args.clientHost;
    delete[] args.clientVM;
}

// Add a serverVM to PlacementController
void PlacementController_clnt::addServerVM(string serverHost, string serverVM)
{
    PlacementAddServerVMArgs args;
    args.serverHost = new char[serverHost.length() + 1];
    args.serverVM = new char[serverVM.length() + 1];
    strcpy(args.serverHost, serverHost.c_str());
    strcpy(args.serverVM, serverVM.c_str());
    PlacementAddServerVMRes result;
    enum clnt_stat status = placement_controller_add_server_vm_1(args, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed PlacementController RPC");
    } else if (result.status != PLACEMENT_SUCCESS) {
        cerr << "AddServerVM failed with status " << result.status << endl;
    }
    delete[] args.serverHost;
    delete[] args.serverVM;
}

// Delete a serverVM from PlacementController
void PlacementController_clnt::delServerVM(string serverHost, string serverVM)
{
    PlacementDelServerVMArgs args;
    args.serverHost = new char[serverHost.length() + 1];
    args.serverVM = new char[serverVM.length() + 1];
    strcpy(args.serverHost, serverHost.c_str());
    strcpy(args.serverVM, serverVM.c_str());
    PlacementDelServerVMRes result;
    enum clnt_stat status = placement_controller_del_server_vm_1(args, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed PlacementController RPC");
    } else if (result.status != PLACEMENT_SUCCESS) {
        cerr << "DelServerVM failed with status " << result.status << endl;
    }
    delete[] args.serverHost;
    delete[] args.serverVM;
}

// Try to place a new client and update clientInfo with placement
bool PlacementController_clnt::addClient(Json::Value& clientInfo, string addrPrefix, bool enforce)
{
    Json::Value singleClientInfos = Json::arrayValue;
    singleClientInfos.append(clientInfo);
    bool admitted = addClients(singleClientInfos, addrPrefix, enforce);
    // Copy result back to clientInfo
    clientInfo = singleClientInfos[0u];
    return admitted;
}

// Try to place a new set of clients and update clientInfos with placements
bool PlacementController_clnt::addClients(Json::Value& clientInfos, string addrPrefix, bool enforce)
{
    bool admitted = false;
    // Build RPC parameters
    PlacementAddClientsArgs args;
    string clientInfosStr = jsonToString(clientInfos);
    args.clientInfos = new char[clientInfosStr.length() + 1];
    strcpy(args.clientInfos, clientInfosStr.c_str());
    args.addrPrefix = new char[addrPrefix.length() + 1];
    strcpy(args.addrPrefix, addrPrefix.c_str());
    args.enforce = enforce;
    PlacementAddClientsRes result;
    memset(&result, 0, sizeof(result));
    enum clnt_stat status = placement_controller_add_clients_1(args, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed PlacementController RPC");
    } else if (result.status != PLACEMENT_SUCCESS) {
        cerr << "AddClients failed with status " << result.status << endl;
    } else {
        admitted = result.admitted;
        if (admitted) {
            if ((result.clientHosts.clientHosts_len == clientInfos.size()) &&
                (result.clientVMs.clientVMs_len == clientInfos.size()) &&
                (result.serverHosts.serverHosts_len == clientInfos.size()) &&
                (result.serverVMs.serverVMs_len == clientInfos.size())) {
                for (unsigned int clientInfoIndex = 0; clientInfoIndex < clientInfos.size(); clientInfoIndex++) {
                    Json::Value& clientInfo = clientInfos[clientInfoIndex];
                    clientInfo["clientHost"] = Json::Value(result.clientHosts.clientHosts_val[clientInfoIndex]);
                    clientInfo["clientVM"] = Json::Value(result.clientVMs.clientVMs_val[clientInfoIndex]);
                    clientInfo["serverHost"] = Json::Value(result.serverHosts.serverHosts_val[clientInfoIndex]);
                    clientInfo["serverVM"] = Json::Value(result.serverVMs.serverVMs_val[clientInfoIndex]);
                }
            } else {
                admitted = false;
                cerr << "AddClients returned invalid results" << endl;
            }
        }
        // Free result
        xdr_free((xdrproc_t)xdr_PlacementAddClientsRes, (caddr_t)&result);
    }
    delete[] args.clientInfos;
    delete[] args.addrPrefix;
    return admitted;
}

// Delete a client from PlacementController
void PlacementController_clnt::delClient(string name)
{
    vector<string> names;
    names.push_back(name);
    delClients(names);
}

// Delete a vector of clients from PlacementController
void PlacementController_clnt::delClients(const vector<string>& names)
{
    PlacementDelClientsArgs args;
    args.names.names_val = new char*[names.size()];
    args.names.names_len = names.size();
    for (unsigned int i = 0; i < names.size(); i++) {
        args.names.names_val[i] = new char[names[i].length() + 1];
        strcpy(args.names.names_val[i], names[i].c_str());
    }
    PlacementDelClientsRes result;
    enum clnt_stat status = placement_controller_del_clients_1(args, &result, _cl);
    if (status != RPC_SUCCESS) {
        clnt_perror(_cl, "Failed PlacementController RPC");
    } else if (result.status != PLACEMENT_SUCCESS) {
        cerr << "DelClients failed with status " << result.status << endl;
    }
    for (unsigned int i = 0; i < names.size(); i++) {
        delete[] args.names.names_val[i];
    }
    delete[] args.names.names_val;
}
