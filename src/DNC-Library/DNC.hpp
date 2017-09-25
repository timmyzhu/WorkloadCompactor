// DNC.hpp - Class definitions for the deterministic network calculus (DNC) algorithms.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef DNC_HPP
#define DNC_HPP

#include <string>
#include <vector>
#include "../common/serializeJSON.hpp"
#include "../TraceCommon/ProcessedTrace.hpp"
#include "NC.hpp"

using namespace std;

// Represents an (x,y) point and slope.
struct PointSlope : public Serializable {
    PointSlope()
        : x(0),
          y(0),
          slope(0)
    {}
    PointSlope(double initX, double initY, double initSlope)
        : x(initX),
          y(initY),
          slope(initSlope)
    {}
    virtual ~PointSlope()
    {}

    double x;
    double y;
    double slope;

    virtual void serialize(Json::Value& json) const
    {
        serializeJSON(json, "x", x);
        serializeJSON(json, "y", y);
        serializeJSON(json, "slope", slope);
    }
    virtual void deserialize(const Json::Value& json)
    {
        deserializeJSON(json, "x", x);
        deserializeJSON(json, "y", y);
        deserializeJSON(json, "slope", slope);
    }
};

// Represents a piecewise linear curve.
typedef vector<PointSlope> Curve;

// Represents a simple arrival curve corresponding to a single rate limit (r,b) pair.
struct SimpleArrivalCurve {
    double r;
    double b;
};

// Represents a simple service curve corresponding to a rate R with a delay T.
struct SimpleServiceCurve {
    double R;
    double T;
};

// Extends the Flow structure with DNC-specific information.
struct DNCFlow : Flow {
    Curve arrivalCurve;
    SimpleArrivalCurve shaperCurve;
};

enum DNCAlgorithm {
    DNC_SIMPLE_ALGORITHM_AGGREGATE,
    DNC_SIMPLE_ALGORITHM_HOP_BY_HOP,
};

// DNC algorithms for calculating latency.
class DNC : public NC
{
private:
    DNCAlgorithm _algorithm;

    // DNC algorithm that analyzes a flow's latency by considering each queue (a.k.a., "hop") one at a time.
    void calcArrivalCurveAtQueue(const DNCFlow* f, unsigned int index, SimpleArrivalCurve& arrivalCurve);
    void calcServiceCurveAtQueue(const DNCFlow* f, unsigned int index, SimpleServiceCurve& serviceCurve);
    void hopByHopAnalysis(DNCFlow* flow);
    // DNC algorithm that takes a similar analysis approach as in the SNC-Meister paper, except using DNC operators.
    // Currently supported for flows with up to two queues, as is the case when modeling end-host network links.
    void aggregateAnalysisTwoHop(DNCFlow* flow);

protected:
    virtual FlowId initFlow(Flow* f, const Json::Value& flowInfo, ClientId clientId);

    DNCFlow* getDNCFlow(FlowId flowId) { return static_cast<DNCFlow*>(const_cast<Flow*>(getFlow(flowId))); }

public:
    DNC(DNCAlgorithm algorithm = DNC_SIMPLE_ALGORITHM_AGGREGATE)
        : _algorithm(algorithm)
    {}
    virtual ~DNC()
    {}

    // Calculate the latency for a flow.
    // Assumes priorities are set.
    virtual double calcFlowLatency(FlowId flowId);

    // Get the arrival curve representing the flow's behavior.
    const Curve& getArrivalCurve(FlowId flowId) { return getDNCFlow(flowId)->arrivalCurve; }

    // Get/set the shaper curve that representing the flow's (r,b) rate limit parameters.
    const SimpleArrivalCurve& getShaperCurve(FlowId flowId) { return getDNCFlow(flowId)->shaperCurve; }
    void setShaperCurve(FlowId flowId, const SimpleArrivalCurve& shaperCurve) { getDNCFlow(flowId)->shaperCurve = shaperCurve; }

    static void setArrivalInfo(Json::Value& flowInfo, string trace, const Json::Value& estimatorInfo, double maxRate, string arrivalCurveFilename);
};

// Return x-intercept of a line with a given slope passing through (x,y).
inline double xIntercept(double x, double y, double slope)
{
    return x - y / slope;
}
// Return y-intercept of a line with a given slope passing through (x,y).
inline double yIntercept(double x, double y, double slope)
{
    return y - slope * x;
}

// Calculate the minimum rate needed to sustain a workload (i.e., average rate of work).
double calcMinRate(ProcessedTrace* pTrace);
// Calculate the r-b curve for a given workload for a given set of rates.
void rbGen(ProcessedTrace* pTrace, const vector<double>& rates, map<double, double>& bursts);
// Calculate intersection of two point slopes
// Output slope is the same as first point p1
// Returns p1 if slopes are the same
PointSlope calcPointSlopeIntersection(const PointSlope& p1, const PointSlope& p2);
// Generate an arrival curve from an r-b curve.
// Assumes rates is decreasing
void rbCurveToArrivalCurve(Curve& arrivalCurve, const vector<double>& rates, map<double, double>& bursts);
// Approximate an arrival curve by an arrival curve with n points.
void pruneArrivalCurve(Curve& arrivalCurve, unsigned int n);
// Calculate an arrival curve from a trace.
void calcArrivalCurve(Curve& arrivalCurve, ProcessedTrace* pTrace, double maxRate);
// Read an arrival curve from a file.
bool readArrivalCurve(Curve& arrivalCurve, string arrivalCurveFilename);
// Write an arrival curve to a file.
void writeArrivalCurve(const Curve& arrivalCurve, string arrivalCurveFilename);
// Calculate the worst case latency for a given arrivalCurve and serviceCurve.
double calcLatency(const Curve& arrivalCurve, const Curve& serviceCurve);
// Calculate the latency due to a (r,b) rate limiter (i.e., shaper).
double calcShaperLatency(const Curve& arrivalCurve, const SimpleArrivalCurve& shaperCurve);

//
// Operators on simple arrival and service curves
//
// DNC operator for an arrival curve with no arrivals.
SimpleArrivalCurve ZeroArrivalCurve();
// DNC operator for a constant service curve with rate q->bandwidth.
SimpleServiceCurve ConstantServiceCurve(const Queue* q);
// DNC operator for the aggregation of two arrival curves A and B.
SimpleArrivalCurve AggregateArrivalCurve(const SimpleArrivalCurve& A, const SimpleArrivalCurve& B);
// DNC operator for the convolution of two service curves S and T.
SimpleServiceCurve ConvolutionServiceCurve(const SimpleServiceCurve& S, const SimpleServiceCurve& T);
// DNC operator for the departure  D of an arrival curve A after leaving a queue with service curve S (i.e., D = OutputArrival(A, S)).
// D is thus an arrival curve into the next queue.
SimpleArrivalCurve OutputArrivalCurve(const SimpleArrivalCurve& A, const SimpleServiceCurve& S);
// DNC operator for the remaining service curve that is leftover once a queue with service curve S has accounted for the behavior of an arrival curve A.
SimpleServiceCurve LeftoverServiceCurve(const SimpleArrivalCurve& A, const SimpleServiceCurve& S);
// Calculates the worst case latency for an arrival curve A experiencing a service curve S.
double DNCLatencyBound(const SimpleArrivalCurve& A, const SimpleServiceCurve& S);

#endif // DNC_HPP
