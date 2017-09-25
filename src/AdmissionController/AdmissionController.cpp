// AdmissionController.cpp - admission control server.
// Performs admission control for networked storage based on Deterministic Network Calculus (DNC) using DNC-Library.
// When a workload (a.k.a. client) seeks admission at a particular server, its rate limit parameters are optimized using WorkloadCompactor's linear program to try to pack as many workloads onto the server as possible.
// Once rate limit parameters have been (re)configured for each of the workloads sharing the server with the new workload, then the admission controller checks that the new workload's worst-case latency, as calculated using DNC, is less than the workload's SLO.
// Additionally, the other workloads that are affected by the new workload are checked to see if they meet their SLOs.
// If the new workload and affected workloads meet their SLOs, then the new workload is admitted.
// Otherwise, the workload is rejected at the particular server, but other servers can be tested to see if the workload fits (see PlacementController, which tries placements on each of the servers until it finds one where the workload will fit).
// If admitted, then the storage (NFSEnforcer) and network (NetEnforcer) enforcers will be updated with the workload rate limits and priorities if the following are set in the workload's flow:
// "enforcerAddr" - address where the enforcer is running
// "enforcerType" - "network" or "storage"
// "dstAddr" (network) - destination address of flow
// "srcAddr" (network) - source address of flow
// "clientAddr" (storage) - address of the client sending requests
// Priority is determined with the BySLO policy where the tightest SLO is assigned the highest priority.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <cassert>
#include <iostream>
#include <string>
#include <map>
#include <set>
#include <unistd.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <json/json.h>
#include "../prot/AdmissionController_prot.h"
#include "../prot/net_clnt.hpp"
#include "../prot/storage_clnt.hpp"
#include "../common/common.hpp"
#include "../DNC-Library/NC.hpp"
#include "../DNC-Library/NCConfig.hpp"
#include "../DNC-Library/WorkloadCompactor.hpp"

using namespace std;

// Global network calculus calculator
NC* nc = NULL;

// Global storage for clientInfos
map<ClientId, Json::Value> clientInfoStore;

// Send RPC to NetEnforcer to update workload
void updateNetEnforcerClient(Json::Value& flowInfo)
{
    if (!flowInfo.isMember("enforcerAddr") || !flowInfo.isMember("dstAddr") || !flowInfo.isMember("srcAddr")) {
        return;
    }
    net_clnt clnt(flowInfo["enforcerAddr"].asString());
    setFlowParameters(flowInfo, nc);
    clnt.updateClient(flowInfo);
}

// Send RPC to NetEnforcer to remove workload
void removeNetEnforcerClient(Json::Value& flowInfo)
{
    if (!flowInfo.isMember("enforcerAddr") || !flowInfo.isMember("dstAddr") || !flowInfo.isMember("srcAddr")) {
        return;
    }
    net_clnt clnt(flowInfo["enforcerAddr"].asString());
    clnt.removeClient(flowInfo);
}

// Send RPC to NFSEnforcer to update workload
void updateNFSEnforcerClient(Json::Value& flowInfo)
{
    if (!flowInfo.isMember("enforcerAddr") || !flowInfo.isMember("clientAddr")) {
        return;
    }
    storage_clnt clnt(flowInfo["enforcerAddr"].asString());
    setFlowParameters(flowInfo, nc);
    clnt.updateClient(flowInfo);
}

// Send RPC to NFSEnforcer to remove workload
void removeNFSEnforcerClient(Json::Value& flowInfo)
{
    if (!flowInfo.isMember("enforcerAddr") || !flowInfo.isMember("clientAddr")) {
        return;
    }
    storage_clnt clnt(flowInfo["enforcerAddr"].asString());
    flowInfo["priority"] = Json::Value(0);
    flowInfo.removeMember("rateLimiters");
    clnt.updateClient(flowInfo);
}

// Check the JSON flowInfo format.
// Returns error for invalid arguments.
AdmissionStatus checkFlowInfo(set<string>& flowNames, const Json::Value& flowInfo)
{
    // Check name
    if (!flowInfo.isMember("name")) {
        return ADMISSION_ERR_MISSING_ARGUMENT;
    }
    string name = flowInfo["name"].asString();
    if (nc->getFlowIdByName(name) != InvalidFlowId) {
        return ADMISSION_ERR_FLOW_NAME_IN_USE;
    }
    if (flowNames.find(name) != flowNames.end()) {
        return ADMISSION_ERR_FLOW_NAME_IN_USE;
    }
    flowNames.insert(name);
    // Check queues
    if (!flowInfo.isMember("queues")) {
        return ADMISSION_ERR_MISSING_ARGUMENT;
    }
    const Json::Value& flowQueues = flowInfo["queues"];
    if (!flowQueues.isArray()) {
        return ADMISSION_ERR_INVALID_ARGUMENT;
    }
    for (unsigned int index = 0; index < flowQueues.size(); index++) {
        string queueName = flowQueues[index].asString();
        if (nc->getQueueIdByName(queueName) == InvalidQueueId) {
            return ADMISSION_ERR_QUEUE_NAME_NONEXISTENT;
        }
    }
    // Check arrivalInfo
    if (!flowInfo.isMember("arrivalInfo")) {
        return ADMISSION_ERR_MISSING_ARGUMENT;
    }
    return ADMISSION_SUCCESS;
}

// Check the JSON clientInfo format.
// Returns error for invalid arguments.
AdmissionStatus checkClientInfo(set<string>& clientNames, set<string>& flowNames, const Json::Value& clientInfo)
{
    // Check name
    if (!clientInfo.isMember("name")) {
        return ADMISSION_ERR_MISSING_ARGUMENT;
    }
    string name = clientInfo["name"].asString();
    if (nc->getClientIdByName(name) != InvalidClientId) {
        return ADMISSION_ERR_CLIENT_NAME_IN_USE;
    }
    if (clientNames.find(name) != clientNames.end()) {
        return ADMISSION_ERR_CLIENT_NAME_IN_USE;
    }
    clientNames.insert(name);
    // Check SLO
    if (!clientInfo.isMember("SLO")) {
        return ADMISSION_ERR_MISSING_ARGUMENT;
    }
    double SLO = clientInfo["SLO"].asDouble();
    if (SLO <= 0) {
        return ADMISSION_ERR_INVALID_ARGUMENT;
    }
    // Check SLOpercentile
    if (clientInfo.isMember("SLOpercentile")) {
        double SLOpercentile = clientInfo["SLOpercentile"].asDouble();
        if (!((0 < SLOpercentile) && (SLOpercentile < 100))) {
            return ADMISSION_ERR_INVALID_ARGUMENT;
        }
    }
    // Check client's flows
    if (!clientInfo.isMember("flows")) {
        return ADMISSION_ERR_MISSING_ARGUMENT;
    }
    const Json::Value& clientFlows = clientInfo["flows"];
    if (!clientFlows.isArray()) {
        return ADMISSION_ERR_INVALID_ARGUMENT;
    }
    for (unsigned int flowIndex = 0; flowIndex < clientFlows.size(); flowIndex++) {
        AdmissionStatus status = checkFlowInfo(flowNames, clientFlows[flowIndex]);
        if (status != ADMISSION_SUCCESS) {
            return status;
        }
    }
    return ADMISSION_SUCCESS;
}

// Check list of JSON clientInfo format.
// Returns error for invalid arguments.
AdmissionStatus checkClientInfos(const Json::Value& clientInfos)
{
    // Check clientInfos is an array
    if (!clientInfos.isArray()) {
        return ADMISSION_ERR_INVALID_ARGUMENT;
    }
    set<string> clientNames; // ensure no duplicate names
    set<string> flowNames; // ensure no duplicate names
    for (unsigned int i = 0; i < clientInfos.size(); i++) {
        AdmissionStatus status = checkClientInfo(clientNames, flowNames, clientInfos[i]);
        if (status != ADMISSION_SUCCESS) {
            return status;
        }
    }
    return ADMISSION_SUCCESS;
}

// FlowIndex less-than function
bool operator< (const FlowIndex& fi1, const FlowIndex& fi2)
{
    if (fi1.flowId == fi2.flowId) {
        return (fi1.index < fi2.index);
    }
    return (fi1.flowId < fi2.flowId);
}

// Mark flows affected at a priority level starting from a flow at a given index.
void markAffectedFlows(set<FlowIndex>& affectedFlows, const FlowIndex& fi, unsigned int priority)
{
    const Flow* f = nc->getFlow(fi.flowId);
    // If f is higher priority, it is unaffected
    if (f->priority < priority) {
        return;
    }
    // If we've already marked flow at given index, stop
    if (affectedFlows.find(fi) != affectedFlows.end()) {
        return;
    }
    affectedFlows.insert(fi);
    // Loop through queues affected by flow starting at index
    for (unsigned int index = fi.index; index < f->queueIds.size(); index++) {
        const Queue* q = nc->getQueue(f->queueIds[index]);
        // Try marking other flows sharing queue
        for (vector<FlowIndex>::const_iterator itFi = q->flows.begin(); itFi != q->flows.end(); itFi++) {
            markAffectedFlows(affectedFlows, *itFi, f->priority);
        }
    }
}

// Check latency of added clients
bool checkLatency(const set<ClientId>& clientIds)
{
    bool admitted = true;
    set<FlowIndex> affectedFlows;
    for (set<ClientId>::const_iterator it = clientIds.begin(); it != clientIds.end(); it++) {
        ClientId clientId = *it;
        nc->calcClientLatency(clientId);
        const Client* c = nc->getClient(clientId);
        if (c->latency > c->SLO) {
            admitted = false;
            break;
        }
        // Add affected flows
        for (vector<FlowId>::const_iterator itF = c->flowIds.begin(); itF != c->flowIds.end(); itF++) {
            FlowIndex fi;
            fi.flowId = *itF;
            fi.index = 0;
            markAffectedFlows(affectedFlows, fi, 0);
        }
    }
    if (admitted) {
        // Get clientIds of affected flows
        set<ClientId> affectedClientIds;
        for (set<FlowIndex>::const_iterator it = affectedFlows.begin(); it != affectedFlows.end(); it++) {
            const Flow* f = nc->getFlow(it->flowId);
            affectedClientIds.insert(f->clientId);
        }
        // Check latency of other affected clients
        for (set<ClientId>::const_iterator it = affectedClientIds.begin(); it != affectedClientIds.end(); it++) {
            ClientId clientId = *it;
            if (clientIds.find(clientId) == clientIds.end()) {
                nc->calcClientLatency(clientId);
                const Client* c = nc->getClient(clientId);
                if (c->latency > c->SLO) {
                    admitted = false;
                    break;
                }
            }
        }
    }
    return admitted;
}

// Check if we should exit early since server is full
bool checkOverload(const Json::Value& clientInfos)
{
    bool possibleOverload = false;
    DNC* dnc = dynamic_cast<DNC*>(nc);
    if (dnc) {
        for (unsigned int i = 0; i < clientInfos.size(); i++) {
            const Json::Value& clientInfo = clientInfos[i];
            if (clientInfo.isMember("admitted") && clientInfo["admitted"].asBool()) {
                // Must skip checking admitted clients, since they may require shaper curve recomputation
                continue;
            }
            const Json::Value& clientFlows = clientInfo["flows"];
            for (unsigned int flowIndex = 0; flowIndex < clientFlows.size(); flowIndex++) {
                const Json::Value& flowInfo = clientFlows[flowIndex];
                Curve arrivalCurve;
                deserializeJSON(flowInfo, "arrivalInfo", arrivalCurve);
                const Json::Value& flowQueues = flowInfo["queues"];
                for (unsigned int index = 0; index < flowQueues.size(); index++) {
                    string queueName = flowQueues[index].asString();
                    QueueId queueId = nc->getQueueIdByName(queueName);
                    const Queue* queue = nc->getQueue(queueId);
                    double load = arrivalCurve.back().slope;
                    for (vector<FlowIndex>::const_iterator it = queue->flows.begin(); it != queue->flows.end(); it++) {
                        if (dnc) {
                            SimpleArrivalCurve shaperCurve = dnc->getShaperCurve(it->flowId);
                            if ((shaperCurve.r == 0) && (shaperCurve.b == 0)) {
                                // Uninitialized shaper curves require recomputation
                                return false;
                            } else {
                                load += shaperCurve.r;
                            }
                        }
                    }
                    if (load > 0.999999 * queue->bandwidth) {
                        possibleOverload = true;
                    }
                }
            }
        }
    }
    return possibleOverload;
}

// AddClients RPC - performs admission control check on a set of clients and adds clients to system if admitted.
// Assumes RPCs are not multi-threaded
AdmissionAddClientsRes* admission_controller_add_clients_svc(AdmissionAddClientsArgs* argp, struct svc_req* rqstp)
{
    static AdmissionAddClientsRes result;
    // Initialize result
    result.admitted = true;
    result.status = ADMISSION_SUCCESS;
    // Parse input
    Json::Value clientInfos;
    if (!stringToJson(argp->clientInfos, clientInfos)) {
        result.status = ADMISSION_ERR_INVALID_ARGUMENT;
        result.admitted = false;
        return &result;
    }
    // Check parameters
    result.status = checkClientInfos(clientInfos);
    if (result.status != ADMISSION_SUCCESS) {
        result.admitted = false;
        return &result;
    }
    // Check fast first fit
    if (argp->fastFirstFit) {
        // Check overload
        if (checkOverload(clientInfos)) {
            result.admitted = false;
            return &result;
        }
    }
    // Add clients
    set<ClientId> clientIds;
    for (unsigned int i = 0; i < clientInfos.size(); i++) {
        const Json::Value& clientInfo = clientInfos[i];
        ClientId clientId = nc->addClient(clientInfo);
        clientIds.insert(clientId);
        clientInfoStore[clientId] = clientInfo;
    }
    if (result.status == ADMISSION_SUCCESS) {
        bool admitOverride = true;
        for (unsigned int i = 0; i < clientInfos.size(); i++) {
            const Json::Value& clientInfo = clientInfos[i];
            if (!clientInfo.isMember("admitted") || !clientInfo["admitted"].asBool()) {
                admitOverride = false;
                break;
            }
        }
        if (!admitOverride) {
            // Check latency of added clients
            result.admitted = checkLatency(clientIds);
        }
    }
    if (result.admitted) {
        // Send RPC to NetEnforcer/NFSEnforcer to update client
        for (unsigned int i = 0; i < clientInfos.size(); i++) {
            Json::Value& clientInfo = clientInfos[i];
            Json::Value& clientFlows = clientInfo["flows"];
            for (unsigned int flowIndex = 0; flowIndex < clientFlows.size(); flowIndex++) {
                Json::Value& flowInfo = clientFlows[flowIndex];
                if (flowInfo.isMember("enforcerType")) {
                    if (flowInfo["enforcerType"].asString() == "network") {
                        updateNetEnforcerClient(flowInfo);
                    } else if (flowInfo["enforcerType"].asString() == "storage") {
                        updateNFSEnforcerClient(flowInfo);
                    }
                }
            }
        }
    } else {
        // Delete clients
        for (set<ClientId>::const_iterator it = clientIds.begin(); it != clientIds.end(); it++) {
            ClientId clientId = *it;
            clientInfoStore.erase(clientId);
            nc->delClient(clientId);
        }
    }
    return &result;
}

// DelClient RPC - delete a client from system.
// Assumes RPCs are not multi-threaded
AdmissionDelClientRes* admission_controller_del_client_svc(AdmissionDelClientArgs* argp, struct svc_req* rqstp)
{
    static AdmissionDelClientRes result;
    string name(argp->name);
    ClientId clientId = nc->getClientIdByName(name);
    // Check that client exists
    if (clientId == InvalidClientId) {
        result.status = ADMISSION_ERR_CLIENT_NAME_NONEXISTENT;
        return &result;
    }
    // Send RPC to NetEnforcer/NFSEnforcer to remove client
    assert(clientInfoStore.find(clientId) != clientInfoStore.end());
    Json::Value& clientInfo = clientInfoStore[clientId];
    Json::Value& clientFlows = clientInfo["flows"];
    for (unsigned int flowIndex = 0; flowIndex < clientFlows.size(); flowIndex++) {
        Json::Value& flowInfo = clientFlows[flowIndex];
        if (flowInfo.isMember("enforcerType")) {
            if (flowInfo["enforcerType"].asString() == "network") {
                removeNetEnforcerClient(flowInfo);
            } else if (flowInfo["enforcerType"].asString() == "storage") {
                removeNFSEnforcerClient(flowInfo);
            }
        }
    }
    // Delete client
    clientInfoStore.erase(clientId);
    nc->delClient(clientId);
    result.status = ADMISSION_SUCCESS;
    return &result;
}

// AddQueue RPC - add a queue to system.
// Assumes RPCs are not multi-threaded
AdmissionAddQueueRes* admission_controller_add_queue_svc(AdmissionAddQueueArgs* argp, struct svc_req* rqstp)
{
    static AdmissionAddQueueRes result;
    // Parse input
    Json::Value queueInfo;
    if (!stringToJson(argp->queueInfo, queueInfo)) {
        result.status = ADMISSION_ERR_INVALID_ARGUMENT;
        return &result;
    }
    // Check for valid name
    if (!queueInfo.isMember("name")) {
        result.status = ADMISSION_ERR_MISSING_ARGUMENT;
        return &result;
    }
    if (nc->getQueueIdByName(queueInfo["name"].asString()) != InvalidQueueId) {
        result.status = ADMISSION_ERR_QUEUE_NAME_IN_USE;
        return &result;
    }
    // Check for valid bandwidth
    if (!queueInfo.isMember("bandwidth")) {
        result.status = ADMISSION_ERR_MISSING_ARGUMENT;
        return &result;
    }
    if (queueInfo["bandwidth"].asDouble() <= 0) {
        result.status = ADMISSION_ERR_INVALID_ARGUMENT;
        return &result;
    }
    // Add queue
    nc->addQueue(queueInfo);
    result.status = ADMISSION_SUCCESS;
    return &result;
}

// DelQueue RPC - delete a queue from system.
// Assumes RPCs are not multi-threaded
AdmissionDelQueueRes* admission_controller_del_queue_svc(AdmissionDelQueueArgs* argp, struct svc_req* rqstp)
{
    static AdmissionDelQueueRes result;
    string name(argp->name);
    QueueId queueId = nc->getQueueIdByName(name);
    // Check that queue exists
    if (queueId == InvalidQueueId) {
        result.status = ADMISSION_ERR_QUEUE_NAME_NONEXISTENT;
        return &result;
    }
    // Check that queue is empty
    const Queue* q = nc->getQueue(queueId);
    assert(q != NULL);
    if (!q->flows.empty()) {
        result.status = ADMISSION_ERR_QUEUE_HAS_ACTIVE_FLOWS;
        return &result;
    }
    // Delete queue
    nc->delQueue(queueId);
    result.status = ADMISSION_SUCCESS;
    return &result;
}

// Main RPC handler
void admission_controller_program(struct svc_req* rqstp, register SVCXPRT* transp)
{
    union {
        AdmissionAddClientsArgs admission_controller_add_clients_arg;
        AdmissionDelClientArgs admission_controller_del_client_arg;
        AdmissionAddQueueArgs admission_controller_add_queue_arg;
        AdmissionDelQueueArgs admission_controller_del_queue_arg;
    } argument;
    char* result;
    xdrproc_t _xdr_argument, _xdr_result;
    char* (*local)(char*, struct svc_req*);

    switch (rqstp->rq_proc) {
        case ADMISSION_CONTROLLER_NULL:
            svc_sendreply(transp, (xdrproc_t)xdr_void, (caddr_t)NULL);
            return;

        case ADMISSION_CONTROLLER_ADD_CLIENTS:
            _xdr_argument = (xdrproc_t)xdr_AdmissionAddClientsArgs;
            _xdr_result = (xdrproc_t)xdr_AdmissionAddClientsRes;
            local = (char* (*)(char*, struct svc_req*))admission_controller_add_clients_svc;
            break;

        case ADMISSION_CONTROLLER_DEL_CLIENT:
            _xdr_argument = (xdrproc_t)xdr_AdmissionDelClientArgs;
            _xdr_result = (xdrproc_t)xdr_AdmissionDelClientRes;
            local = (char* (*)(char*, struct svc_req*))admission_controller_del_client_svc;
            break;

        case ADMISSION_CONTROLLER_ADD_QUEUE:
            _xdr_argument = (xdrproc_t)xdr_AdmissionAddQueueArgs;
            _xdr_result = (xdrproc_t)xdr_AdmissionAddQueueRes;
            local = (char* (*)(char*, struct svc_req*))admission_controller_add_queue_svc;
            break;

        case ADMISSION_CONTROLLER_DEL_QUEUE:
            _xdr_argument = (xdrproc_t)xdr_AdmissionDelQueueArgs;
            _xdr_result = (xdrproc_t)xdr_AdmissionDelQueueRes;
            local = (char* (*)(char*, struct svc_req*))admission_controller_del_queue_svc;
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
    // Create NC
    nc = new WorkloadCompactor();

    // Unregister AdmissionController RPC handlers
    pmap_unset(ADMISSION_CONTROLLER_PROGRAM, ADMISSION_CONTROLLER_V1);

    // Replace tcp RPC handlers
    register SVCXPRT *transp;
    transp = svctcp_create(RPC_ANYSOCK, 0, 0);
    if (transp == NULL) {
        cerr << "Failed to create tcp service" << endl;
        delete nc;
        return 1;
    }
    if (!svc_register(transp, ADMISSION_CONTROLLER_PROGRAM, ADMISSION_CONTROLLER_V1, admission_controller_program, IPPROTO_TCP)) {
        cerr << "Failed to register tcp AdmissionController" << endl;
        delete nc;
        return 1;
    }

    // Run proxy
    svc_run();
    cerr << "svc_run returned" << endl;
    delete nc;
    return 1;
}
