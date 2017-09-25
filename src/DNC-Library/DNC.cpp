// DNC.cpp - Code for deterministic network calculus (DNC) algorithms.
// See DNC.hpp for details.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <cassert>
#include <cmath>
#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <set>
#include <limits>
#include <json/json.h>
#include "../TraceCommon/ProcessedTrace.hpp"
#include "../common/time.hpp"
#include "../common/serializeJSON.hpp"
#include "NC.hpp"
#include "DNC.hpp"

using namespace std;

// Calculate the minimum rate needed to sustain a workload (i.e., average rate of work).
double calcMinRate(ProcessedTrace* pTrace)
{
    pTrace->reset();
    // Sum work over trace
    double rate = 0;
    ProcessedTraceEntry traceEntry;
    if (pTrace->nextEntry(traceEntry)) {
        uint64_t firstTimestamp = traceEntry.arrivalTime;
        do {
            rate += traceEntry.work;
        } while (pTrace->nextEntry(traceEntry));
        // Divide by total duration to get average
        double duration = ConvertTimeToSeconds(traceEntry.arrivalTime - firstTimestamp);
        rate /= duration;
    } else {
        cerr << "Empty trace file" << endl;
    }
    return rate;
}

// Calculate the r-b curve for a given workload for a given set of rates.
void rbGen(ProcessedTrace* pTrace, const vector<double>& rates, map<double, double>& bursts)
{
    // reset bursts to 0 just in case it has old or uninitialized data
    map<double, double> virtualBucket;
    for (unsigned int i = 0; i < rates.size(); i++) {
        double rate = rates[i];
        virtualBucket[rate] = 0;
        bursts[rate] = 0;
    }
    // Calculate bursts
    pTrace->reset();
    uint64_t prevTimestamp = 0;
    ProcessedTraceEntry traceEntry;
    while (pTrace->nextEntry(traceEntry)) {
        double interarrival = ConvertTimeToSeconds(traceEntry.arrivalTime - prevTimestamp);
        for (unsigned int i = 0; i < rates.size(); i++) {
            double rate = rates[i];
            // Drain token bucket for time since last request
            virtualBucket[rate] -= rate * interarrival;
            if (virtualBucket[rate] < 0) {
                virtualBucket[rate] = 0;
            }
            // Add tokens for current request
            virtualBucket[rate] += traceEntry.work;
            // Record max burst
            if (virtualBucket[rate] > bursts[rate]) {
                bursts[rate] = virtualBucket[rate];
            }
        }
        prevTimestamp = traceEntry.arrivalTime;
    }
}

// Calculate intersection of two point slopes
// Output slope is the same as first point p1
// Returns p1 if slopes are the same
PointSlope calcPointSlopeIntersection(const PointSlope& p1, const PointSlope& p2)
{
    PointSlope intersectionPoint = p1;
    if (p1.slope != p2.slope) {
        if (isinf(p1.slope)) {
            intersectionPoint.x = p1.x;
            intersectionPoint.y = p2.y - p2.slope * (p2.x - p1.x);
        } else if (isinf(p2.slope)) {
            intersectionPoint.x = p2.x;
            intersectionPoint.y = p1.y - p1.slope * (p1.x - p2.x);
        } else {
            assert(isfinite(p1.slope));
            assert(isfinite(p2.slope));
            // y = m x + b => b = y - m x
            double b1 = yIntercept(p1.x, p1.y, p1.slope);
            double b2 = yIntercept(p2.x, p2.y, p2.slope);
            // y = m1 x + b1 && y = m2 x + b2 => x = (b2 - b1) / (m1 - m2)
            intersectionPoint.x = (b2 - b1) / (p1.slope - p2.slope);
            intersectionPoint.y = p1.slope * intersectionPoint.x + b1;
        }
    }
    return intersectionPoint;
}

// Generate an arrival curve from an r-b curve.
// Assumes rates is decreasing
void rbCurveToArrivalCurve(Curve& arrivalCurve, const vector<double>& rates, map<double, double>& bursts)
{
    // Initialize arrival curve
    PointSlope initialPoint(0, 0, numeric_limits<double>::infinity());
    arrivalCurve.assign(1, initialPoint);
    for (unsigned int i = 0; i < rates.size(); i++) {
        double rate = rates[i];
        PointSlope point(0, bursts[rate], rate);
        while (arrivalCurve.size() > 1) {
            PointSlope& lastPoint = arrivalCurve.back();
            PointSlope intersectionPoint = calcPointSlopeIntersection(point, lastPoint);
            if (intersectionPoint.x > lastPoint.x) {
                point = intersectionPoint;
                break;
            }
            arrivalCurve.pop_back();
        }
        arrivalCurve.push_back(point);
    }
}

// Approximate an arrival curve by an arrival curve with n points.
void pruneArrivalCurve(Curve& arrivalCurve, unsigned int n)
{
    n++; // compensate for the initial (0, 0) point
    // Remove points with high x - they probably don't have much impact unless dealing with high latencys
    const double pruneThreshold = 30;
    while (arrivalCurve.size() > n) {
        if (arrivalCurve.back().x < pruneThreshold) {
            break;
        }
        arrivalCurve.pop_back();
    }
    // Remove points that are close together in the y dimension
    while (arrivalCurve.size() > n) {
        int toRemove = 1;
        double minDiffY = numeric_limits<double>::infinity();
        for (unsigned int i = 1; i < (arrivalCurve.size() - 1); i++) {
            const PointSlope& p1 = arrivalCurve[i];
            const PointSlope& p2 = arrivalCurve[i + 1];
            if (p2.y - p1.y < minDiffY) {
                minDiffY = p2.y - p1.y;
                toRemove = i;
            }
        }
        arrivalCurve.erase(arrivalCurve.begin() + toRemove);
        arrivalCurve[toRemove] = calcPointSlopeIntersection(arrivalCurve[toRemove], arrivalCurve[toRemove - 1]);
    }
}

// Calculate an arrival curve from a trace.
void calcArrivalCurve(Curve& arrivalCurve, ProcessedTrace* pTrace, double maxRate)
{
    double minRate = calcMinRate(pTrace);
    vector<double> rates;
    for (double rate = maxRate; rate >= minRate; rate -= 0.001 * maxRate) {
        rates.push_back(rate);
    }
    map<double, double> bursts;
    rbGen(pTrace, rates, bursts);
    rbCurveToArrivalCurve(arrivalCurve, rates, bursts);
    pruneArrivalCurve(arrivalCurve, 12);
}

// Read an arrival curve from a file.
bool readArrivalCurve(Curve& arrivalCurve, string arrivalCurveFilename)
{
    if (arrivalCurveFilename == "") {
        return false;
    }
    ifstream file(arrivalCurveFilename.c_str());
    if (!file.good()) {
        return false;
    }
    PointSlope p(0, 0, numeric_limits<double>::infinity());
    arrivalCurve.clear();
    arrivalCurve.push_back(p);
    while (file.good()) {
        // Read line
        string line;
        getline(file, line);
        // Parse line
        if (sscanf(line.c_str(), "%lf,%lf,%lf", &p.x, &p.y, &p.slope) == 3) {
            arrivalCurve.push_back(p);
        }
    }
    return true;
}

// Write an arrival curve to a file.
void writeArrivalCurve(const Curve& arrivalCurve, string arrivalCurveFilename)
{
    if (arrivalCurveFilename == "") {
        return;
    }
    ofstream file(arrivalCurveFilename.c_str(), ofstream::out | ofstream::trunc);
    // Set output precision
    file << setprecision(15);
    for (unsigned int i = 1; i < arrivalCurve.size(); i++) {
        const PointSlope& p = arrivalCurve[i];
        file << p.x << "," << p.y << "," << p.slope << endl;
    }
}

// Calculate the worst case latency for a given arrivalCurve and serviceCurve.
// The latency is the x-distance between the arrival and service curves.
// The worst case for these piecewise linear functions must be at one
// of the vertices. Thus, we check for each point in either the arrival
// or service curve, we calculate the distance between the curves.
// For a given point, this is done by calculating the corresponding point
// on the other curve with the same y value.
double calcLatency(const Curve& arrivalCurve, const Curve& serviceCurve)
{
    double maxLatency = 0;
    unsigned int arrivalIndex = 0;
    unsigned int serviceIndex = 0;
    while ((arrivalIndex < arrivalCurve.size()) || (serviceIndex < serviceCurve.size())) {
        double latency;
        double arrivalY = (arrivalIndex < arrivalCurve.size()) ? arrivalCurve[arrivalIndex].y : numeric_limits<double>::infinity();
        double serviceY = (serviceIndex < serviceCurve.size()) ? serviceCurve[serviceIndex].y : numeric_limits<double>::infinity();
        if (arrivalY < serviceY) {
            const PointSlope& arrivalPoint = arrivalCurve[arrivalIndex];
            const PointSlope& servicePoint = serviceCurve[serviceIndex - 1];
            double deltaY = arrivalPoint.y - servicePoint.y;
            double deltaX = deltaY / servicePoint.slope;
            latency = (servicePoint.x + deltaX) - arrivalPoint.x;
            arrivalIndex++;
        } else if (arrivalY > serviceY) {
            const PointSlope& arrivalPoint = arrivalCurve[arrivalIndex - 1];
            const PointSlope& servicePoint = serviceCurve[serviceIndex];
            double deltaY = servicePoint.y - arrivalPoint.y;
            double deltaX = deltaY / arrivalPoint.slope;
            latency = servicePoint.x - (arrivalPoint.x + deltaX);
            serviceIndex++;
        } else {
            const PointSlope& arrivalPoint = arrivalCurve[arrivalIndex];
            const PointSlope& servicePoint = serviceCurve[serviceIndex];
            latency = servicePoint.x - arrivalPoint.x;
            arrivalIndex++;
            serviceIndex++;
        }
        if (latency > maxLatency) {
            maxLatency = latency;
        }
    }
    return maxLatency;
}

// Calculate the latency due to a (r,b) rate limiter (i.e., shaper).
double calcShaperLatency(const Curve& arrivalCurve, const SimpleArrivalCurve& shaperCurve)
{
    Curve serviceCurve;
    PointSlope p(0, 0, numeric_limits<double>::infinity());
    serviceCurve.assign(1, p);
    p.y = shaperCurve.b;
    p.slope = shaperCurve.r;
    serviceCurve.push_back(p);
    return calcLatency(arrivalCurve, serviceCurve);
}

//
// Operators on simple arrival and service curves
//
// DNC operator for an arrival curve with no arrivals.
SimpleArrivalCurve ZeroArrivalCurve()
{
    SimpleArrivalCurve zero;
    zero.r = 0;
    zero.b = 0;
    return zero;
}

// DNC operator for a constant service curve with rate q->bandwidth.
SimpleServiceCurve ConstantServiceCurve(const Queue* q)
{
    SimpleServiceCurve service;
    service.R = q->bandwidth;
    service.T = 0;
    return service;
}

// DNC operator for the aggregation of two arrival curves A and B.
SimpleArrivalCurve AggregateArrivalCurve(const SimpleArrivalCurve& A, const SimpleArrivalCurve& B)
{
    SimpleArrivalCurve sum;
    sum.r = A.r + B.r;
    sum.b = A.b + B.b;
    return sum;
}

// DNC operator for the convolution of two service curves S and T.
SimpleServiceCurve ConvolutionServiceCurve(const SimpleServiceCurve& S, const SimpleServiceCurve& T)
{
    SimpleServiceCurve convolution;
    convolution.R = min(S.R, T.R);
    convolution.T = S.T + T.T;
    return convolution;
}

// DNC operator for the departure  D of an arrival curve A after leaving a queue with service curve S (i.e., D = OutputArrival(A, S)).
// D is thus an arrival curve into the next queue.
SimpleArrivalCurve OutputArrivalCurve(const SimpleArrivalCurve& A, const SimpleServiceCurve& S)
{
    SimpleArrivalCurve output;
    output.r = A.r;
    output.b = A.b + A.r * S.T;
    return output;
}

// DNC operator for the remaining service curve that is leftover once a queue with service curve S has accounted for the behavior of an arrival curve A.
SimpleServiceCurve LeftoverServiceCurve(const SimpleArrivalCurve& A, const SimpleServiceCurve& S)
{
    SimpleServiceCurve leftover;
    leftover.R = S.R - A.r;
    leftover.T = S.T + (A.b + A.r * S.T) / leftover.R;
    return leftover;
}

// Calculates the worst case latency for an arrival curve A experiencing a service curve S.
double DNCLatencyBound(const SimpleArrivalCurve& A, const SimpleServiceCurve& S)
{
    if (A.r > S.R) {
        return numeric_limits<double>::infinity();
    } else {
        return S.T + A.b / S.R;
    }
}

// DNC algorithm that analyzes a flow's latency by considering each queue (a.k.a., "hop") one at a time.
void DNC::calcArrivalCurveAtQueue(const DNCFlow* f, unsigned int index, SimpleArrivalCurve& arrivalCurve)
{
    if (index == 0) {
        arrivalCurve = f->shaperCurve;
    } else {
        SimpleArrivalCurve prevArrivalCurve;
        SimpleServiceCurve prevServiceCurve;
        calcArrivalCurveAtQueue(f, index - 1, prevArrivalCurve);
        calcServiceCurveAtQueue(f, index - 1, prevServiceCurve);
        arrivalCurve = OutputArrivalCurve(prevArrivalCurve, prevServiceCurve);
    }
}
void DNC::calcServiceCurveAtQueue(const DNCFlow* f, unsigned int index, SimpleServiceCurve& serviceCurve)
{
    const Queue* q = getQueue(f->queueIds[index]);
    // Initialize service curve
    serviceCurve = ConstantServiceCurve(q);
    for (vector<FlowIndex>::const_iterator it = q->flows.begin(); it != q->flows.end(); it++) {
        const DNCFlow* flow = getDNCFlow(it->flowId);
        // Only consider other flows of higher (or equal) priority (i.e. <= f->priority)
        if ((flow->priority <= f->priority) && (flow->flowId != f->flowId)) {
            SimpleArrivalCurve arrivalCurve;
            calcArrivalCurveAtQueue(flow, it->index, arrivalCurve);
            serviceCurve = LeftoverServiceCurve(arrivalCurve, serviceCurve);
        }
    }
}
void DNC::hopByHopAnalysis(DNCFlow* flow)
{
    // Calculate queue latencies
    SimpleArrivalCurve arrivalCurve = flow->shaperCurve;
    SimpleServiceCurve serviceCurve;
    double latency = 0;
    for (unsigned int index = 0; index < flow->queueIds.size(); index++) {
        calcServiceCurveAtQueue(flow, index, serviceCurve);
        latency += DNCLatencyBound(arrivalCurve, serviceCurve);
        arrivalCurve = OutputArrivalCurve(arrivalCurve, serviceCurve);
    }
    flow->latency = latency;
}

// DNC algorithm that takes a similar analysis approach as in the SNC-Meister paper, except using DNC operators.
// Currently supported for flows with up to two queues, as is the case when modeling end-host network links.
void DNC::aggregateAnalysisTwoHop(DNCFlow* flow)
{
    assert(flow->queueIds.size() <= 2);
    if (flow->queueIds.size() == 1) {
        //
        // One hop
        //
        // Calculate leftover service from higher priority flows and arrival of flow at first hop
        QueueId firstQueueId = flow->queueIds[0];
        const Queue* firstQueue = getQueue(firstQueueId);
        SimpleArrivalCurve arrivalCurve = ZeroArrivalCurve();
        SimpleServiceCurve serviceCurve = ConstantServiceCurve(firstQueue);
        for (unsigned int i = 0; i < firstQueue->flows.size(); i++) {
            assert(firstQueue->flows[i].index == 0);
            const DNCFlow* f = getDNCFlow(firstQueue->flows[i].flowId);
            assert(f->queueIds[0] == firstQueueId);
            // Only consider flows of higher (or equal) priority (i.e. <= flow->priority)
            if (f->priority <= flow->priority) {
                // Aggregate equal priority flows
                if (f->priority == flow->priority) {
                    arrivalCurve = AggregateArrivalCurve(f->shaperCurve, arrivalCurve);
                } else {
                    serviceCurve = LeftoverServiceCurve(f->shaperCurve, serviceCurve);
                }
            }
        }
        // Calculate latency
        flow->latency = DNCLatencyBound(arrivalCurve, serviceCurve);
    } else if (flow->queueIds.size() == 2) {
        //
        // Two hops
        //
        // Identify ids of first queues that feed into this particular second queue
        QueueId firstQueueId = flow->queueIds[0];
        QueueId secondQueueId = flow->queueIds[1];
        const Queue* secondQueue = getQueue(secondQueueId);
        map<QueueId, unsigned int> firstQueueIds; // first QueueId -> priority
        for (unsigned int i = 0; i < secondQueue->flows.size(); i++) {
            assert(secondQueue->flows[i].index == 1);
            const DNCFlow* f = getDNCFlow(secondQueue->flows[i].flowId);
            assert(f->queueIds[1] == secondQueueId);
            // Exclude first queue
            if (f->queueIds[0] != firstQueueId) {
                // Only consider flows of higher (or equal) priority (i.e. <= flow->priority)
                if (f->priority <= flow->priority) {
                    map<QueueId, unsigned int>::iterator it = firstQueueIds.find(f->queueIds[0]);
                    if (it == firstQueueIds.end()) {
                        firstQueueIds[f->queueIds[0]] = f->priority;
                    // Of the competing high priority flows for a given first queue, identify the lowest priority (i.e., max value)
                    } else if (f->priority > it->second) {
                        it->second = f->priority;
                    }
                }
            }
        }
        // Loop through first queues to calculate second queue leftover service and aggregate arrival
        SimpleServiceCurve secondQueueServiceCurve = ConstantServiceCurve(secondQueue);
        for (map<QueueId, unsigned int>::const_iterator it = firstQueueIds.begin(); it != firstQueueIds.end(); it++) {
            const Queue* q = getQueue(it->first);
            SimpleArrivalCurve firstQueueArrivalCurve = ZeroArrivalCurve();
            SimpleServiceCurve firstQueueServiceCurve = ConstantServiceCurve(q);
            for (unsigned int i = 0; i < q->flows.size(); i++) {
                assert(q->flows[i].index == 0);
                const DNCFlow* f = getDNCFlow(q->flows[i].flowId);
                assert(f->queueIds[0] == it->first);
                // Only consider flows of higher (or equal) priority than the lowest priority competing flow as identified in firstQueueIds
                if (f->priority <= it->second) {
                    // Check if sharing second queue
                    if (f->queueIds[1] == secondQueueId) {
                        firstQueueArrivalCurve = AggregateArrivalCurve(f->shaperCurve, firstQueueArrivalCurve);
                    } else {
                        firstQueueServiceCurve = LeftoverServiceCurve(f->shaperCurve, firstQueueServiceCurve);
                    }
                }
            }
            // Generate output bound on high priority flows that share second queue
            SimpleArrivalCurve outputArrivalCurve = OutputArrivalCurve(firstQueueArrivalCurve, firstQueueServiceCurve);
            // Subtract output from second queue service
            secondQueueServiceCurve = LeftoverServiceCurve(outputArrivalCurve, secondQueueServiceCurve);
        }
        // Calculate first hop service for convolution
        const Queue* firstQueue = getQueue(firstQueueId);
        SimpleArrivalCurve arrivalCurve = ZeroArrivalCurve();
        SimpleArrivalCurve shareArrivalCurve = ZeroArrivalCurve();
        SimpleServiceCurve serviceCurveForConvolution = ConstantServiceCurve(firstQueue);
        for (unsigned int i = 0; i < firstQueue->flows.size(); i++) {
            assert(firstQueue->flows[i].index == 0);
            const DNCFlow* f = getDNCFlow(firstQueue->flows[i].flowId);
            assert(f->queueIds[0] == firstQueueId);
            // Only consider flows of higher (or equal) priority (i.e. <= flow->priority)
            if (f->priority <= flow->priority) {
                // Check if sharing second queue
                if (f->queueIds[1] == secondQueueId) {
                    // Aggregate equal priority flows
                    if (f->priority == flow->priority) {
                        arrivalCurve = AggregateArrivalCurve(f->shaperCurve, arrivalCurve);
                    } else {
                        shareArrivalCurve = AggregateArrivalCurve(f->shaperCurve, shareArrivalCurve);
                    }
                } else {
                    serviceCurveForConvolution = LeftoverServiceCurve(f->shaperCurve, serviceCurveForConvolution);
                }
            }
        }
        // Calculate latency
        SimpleServiceCurve convolutedServiceCurve = ConvolutionServiceCurve(serviceCurveForConvolution, secondQueueServiceCurve);
        SimpleServiceCurve finalService = LeftoverServiceCurve(shareArrivalCurve, convolutedServiceCurve);
        flow->latency = DNCLatencyBound(arrivalCurve, finalService);
    }
}

// Calculate the latency for a flow.
// Assumes priorities are set.
double DNC::calcFlowLatency(FlowId flowId)
{
    DNCFlow* f = getDNCFlow(flowId);
    if (f->ignoreLatency) {
        f->latency = 0;
        return f->latency;
    }
    // Calculate queue latency
    switch (_algorithm) {
        case DNC_SIMPLE_ALGORITHM_AGGREGATE:
            aggregateAnalysisTwoHop(f);
            break;

        case DNC_SIMPLE_ALGORITHM_HOP_BY_HOP:
            hopByHopAnalysis(f);
            break;

        default:
            cerr << "Invalid algorithm " << _algorithm << endl;
            f->latency = 0;
            break;
    }
    // Calculate shaper latency
    f->latency += calcShaperLatency(f->arrivalCurve, f->shaperCurve);
    return f->latency;
}

FlowId DNC::initFlow(Flow* f, const Json::Value& flowInfo, ClientId clientId)
{
    if (f == NULL) {
        f = new DNCFlow;
    }
    FlowId flowId = NC::initFlow(f, flowInfo, clientId);
    // Get DNC parameters
    DNCFlow* dsf = getDNCFlow(flowId);
    deserializeJSON(flowInfo, "arrivalInfo", dsf->arrivalCurve);
    PointSlope initialPoint(0, 0, numeric_limits<double>::infinity());
    dsf->arrivalCurve.insert(dsf->arrivalCurve.begin(), initialPoint);
    // Initialize shaper curve to 0
    dsf->shaperCurve = ZeroArrivalCurve();
    return flowId;
}

void DNC::setArrivalInfo(Json::Value& flowInfo, string trace, const Json::Value& estimatorInfo, double maxRate, string arrivalCurveFilename)
{
    Curve arrivalCurve;
    if (!readArrivalCurve(arrivalCurve, arrivalCurveFilename)) {
        // Init estimator
        Estimator* pEst = Estimator::create(estimatorInfo);
        // Read trace
        ProcessedTrace* pTrace = new ProcessedTrace(trace, pEst);
        calcArrivalCurve(arrivalCurve, pTrace, maxRate);
        delete pTrace;
        writeArrivalCurve(arrivalCurve, arrivalCurveFilename);
    }
    arrivalCurve.erase(arrivalCurve.begin());
    serializeJSON(flowInfo, "arrivalInfo", arrivalCurve);
}
