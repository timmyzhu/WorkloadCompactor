// PlacementController.cpp - code for placing workloads on client/server machines.
// Communicates with AdmissionController admission control servers to add workloads (a.k.a. clients) to the system.
// Workloads are placed one by one onto servers in a first-fit fashion.
// To improve the placement performance, multiple admission control servers can be used to run the computation in parallel.
// Each admission control server is used to speculatively test the ability to place a workload onto a server.
// This is done until a fit is found, at which point, the work to test the rest of the servers is canceled.
//
// Command line parameters:
// -a AdmissionControllerAddr (required) - the address of the AdmissionController server that determines if a workload can fit on a server; this command line option can be used multiple times to use multiple AdmissionController servers for performing the placement computation in parallel
// -f (optional) - enables the fast-first-fit computation optimization, which tells the AdmissionController server to return early if a placement is unlikely to fit
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <cassert>
#include <iostream>
#include <fstream>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <cerrno>
#include <pthread.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include "../prot/PlacementController_prot.h"
#include "../prot/AdmissionController_clnt.hpp"
#include <json/json.h>
#include "../common/time.hpp"
#include "../common/common.hpp"
#include "../DNC-Library/NCConfig.hpp"

using namespace std;

struct WorkloadInfo {
    string name;
    string clientHost;
    string clientVM;
    string serverHost;
    string serverVM;
};

//
// Globals fixed at init
//
vector<AdmissionController_clnt*> g_clnts; // connections to AdmissionController servers that perform most of the computation; many are used for computation parallelism
bool g_fastFirstFit = false; // enable fast-first-fit computation optimization

//
// Globals protected by g_mutex
//
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
map<string, set<string> > g_servers; // map serverHost -> serverVMs
map<string, set<string> > g_clients; // map clientHost -> clientVMs
map<string, string> g_serverClientGrouping; // map serverHost -> clientHost to group workloads that share the same server onto the same client
list<WorkloadInfo> g_workloads; // list of workloads in system
// manage placement work queue
pthread_cond_t g_workAvailable = PTHREAD_COND_INITIALIZER; // indicates there is work to do
pthread_cond_t g_workComplete = PTHREAD_COND_INITIALIZER; // indicates current placement is complete
Json::Value* g_currentClientInfo = NULL; // current workload to place
string g_currentAddrPrefix = ""; // current addrPrefix
vector<pair<string, string> > g_workQueue; // work queue consists of a list of serverHost/serverVM pairs to try placing the current workload onto
unsigned int g_outstandingWork = 0; // number of placements being tested concurrently
unsigned int g_nextWorkQueueIndex = 0; // next index in work queue to test
unsigned int g_bestWorkQueueIndex; // index of best server (i.e., lowest index for first-fit)

// Decides which client VM to place a workload on.
// The current algorithm groups workloads that share a server onto the same client machine.
// This is because their performance is already correlated by sharing a server, so it's better
// to continue sharing so as not to introduce additional correlations with other workloads.
// Assumes g_mutex is held
pair<string, string> clientServerPlacement(string serverHost)
{
    string clientHost;
    map<string, string>::const_iterator it = g_serverClientGrouping.find(serverHost);
    if (it != g_serverClientGrouping.end()) {
        clientHost = it->second;
        const set<string>& clientVMs = g_clients[clientHost];
        if (!clientVMs.empty()) {
            return pair<string, string>(clientHost, *(clientVMs.begin()));
        }
    }
    // Check for other workloads using server
    for (list<WorkloadInfo>::iterator it2 = g_workloads.begin(); it2 != g_workloads.end(); it2++) {
        if (it2->serverHost == serverHost) {
            clientHost = it2->clientHost;
            const set<string>& clientVMs = g_clients[clientHost];
            if (!clientVMs.empty()) {
                return pair<string, string>(clientHost, *(clientVMs.begin()));
            }
        }
    }
    // Look for a client to use
    unsigned int maxAvailableClients = 0;
    for (map<string, set<string> >::const_iterator it3 = g_clients.begin(); it3 != g_clients.end(); it3++) {
        if (it3->second.size() > maxAvailableClients) {
            maxAvailableClients = it3->second.size();
            clientHost = it3->first;
        }
    }
    if (maxAvailableClients <= 0) {
        cerr << "Out of client machines" << endl;
        exit(-1);
    }
    return pair<string, string>(clientHost, *(g_clients[clientHost].begin()));
}

//
// Manage placement work queue
//
// Assumes g_mutex is held
unsigned int nextWork()
{
    while (g_nextWorkQueueIndex >= g_workQueue.size()) {
        pthread_cond_wait(&g_workAvailable, &g_mutex);
    }
    unsigned int workQueueIndex = g_nextWorkQueueIndex;
    g_nextWorkQueueIndex++;
    g_outstandingWork++;
    return workQueueIndex;
}

// Assumes g_mutex is held
void workComplete(unsigned int workQueueIndex, bool admitted)
{
    g_outstandingWork--;
    if (admitted) {
        // Cancel remaining global work (optimization)
        g_nextWorkQueueIndex = g_workQueue.size();
        // Track best placement
        if (workQueueIndex < g_bestWorkQueueIndex) {
            g_bestWorkQueueIndex = workQueueIndex;
        }
    }
    // Check if done with client
    if ((g_outstandingWork == 0) && (g_nextWorkQueueIndex >= g_workQueue.size())) {
        pthread_cond_signal(&g_workComplete);
    }
}

void* workerThread(void* ptr)
{
    AdmissionController_clnt* clnt = static_cast<AdmissionController_clnt*>(ptr);
    pthread_mutex_lock(&g_mutex);
    while (true) {
        unsigned int workQueueIndex = nextWork();
        pair<string, string> server = g_workQueue[workQueueIndex];
        pair<string, string> client = clientServerPlacement(server.first);
        // Make a copy of clientInfo
        Json::Value clientInfo = *g_currentClientInfo;
        pthread_mutex_unlock(&g_mutex);

        // Update client/server
        clientInfo["clientHost"] = Json::Value(client.first);
        clientInfo["clientVM"] = Json::Value(client.second);
        clientInfo["serverHost"] = Json::Value(server.first);
        clientInfo["serverVM"] = Json::Value(server.second);
        // Convert clientInfo using NC-ConfigGen
        configGenClient(clientInfo, clientInfo["name"].asString(), g_currentAddrPrefix, false);
        bool admitted = clnt->addClient(clientInfo, g_fastFirstFit);
        if (admitted) {
            clnt->delClient(clientInfo["name"].asString());
        }

        pthread_mutex_lock(&g_mutex);
        workComplete(workQueueIndex, admitted);
    }
    pthread_mutex_unlock(&g_mutex);
    return NULL;
}

// Decides which server VM to place a workload on.
// Assumes called from single thread
// Assumes g_mutex is held
bool placeClient(Json::Value& clientInfo, string addrPrefix, bool enforce)
{
    assert(g_currentClientInfo == NULL);
    assert(g_currentAddrPrefix == "");
    assert(g_workQueue.empty());
    assert(g_nextWorkQueueIndex == 0);
    g_currentClientInfo = &clientInfo;
    g_currentAddrPrefix = addrPrefix;
    // Check if admitted already
    if (clientInfo.isMember("admitted") && clientInfo["admitted"].asBool()) {
        g_workQueue.push_back(pair<string, string>(clientInfo["serverHost"].asString(), clientInfo["serverVM"].asString()));
        g_bestWorkQueueIndex = 0;
    } else {
        // Add work in first fit order
        for (map<string, set<string> >::const_iterator it = g_servers.begin(); it != g_servers.end(); it++) {
            string serverHost = it->first;
            const set<string>& serverVMs = it->second;
            for (set<string>::const_iterator it2 = serverVMs.begin(); it2 != serverVMs.end(); it2++) {
                string serverVM = *it2;
                g_workQueue.push_back(pair<string, string>(serverHost, serverVM));
            }
        }
        g_bestWorkQueueIndex = g_workQueue.size();
        pthread_cond_broadcast(&g_workAvailable);
        // Wait for work to complete
        while ((g_outstandingWork > 0) || (g_nextWorkQueueIndex < g_workQueue.size())) {
            pthread_cond_wait(&g_workComplete, &g_mutex);
        }
    }
    // Get results
    bool admitted = (g_bestWorkQueueIndex < g_workQueue.size());
    if (admitted) {
        // Mark admitted
        clientInfo["admitted"] = Json::Value(true);
        pair<string, string> server = g_workQueue[g_bestWorkQueueIndex];
        pair<string, string> client = clientServerPlacement(server.first);
        clientInfo["clientHost"] = Json::Value(client.first);
        clientInfo["clientVM"] = Json::Value(client.second);
        clientInfo["serverHost"] = Json::Value(server.first);
        clientInfo["serverVM"] = Json::Value(server.second);
        // Convert clientInfo using NC-ConfigGen
        string clientName = clientInfo["name"].asString();
        if (enforce) {
            Json::Value clientInfoCopy = clientInfo;
            configGenClient(clientInfoCopy, clientName, addrPrefix, true);
            configGenClient(clientInfo, clientName, addrPrefix, false);
            g_clnts[0]->addClient(clientInfoCopy, g_fastFirstFit);
        } else {
            configGenClient(clientInfo, clientName, addrPrefix, false);
            g_clnts[0]->addClient(clientInfo, g_fastFirstFit);
        }
        // Update all remaining clnts
        for (unsigned int index = 1; index < g_clnts.size(); index++) {
            g_clnts[index]->addClient(clientInfo, g_fastFirstFit);
        }
        // Mark client as used
        g_serverClientGrouping[server.first] = client.first;
        g_clients[client.first].erase(client.second);
        // Add workload info
        WorkloadInfo workloadInfo;
        workloadInfo.name = clientName;
        workloadInfo.clientHost = client.first;
        workloadInfo.clientVM = client.second;
        workloadInfo.serverHost = server.first;
        workloadInfo.serverVM = server.second;
        g_workloads.push_back(workloadInfo);
    }
    g_currentClientInfo = NULL;
    g_currentAddrPrefix = "";
    g_workQueue.clear();
    g_nextWorkQueueIndex = 0;
    return admitted;
}

// Remove a workload from the system.
// Assumes g_mutex is held
void removeClient(string clientName)
{
    for (list<WorkloadInfo>::iterator it = g_workloads.begin(); it != g_workloads.end(); it++) {
        if (it->name == clientName) {
            // Update clnts
            for (unsigned int index = 0; index < g_clnts.size(); index++) {
                g_clnts[index]->delClient(clientName);
            }
            // Mark client as unused
            g_serverClientGrouping.erase(it->serverHost);
            g_clients[it->clientHost].insert(it->clientVM);
            // Remove workload info
            g_workloads.erase(it);
            break;
        }
    }
}

// AddClients RPC - performs placement on a set of workloads and adds workloads to system.
// Assumes RPCs are not multi-threaded
PlacementAddClientsRes* placement_controller_add_clients_svc(PlacementAddClientsArgs* argp, struct svc_req* rqstp)
{
    static PlacementAddClientsRes result = {PLACEMENT_SUCCESS, false, {0, NULL}, {0, NULL}, {0, NULL}, {0, NULL}};
    // Parse input
    Json::Value clientInfos;
    if (!stringToJson(argp->clientInfos, clientInfos)) {
        result.status = PLACEMENT_ERR_INVALID_ARGUMENT;
        result.admitted = false;
        return &result;
    }
    string addrPrefix(argp->addrPrefix);
    bool enforce = argp->enforce;
    // Delete old arrays
    for (unsigned int i = 0; i < result.clientHosts.clientHosts_len; i++) {
        delete[] result.clientHosts.clientHosts_val[i];
    }
    delete[] result.clientHosts.clientHosts_val;
    for (unsigned int i = 0; i < result.clientVMs.clientVMs_len; i++) {
        delete[] result.clientVMs.clientVMs_val[i];
    }
    delete[] result.clientVMs.clientVMs_val;
    for (unsigned int i = 0; i < result.serverHosts.serverHosts_len; i++) {
        delete[] result.serverHosts.serverHosts_val[i];
    }
    delete[] result.serverHosts.serverHosts_val;
    for (unsigned int i = 0; i < result.serverVMs.serverVMs_len; i++) {
        delete[] result.serverVMs.serverVMs_val[i];
    }
    delete[] result.serverVMs.serverVMs_val;
    // Create new result arrays
    result.clientHosts.clientHosts_val = new char*[clientInfos.size()];
    result.clientHosts.clientHosts_len = clientInfos.size();
    result.clientVMs.clientVMs_val = new char*[clientInfos.size()];
    result.clientVMs.clientVMs_len = clientInfos.size();
    result.serverHosts.serverHosts_val = new char*[clientInfos.size()];
    result.serverHosts.serverHosts_len = clientInfos.size();
    result.serverVMs.serverVMs_val = new char*[clientInfos.size()];
    result.serverVMs.serverVMs_len = clientInfos.size();
    // Make placements
    result.admitted = true;
    pthread_mutex_lock(&g_mutex);
    for (unsigned int i = 0; i < clientInfos.size(); i++) {
        Json::Value& clientInfo = clientInfos[i];
        if (placeClient(clientInfo, addrPrefix, enforce)) {
            WorkloadInfo& workloadInfo = g_workloads.back();
            result.clientHosts.clientHosts_val[i] = new char[workloadInfo.clientHost.length() + 1];
            strcpy(result.clientHosts.clientHosts_val[i], workloadInfo.clientHost.c_str());
            result.clientVMs.clientVMs_val[i] = new char[workloadInfo.clientVM.length() + 1];
            strcpy(result.clientVMs.clientVMs_val[i], workloadInfo.clientVM.c_str());
            result.serverHosts.serverHosts_val[i] = new char[workloadInfo.serverHost.length() + 1];
            strcpy(result.serverHosts.serverHosts_val[i], workloadInfo.serverHost.c_str());
            result.serverVMs.serverVMs_val[i] = new char[workloadInfo.serverVM.length() + 1];
            strcpy(result.serverVMs.serverVMs_val[i], workloadInfo.serverVM.c_str());
        } else {
            result.admitted = false;
            // Revert prior placements
            for (unsigned int j = 0; j < i; j++) {
                removeClient(clientInfos[j]["name"].asString());
                delete[] result.clientHosts.clientHosts_val[j];
                delete[] result.clientVMs.clientVMs_val[j];
                delete[] result.serverHosts.serverHosts_val[j];
                delete[] result.serverVMs.serverVMs_val[j];
            }
            delete[] result.clientHosts.clientHosts_val;
            result.clientHosts.clientHosts_val = NULL;
            result.clientHosts.clientHosts_len = 0;
            delete[] result.clientVMs.clientVMs_val;
            result.clientVMs.clientVMs_val = NULL;
            result.clientVMs.clientVMs_len = 0;
            delete[] result.serverHosts.serverHosts_val;
            result.serverHosts.serverHosts_val = NULL;
            result.serverHosts.serverHosts_len = 0;
            delete[] result.serverVMs.serverVMs_val;
            result.serverVMs.serverVMs_val = NULL;
            result.serverVMs.serverVMs_len = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    result.status = PLACEMENT_SUCCESS;
    return &result;
}

// DelClients RPC - delete a set of workloads from system.
// Assumes RPCs are not multi-threaded
PlacementDelClientsRes* placement_controller_del_clients_svc(PlacementDelClientsArgs* argp, struct svc_req* rqstp)
{
    static PlacementDelClientsRes result;
    pthread_mutex_lock(&g_mutex);
    for (unsigned int i = 0; i < argp->names.names_len; i++) {
        string name(argp->names.names_val[i]);
        removeClient(name);
    }
    pthread_mutex_unlock(&g_mutex);

    result.status = PLACEMENT_SUCCESS;
    return &result;
}

// AddClientVM RPC - add a client VM to system.
// Assumes RPCs are not multi-threaded
PlacementAddClientVMRes* placement_controller_add_client_vm_svc(PlacementAddClientVMArgs* argp, struct svc_req* rqstp)
{
    static PlacementAddClientVMRes result;
    string clientHost(argp->clientHost);
    string clientVM(argp->clientVM);

    pthread_mutex_lock(&g_mutex);
    // Check if clientHost does not exist
    map<string, set<string> >::iterator it = g_clients.find(clientHost);
    if (it == g_clients.end()) {
        // Add network queues to AdmissionController
        for (unsigned int index = 0; index < g_clnts.size(); index++) {
            Json::Value queueInInfo;
            configGenNetworkInQueue(queueInInfo, clientHost);
            g_clnts[index]->addQueue(queueInInfo);
            Json::Value queueOutInfo;
            configGenNetworkOutQueue(queueOutInfo, clientHost);
            g_clnts[index]->addQueue(queueOutInfo);
        }
    }
    // Check if clientVM does not exist (unused)
    set<string>& clientVMs = g_clients[clientHost];
    set<string>::const_iterator it2 = clientVMs.find(clientVM);
    if (it2 == clientVMs.end()) {
        // Check if clientVM does not exist (in use)
        list<WorkloadInfo>::const_iterator it3 = g_workloads.begin();
        while (it3 != g_workloads.end()) {
            if ((it3->clientHost == clientHost) && (it3->clientVM == clientVM)) {
                break;
            }
            it3++;
        }
        if (it3 == g_workloads.end()) {
            clientVMs.insert(clientVM);
            result.status = PLACEMENT_SUCCESS;
        } else {
            result.status = PLACEMENT_ERR_CLIENT_VM_ALREADY_EXISTS;
        }
    } else {
        result.status = PLACEMENT_ERR_CLIENT_VM_ALREADY_EXISTS;
    }
    pthread_mutex_unlock(&g_mutex);
    return &result;
}

// DelClientVM RPC - delete a client VM from system.
// Assumes RPCs are not multi-threaded
PlacementDelClientVMRes* placement_controller_del_client_vm_svc(PlacementDelClientVMArgs* argp, struct svc_req* rqstp)
{
    static PlacementDelClientVMRes result;
    string clientHost(argp->clientHost);
    string clientVM(argp->clientVM);

    pthread_mutex_lock(&g_mutex);
    // Check if clientHost exists
    map<string, set<string> >::iterator it = g_clients.find(clientHost);
    if (it != g_clients.end()) {
        // Check if clientVM exists
        set<string>& clientVMs = it->second;
        set<string>::const_iterator it2 = clientVMs.find(clientVM);
        if (it2 != clientVMs.end()) {
            clientVMs.erase(it2);
            // Check if clientHost has no VMs and is not in use
            if (clientVMs.empty()) {
                list<WorkloadInfo>::const_iterator it3 = g_workloads.begin();
                while (it3 != g_workloads.end()) {
                    if (it3->clientHost == clientHost) {
                        break;
                    }
                    it3++;
                }
                if (it3 == g_workloads.end()) {
                    // Remove network queues from AdmissionController
                    for (unsigned int index = 0; index < g_clnts.size(); index++) {
                        g_clnts[index]->delQueue(getQueueInName(clientHost));
                        g_clnts[index]->delQueue(getQueueOutName(clientHost));
                    }
                    g_clients.erase(it);
                }
            }
            result.status = PLACEMENT_SUCCESS;
        } else {
            result.status = PLACEMENT_ERR_CLIENT_VM_NONEXISTENT;
        }
    } else {
        result.status = PLACEMENT_ERR_CLIENT_VM_NONEXISTENT;
    }
    pthread_mutex_unlock(&g_mutex);
    return &result;
}

// AddServerVM RPC - add a server VM to system.
// Assumes RPCs are not multi-threaded
PlacementAddServerVMRes* placement_controller_add_server_vm_svc(PlacementAddServerVMArgs* argp, struct svc_req* rqstp)
{
    static PlacementAddServerVMRes result;
    string serverHost(argp->serverHost);
    string serverVM(argp->serverVM);

    pthread_mutex_lock(&g_mutex);
    // Check if serverHost does not exist
    map<string, set<string> >::iterator it = g_servers.find(serverHost);
    if (it == g_servers.end()) {
        // Add network queues to AdmissionController
        for (unsigned int index = 0; index < g_clnts.size(); index++) {
            Json::Value queueInInfo;
            configGenNetworkInQueue(queueInInfo, serverHost);
            g_clnts[index]->addQueue(queueInInfo);
            Json::Value queueOutInfo;
            configGenNetworkOutQueue(queueOutInfo, serverHost);
            g_clnts[index]->addQueue(queueOutInfo);
        }
    }
    // Check if serverVM does not exist
    set<string>& serverVMs = g_servers[serverHost];
    set<string>::const_iterator it2 = serverVMs.find(serverVM);
    if (it2 == serverVMs.end()) {
        // Add storage queue to AdmissionController
        for (unsigned int index = 0; index < g_clnts.size(); index++) {
            Json::Value queueStorageInfo;
            configGenStorageQueue(queueStorageInfo, getServerName(serverHost, serverVM));
            g_clnts[index]->addQueue(queueStorageInfo);
        }
        serverVMs.insert(serverVM);
        result.status = PLACEMENT_SUCCESS;
    } else {
        result.status = PLACEMENT_ERR_SERVER_VM_ALREADY_EXISTS;
    }
    pthread_mutex_unlock(&g_mutex);
    return &result;
}

// DelServerVM RPC - delete a server VM from system.
// Assumes RPCs are not multi-threaded
PlacementDelServerVMRes* placement_controller_del_server_vm_svc(PlacementDelServerVMArgs* argp, struct svc_req* rqstp)
{
    static PlacementDelServerVMRes result;
    string serverHost(argp->serverHost);
    string serverVM(argp->serverVM);

    pthread_mutex_lock(&g_mutex);
    // Check if serverHost exists
    map<string, set<string> >::iterator it = g_servers.find(serverHost);
    if (it != g_servers.end()) {
        // Check if serverVM exists
        set<string>& serverVMs = it->second;
        set<string>::const_iterator it2 = serverVMs.find(serverVM);
        if (it2 != serverVMs.end()) {
            // Check if server is not in use
            list<WorkloadInfo>::const_iterator it3 = g_workloads.begin();
            while (it3 != g_workloads.end()) {
                if ((it3->serverHost == serverHost) && (it3->serverVM == serverVM)) {
                    break;
                }
                it3++;
            }
            if (it3 == g_workloads.end()) {
                // Remove storage queue from AdmissionController
                for (unsigned int index = 0; index < g_clnts.size(); index++) {
                    g_clnts[index]->delQueue(getServerName(serverHost, serverVM));
                }
                serverVMs.erase(it2);
                if (serverVMs.empty()) {
                    // Remove network queues from AdmissionController
                    for (unsigned int index = 0; index < g_clnts.size(); index++) {
                        g_clnts[index]->delQueue(getQueueInName(serverHost));
                        g_clnts[index]->delQueue(getQueueOutName(serverHost));
                    }
                    g_servers.erase(it);
                }
                result.status = PLACEMENT_SUCCESS;
            } else {
                result.status = PLACEMENT_ERR_SERVER_VM_IN_USE;
            }
        } else {
            result.status = PLACEMENT_ERR_SERVER_VM_NONEXISTENT;
        }
    } else {
        result.status = PLACEMENT_ERR_SERVER_VM_NONEXISTENT;
    }
    pthread_mutex_unlock(&g_mutex);
    return &result;
}

// Main RPC handler
void placement_controller_program(struct svc_req* rqstp, register SVCXPRT* transp)
{
    union {
        PlacementAddClientsArgs placement_controller_add_clients_arg;
        PlacementDelClientsArgs placement_controller_del_clients_arg;
        PlacementAddClientVMArgs placement_controller_add_client_vm_arg;
        PlacementDelClientVMArgs placement_controller_del_client_vm_arg;
        PlacementAddServerVMArgs placement_controller_add_server_vm_arg;
        PlacementDelServerVMArgs placement_controller_del_server_vm_arg;
    } argument;
    char* result;
    xdrproc_t _xdr_argument, _xdr_result;
    char* (*local)(char*, struct svc_req*);

    switch (rqstp->rq_proc) {
        case PLACEMENT_CONTROLLER_NULL:
            svc_sendreply(transp, (xdrproc_t)xdr_void, (caddr_t)NULL);
            return;

        case PLACEMENT_CONTROLLER_ADD_CLIENTS:
            _xdr_argument = (xdrproc_t)xdr_PlacementAddClientsArgs;
            _xdr_result = (xdrproc_t)xdr_PlacementAddClientsRes;
            local = (char* (*)(char*, struct svc_req*))placement_controller_add_clients_svc;
            break;

        case PLACEMENT_CONTROLLER_DEL_CLIENTS:
            _xdr_argument = (xdrproc_t)xdr_PlacementDelClientsArgs;
            _xdr_result = (xdrproc_t)xdr_PlacementDelClientsRes;
            local = (char* (*)(char*, struct svc_req*))placement_controller_del_clients_svc;
            break;

        case PLACEMENT_CONTROLLER_ADD_CLIENT_VM:
            _xdr_argument = (xdrproc_t)xdr_PlacementAddClientVMArgs;
            _xdr_result = (xdrproc_t)xdr_PlacementAddClientVMRes;
            local = (char* (*)(char*, struct svc_req*))placement_controller_add_client_vm_svc;
            break;

        case PLACEMENT_CONTROLLER_DEL_CLIENT_VM:
            _xdr_argument = (xdrproc_t)xdr_PlacementDelClientVMArgs;
            _xdr_result = (xdrproc_t)xdr_PlacementDelClientVMRes;
            local = (char* (*)(char*, struct svc_req*))placement_controller_del_client_vm_svc;
            break;

        case PLACEMENT_CONTROLLER_ADD_SERVER_VM:
            _xdr_argument = (xdrproc_t)xdr_PlacementAddServerVMArgs;
            _xdr_result = (xdrproc_t)xdr_PlacementAddServerVMRes;
            local = (char* (*)(char*, struct svc_req*))placement_controller_add_server_vm_svc;
            break;

        case PLACEMENT_CONTROLLER_DEL_SERVER_VM:
            _xdr_argument = (xdrproc_t)xdr_PlacementDelServerVMArgs;
            _xdr_result = (xdrproc_t)xdr_PlacementDelServerVMRes;
            local = (char* (*)(char*, struct svc_req*))placement_controller_del_server_vm_svc;
            break;

        default:
            svcerr_noproc(transp);
            return;
    }
    memset((char*)&argument, 0, sizeof(argument));
    if (!svc_getargs(transp, (xdrproc_t)_xdr_argument, (caddr_t)&argument)) {
        svcerr_decode(transp);
        return;
    }
    result = (*local)((char*)&argument, rqstp);
    if (result != NULL && !svc_sendreply(transp, (xdrproc_t)_xdr_result, result)) {
        svcerr_systemerr(transp);
    }
    if (!svc_freeargs(transp, (xdrproc_t)_xdr_argument, (caddr_t)&argument)) {
        cerr << "Unable to free arguments" << endl;
    }
}

int main(int argc, char** argv)
{
    int opt = 0;
    do {
        opt = getopt(argc, argv, "a:f");
        switch (opt) {
            case 'a':
                g_clnts.push_back(new AdmissionController_clnt(optarg));
                break;

            case 'f':
                g_fastFirstFit = true;
                break;

            case -1:
                break;

            default:
                break;
        }
    } while (opt != -1);

    if (g_clnts.empty()) {
        cout << "Usage: " << argv[0] << " -a AdmissionControllerAddr [-a AdmissionControllerAddr ...] [-f]" << endl;
        return -1;
    }

    // Unregister PlacementController RPC handlers
    pmap_unset(PLACEMENT_CONTROLLER_PROGRAM, PLACEMENT_CONTROLLER_V1);

    // Replace tcp RPC handlers
    register SVCXPRT *transp;
    transp = svctcp_create(RPC_ANYSOCK, 0, 0);
    if (transp == NULL) {
        cerr << "Failed to create tcp service" << endl;
        return 1;
    }
    if (!svc_register(transp, PLACEMENT_CONTROLLER_PROGRAM, PLACEMENT_CONTROLLER_V1, placement_controller_program, IPPROTO_TCP)) {
        cerr << "Failed to register tcp PlacementController" << endl;
        return 1;
    }

    // Create worker threads
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t* threadArray = new pthread_t[g_clnts.size()];
    for (unsigned int i = 0; i < g_clnts.size(); i++) {
        int rc = pthread_create(&threadArray[i],
                                &attr,
                                workerThread,
                                reinterpret_cast<void*>(g_clnts[i]));
        if (rc) {
            cerr << "Error creating thread: " << rc << " errno: " << errno << endl;
            exit(-1);
        }
    }

    // Run proxy
    svc_run();
    cerr << "svc_run returned" << endl;
    delete[] threadArray;
    for (unsigned int i = 0; i < g_clnts.size(); i++) {
        delete g_clnts[i];
    }
    return 1;
}
