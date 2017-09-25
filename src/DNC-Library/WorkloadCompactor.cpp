// WorkloadCompactor.cpp - Code for WorkloadCompactor's rate limit parameter optimization.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <limits>
#include <vector>
#include <set>
#include <map>
#include <cassert>
#include "Solver.hpp"
#include "NC.hpp"
#include "DNC.hpp"
#include "WorkloadCompactor.hpp"

using namespace std;

// WorkloadCompactor's linear program for optimizing rate limit parameters at each queue.
// See WorkloadCompactor paper for details.
bool WorkloadCompactor::calcShaperParameters()
{
    bool result = true;
    // Partition clients into groups
    vector<set<ClientId> > clientGroups;
    set<QueueId> remainingQueueIds;
    for (map<QueueId, Queue*>::const_iterator it = queuesBegin(); it != queuesEnd(); it++) {
        remainingQueueIds.insert(it->first);
    }
    while (!_affectedQueueIds.empty()) {
        QueueId firstQueueId = *(_affectedQueueIds.begin());
        remainingQueueIds.erase(firstQueueId);
        _affectedQueueIds.erase(firstQueueId);
        clientGroups.resize(clientGroups.size() + 1);
        set<ClientId>& clientGroup = clientGroups[clientGroups.size() - 1];
        vector<QueueId> pendingQueueIds(1, firstQueueId);
        while (!pendingQueueIds.empty()) {
            const Queue* queue = getQueue(pendingQueueIds.back());
            pendingQueueIds.pop_back();
            for (vector<FlowIndex>::const_iterator it = queue->flows.begin(); it != queue->flows.end(); it++) {
                const Client* c = getClient(getFlow(it->flowId)->clientId);
                clientGroup.insert(c->clientId);
                for (unsigned int flowIndex = 0; flowIndex < c->flowIds.size(); flowIndex++) {
                    const Flow* f = getFlow(c->flowIds[flowIndex]);
                    for (unsigned int queueIndex = 0; queueIndex < f->queueIds.size(); queueIndex++) {
                        QueueId queueId = f->queueIds[queueIndex];
                        if (remainingQueueIds.erase(queueId) == 1) {
                            _affectedQueueIds.erase(queueId);
                            pendingQueueIds.push_back(queueId);
                        }
                    }
                }
            }
        }
    }
    // Optimize shaper curves
    for (unsigned int clientGroupIndex = 0; clientGroupIndex < clientGroups.size(); clientGroupIndex++) {
        set<ClientId>& clientGroup = clientGroups[clientGroupIndex];
        // Get SLOs in client group
        map<double, unsigned int> SLOs;
        for (set<ClientId>::iterator it = clientGroup.begin(); it != clientGroup.end(); it++) {
            double SLO = getClient(*it)->SLO * 0.999; // avoid rounding errors
            SLOs[SLO] = 0;
        }
        unsigned int priority = 0;
        for (map<double, unsigned int>::iterator it = SLOs.begin(); it != SLOs.end(); it++) {
            it->second = priority;
            priority++;
        }
        // Get paths and queues
        vector<vector<QueueId> > paths;
        map<QueueId, unsigned int> queueIds;
        for (set<ClientId>::iterator it = clientGroup.begin(); it != clientGroup.end(); it++) {
            const Client* c = getClient(*it);
            vector<QueueId> clientPath;
            for (unsigned int flowIndex = 0; flowIndex < c->flowIds.size(); flowIndex++) {
                const Flow* f = getFlow(c->flowIds[flowIndex]);
                QueueId queueId = f->queueIds.front();
                clientPath.push_back(queueId);
                if (queueIds.find(queueId) == queueIds.end()) {
                    unsigned int stageIndex = queueIds.size();
                    queueIds[queueId] = stageIndex;
                }
            }
            unsigned int pathIndex;
            for (pathIndex = 0; pathIndex < paths.size(); pathIndex++) {
                const vector<QueueId>& path = paths[pathIndex];
                if (path.size() == clientPath.size()) {
                    bool found = true;
                    for (unsigned int j = 0; j < path.size(); j++) {
                        if (path[j] != clientPath[j]) {
                            found = false;
                            break;
                        }
                    }
                    if (found) {
                        break;
                    }
                }
            }
            if (pathIndex == paths.size()) {
                paths.push_back(clientPath);
            }
        }
        // Build LP
        SolverGLPK s;
        vector<VariableHandle> rVars;
        vector<VariableHandle> bVars;
        vector<ConstraintExpression> rConstraints(queueIds.size());
        for (unsigned int stageIndex = 0; stageIndex < queueIds.size(); stageIndex++) {
            rConstraints[stageIndex].init(clientGroup.size());
        }
        vector<vector<vector<ConstraintExpression> > > bConstraints(SLOs.size());
        for (unsigned int i = 0; i < SLOs.size(); i++) {
            bConstraints[i].resize(paths.size());
            for (unsigned int pathIndex = 0; pathIndex < paths.size(); pathIndex++) {
                const vector<QueueId>& path = paths[pathIndex];
                bConstraints[i][pathIndex].resize(path.size());
                for (unsigned int j = 0; j < path.size(); j++) {
                    bConstraints[i][pathIndex][j].init((path.size() + 1) * clientGroup.size());
                }
            }
        }
        for (set<ClientId>::iterator it = clientGroup.begin(); it != clientGroup.end(); it++) {
            const Client* c = getClient(*it);
            double SLO = c->SLO * 0.999; // avoid rounding errors
            for (unsigned int flowIndex = 0; flowIndex < c->flowIds.size(); flowIndex++) {
                DNCFlow* f = getDNCFlow(c->flowIds[flowIndex]);
                QueueId queueId = f->queueIds.front();
                // Create rVar, bVar variables
                VariableHandle rVar = s.addVariable(0, 0.999, VAR_CONTINUOUS, NULL); // avoid rounding errors
                VariableHandle bVar = s.addVariable(0, SLO, VAR_CONTINUOUS, NULL);
                rVars.push_back(rVar);
                bVars.push_back(bVar);
                // Append to r and  b constraints
                rConstraints[queueIds[queueId]].append(1, rVar);
                unsigned int i = 0;
                for (map<double, unsigned int>::reverse_iterator rit = SLOs.rbegin(); rit != SLOs.rend(); rit++) {
                    if (rit->first >= SLO) {
                        for (unsigned int pathIndex = 0; pathIndex < paths.size(); pathIndex++) {
                            const vector<QueueId>& path = paths[pathIndex];
                            for (unsigned int j = 0; j < path.size(); j++) {
                                if (path[j] == queueId) {
                                    if (rit->first > SLO) {
                                        bConstraints[i][pathIndex][j].append(1, rVar);
                                    }
                                    for (unsigned int k = 0; k < path.size(); k++) {
                                        bConstraints[i][pathIndex][k].append(1.0 / rit->first, bVar);
                                    }
                                    break;
                                }
                            }
                        }
                        i++;
                    } else {
                        break;
                    }
                }
                // Add arrival curve constraints
                double bw = getQueue(queueId)->bandwidth; // Bandwidth of first queue
                double coeffs[] = {0, 0};
                VariableHandle vars[] = {rVar, bVar};
                const Curve& arrivalCurve = f->arrivalCurve;
                const PointSlope& p1 = arrivalCurve[1];
                double r1 = p1.slope / bw;
                double b1 = yIntercept(p1.x, p1.y, p1.slope) / bw;
                // bVar >= b1
                coeffs[0] = 0; // rVar
                coeffs[1] = 1; // bVar
                s.addConstraint(2, coeffs, vars, CONSTRAINT_GE, b1, NULL);
                for (unsigned int i = 2; i < arrivalCurve.size(); i++) {
                    const PointSlope& p2 = arrivalCurve[i];
                    double r2 = p2.slope / bw;
                    double b2 = yIntercept(p2.x, p2.y, p2.slope) / bw;
                    assert(b2 >= b1);
                    assert(r1 >= r2);
                    // rVar * (b2 - b1) + bVar * (r1 - r2) >= r1 * b2 - r2 * b1
                    coeffs[0] = b2 - b1; // rVar
                    coeffs[1] = r1 - r2; // bVar
                    s.addConstraint(2, coeffs, vars, CONSTRAINT_GE, r1 * b2 - r2 * b1, NULL);
                    r1 = r2;
                    b1 = b2;
                }
                // rVar >= r1
                coeffs[0] = 1; // rVar
                coeffs[1] = 0; // bVar
                s.addConstraint(2, coeffs, vars, CONSTRAINT_GE, r1, NULL);
            }
        }
        // Add r constraints for each stage
        // sum_k r_k <= 1
        for (unsigned int stageIndex = 0; stageIndex < queueIds.size(); stageIndex++) {
            s.addConstraintExpression(rConstraints[stageIndex], CONSTRAINT_LE, 0.999, NULL); // avoid rounding errors
        }
        // Add b constraints for each SLO_i, for each path, for each stage in path
        // [sum_k|SLO_k<=SLO_i,k in path (b_k / SLO_i)] + [sum_k|SLO_k<SLO_i,k==stage (r_k)] <= 1
        for (unsigned int i = 0; i < SLOs.size(); i++) {
            for (unsigned int pathIndex = 0; pathIndex < paths.size(); pathIndex++) {
                const vector<QueueId>& path = paths[pathIndex];
                for (unsigned int j = 0; j < path.size(); j++) {
                    s.addConstraintExpression(bConstraints[i][pathIndex][j], CONSTRAINT_LE, 1, NULL);
                }
            }
        }
        // Add objective function (minimize sum_k r_k)
        s.setObjectiveDirection(OBJECTIVE_MIN);
        for (unsigned int i = 0; i < rVars.size(); i++) {
            s.setObjectiveCoeff(1, rVars[i]);
        }
        // Solve LP
        if (s.solve()) {
            // Extract solution
            unsigned int i = 0;
            for (set<ClientId>::iterator it = clientGroup.begin(); it != clientGroup.end(); it++) {
                const Client* c = getClient(*it);
                double SLO = c->SLO * 0.999; // avoid rounding errors
                for (unsigned int flowIndex = 0; flowIndex < c->flowIds.size(); flowIndex++) {
                    DNCFlow* f = getDNCFlow(c->flowIds[flowIndex]);
                    double bw = getQueue(f->queueIds.front())->bandwidth; // Bandwidth of first queue
                    SimpleArrivalCurve& shaperCurve = f->shaperCurve;
                    shaperCurve.r = s.getSolutionVariable(rVars[i]) * bw;
                    shaperCurve.b = s.getSolutionVariable(bVars[i]) * bw;
                    i++;
                    // Set priority
                    setFlowPriority(f->flowId, SLOs[SLO]);
                }
            }
        } else {
            result = false;
            // Set shaper curve to be uninitialized
            for (set<ClientId>::iterator it = clientGroup.begin(); it != clientGroup.end(); it++) {
                const Client* c = getClient(*it);
                double SLO = c->SLO * 0.999; // avoid rounding errors
                for (unsigned int flowIndex = 0; flowIndex < c->flowIds.size(); flowIndex++) {
                    DNCFlow* f = getDNCFlow(c->flowIds[flowIndex]);
                    f->shaperCurve = ZeroArrivalCurve();
                    // Set priority
                    setFlowPriority(f->flowId, SLOs[SLO]);
                }
            }
        }
    }
    return result;
}

double WorkloadCompactor::calcFlowLatency(FlowId flowId)
{
    // Re-optimize rate limit (i.e., shaper) parameters before calculating latency
    if (!_affectedQueueIds.empty()) {
        calcShaperParameters();
        _affectedQueueIds.clear();
    }
    return DNC::calcFlowLatency(flowId);
}

ClientId WorkloadCompactor::addClient(const Json::Value& clientInfo)
{
    // Add workload
    ClientId clientId = DNC::addClient(clientInfo);
    // Mark queues affected by workload addition
    const Client* c = getClient(clientId);
    for (unsigned int flowIndex = 0; flowIndex < c->flowIds.size(); flowIndex++) {
        const Flow* f = getFlow(c->flowIds[flowIndex]);
        for (unsigned int queueIndex = 0; queueIndex < f->queueIds.size(); queueIndex++) {
            QueueId queueId = f->queueIds[queueIndex];
            _affectedQueueIds.insert(queueId);
        }
    }
    return clientId;
}

void WorkloadCompactor::delClient(ClientId clientId)
{
    // Mark queues affected by workload deletion
    const Client* c = getClient(clientId);
    for (unsigned int flowIndex = 0; flowIndex < c->flowIds.size(); flowIndex++) {
        const Flow* f = getFlow(c->flowIds[flowIndex]);
        for (unsigned int queueIndex = 0; queueIndex < f->queueIds.size(); queueIndex++) {
            QueueId queueId = f->queueIds[queueIndex];
            _affectedQueueIds.insert(queueId);
        }
    }
    // Delete workload
    DNC::delClient(clientId);
}
