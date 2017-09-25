// WorkloadCompactor.hpp - Class definitions for WorkloadCompactor's rate limit parameter optimization.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef WORKLOAD_COMPACTOR_HPP
#define WORKLOAD_COMPACTOR_HPP

#include <vector>
#include <set>
#include "DNC.hpp"

using namespace std;

class WorkloadCompactor : public DNC
{
private:
    set<QueueId> _affectedQueueIds; // track queues affected by adding/deleting workloads that need to be re-optimized

    // WorkloadCompactor's linear program for optimizing rate limit parameters at each queue.
    // See WorkloadCompactor paper for details.
    bool calcShaperParameters();

public:
    WorkloadCompactor()
    {}
    virtual ~WorkloadCompactor()
    {}

    virtual double calcFlowLatency(FlowId flowId);

    virtual ClientId addClient(const Json::Value& clientInfo);
    virtual void delClient(ClientId clientId);
};

#endif // WORKLOAD_COMPACTOR_HPP
