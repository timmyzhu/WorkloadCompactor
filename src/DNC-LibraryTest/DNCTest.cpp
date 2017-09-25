// DNCTest.cpp - DNC test code.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <cassert>
#include <cstdlib>
#include <limits>
#include <iostream>
#include <vector>
#include "../common/serializeJSON.hpp"
#include "../Estimator/Estimator.hpp"
#include "../TraceCommon/ProcessedTrace.hpp"
#include "../DNC-Library/DNC.hpp"
#include "DNC-LibraryTest.hpp"

using namespace std;

class TestDNC : public DNC
{
protected:
    virtual FlowId initFlow(Flow* f, const Json::Value& flowInfo, ClientId clientId)
    {
        if (f == NULL) {
            f = new DNCFlow;
        }
        FlowId flowId = NC::initFlow(f, flowInfo, clientId);
        // Initialize shaper curve
        double r = flowInfo["r"].asDouble();
        double b = flowInfo["b"].asDouble();
        DNCFlow* dsf = getDNCFlow(flowId);
        dsf->shaperCurve.r = r;
        dsf->shaperCurve.b = b;
        // Initialize arrival curve
        PointSlope initialPoint(0, 0, numeric_limits<double>::infinity());
        dsf->arrivalCurve.push_back(initialPoint);
        PointSlope p(0, b, r);
        dsf->arrivalCurve.push_back(p);
        return flowId;
    }

public:
    TestDNC()
    {}
    virtual ~TestDNC()
    {}
};

static void buildArrivalCurve(Curve& arrivalCurve, unsigned int count, double initialY, double xArr[], double slopeArr[])
{
    PointSlope p(0, 0, numeric_limits<double>::infinity());
    arrivalCurve.clear();
    arrivalCurve.push_back(p);
    p.y = initialY;
    p.slope = 0; // avoid 0 * infinity
    for (unsigned int i = 0; i < count; i++) {
        p.y += (xArr[i] - p.x) * p.slope;
        p.x = xArr[i];
        p.slope = slopeArr[i];
        arrivalCurve.push_back(p);
    }
}

static void buildServiceCurve(Curve& serviceCurve, unsigned int count, double xArr[], double slopeArr[])
{
    PointSlope p(0, 0, 0);
    serviceCurve.clear();
    for (unsigned int i = 0; i < count; i++) {
        p.y += (xArr[i] - p.x) * p.slope;
        p.x = xArr[i];
        p.slope = slopeArr[i];
        serviceCurve.push_back(p);
    }
}

// p1 == p2
static bool equalPointSlope(const PointSlope& p1, const PointSlope& p2)
{
    return (p1.x == p2.x) && (p1.y == p2.y) && (p1.slope == p2.slope);
}

// c1 == c2
static bool equalCurve(const Curve& c1, const Curve& c2)
{
    if (c1.size() != c2.size()) {
        return false;
    }
    for (unsigned int index = 0; index < c1.size(); index++) {
        const PointSlope& p1 = c1[index];
        const PointSlope& p2 = c2[index];
        if (!equalPointSlope(p1, p2)) {
            return false;
        }
    }
    return true;
}

void testCalcMinRate(ProcessedTrace* pTrace0, ProcessedTrace* pTrace1)
{
    assert(calcMinRate(pTrace0) == 0.18); // hand calculated from testTrace.csv
    assert(calcMinRate(pTrace1) == 0.23); // hand calculated from testTrace.csv
}

void testRbGen(ProcessedTrace* pTrace0, ProcessedTrace* pTrace1)
{
    vector<double> rates(3);
    rates[0] = 1;
    rates[1] = 0.5;
    rates[2] = 0.25;
    map<double, double> bursts0;
    rbGen(pTrace0, rates, bursts0);
    assert(bursts0[1] == 6); // hand calculated from testTrace.csv
    assert(bursts0[0.5] == 10); // hand calculated from testTrace.csv
    assert(bursts0[0.25] == 21); // hand calculated from testTrace.csv
    map<double, double> bursts1;
    rbGen(pTrace1, rates, bursts1);
    assert(bursts1[1] == 13); // hand calculated from testTrace.csv
    assert(bursts1[0.5] == 20); // hand calculated from testTrace.csv
    assert(bursts1[0.25] == 30); // hand calculated from testTrace.csv
}

void testRbCurveToArrivalCurve()
{
    Curve arrivalCurve0;
    Curve arrivalCurve1;
    Curve calcArrivalCurve0;
    Curve calcArrivalCurve1;
    // Test different rates per flow
    {
        vector<double> rates0(1);
        rates0[0] = 1;
        map<double, double> bursts0;
        bursts0[1] = 0;
        bursts0[0.25] = 1;
        rbCurveToArrivalCurve(calcArrivalCurve0, rates0, bursts0);

        vector<double> rates1(2);
        rates1[0] = 1;
        rates1[1] = 0.25;
        map<double, double> bursts1;
        bursts1[1] = 1;
        bursts1[0.25] = 4;
        rbCurveToArrivalCurve(calcArrivalCurve1, rates1, bursts1);

        double initialY0 = 0;
        double xArr0[] = {0};
        double slopeArr0[] = {1};
        unsigned int count0 = sizeof(xArr0) / sizeof(xArr0[0]);
        buildArrivalCurve(arrivalCurve0, count0, initialY0, xArr0, slopeArr0);

        double initialY1 = 1;
        double xArr1[] = {0, 4};
        double slopeArr1[] = {1, 0.25};
        unsigned int count1 = sizeof(xArr1) / sizeof(xArr1[0]);
        buildArrivalCurve(arrivalCurve1, count1, initialY1, xArr1, slopeArr1);
    }
    assert(equalCurve(calcArrivalCurve0, arrivalCurve0));
    assert(equalCurve(calcArrivalCurve1, arrivalCurve1));
    // Test remove points
    {
        vector<double> rates0(5);
        rates0[0] = 1;
        rates0[1] = 0.75;
        rates0[2] = 0.5;
        rates0[3] = 0.25;
        rates0[4] = 0.125;
        map<double, double> bursts0;
        bursts0[1] = 2;
        bursts0[0.75] = 1;
        bursts0[0.5] = 2.5;
        bursts0[0.25] = 3;
        bursts0[0.125] = 4;
        rbCurveToArrivalCurve(calcArrivalCurve0, rates0, bursts0);

        vector<double> rates1(5);
        rates1[0] = 1;
        rates1[1] = 0.75;
        rates1[2] = 0.5;
        rates1[3] = 0.25;
        rates1[4] = 0.125;
        map<double, double> bursts1;
        bursts1[1] = 2;
        bursts1[0.75] = 3;
        bursts1[0.5] = 5;
        bursts1[0.25] = 4.5;
        bursts1[0.125] = 1;
        rbCurveToArrivalCurve(calcArrivalCurve1, rates1, bursts1);

        double initialY0 = 1;
        double xArr0[] = {0, 4, 8};
        double slopeArr0[] = {0.75, 0.25, 0.125};
        unsigned int count0 = sizeof(xArr0) / sizeof(xArr0[0]);
        buildArrivalCurve(arrivalCurve0, count0, initialY0, xArr0, slopeArr0);

        double initialY1 = 1;
        double xArr1[] = {0};
        double slopeArr1[] = {0.125};
        unsigned int count1 = sizeof(xArr1) / sizeof(xArr1[0]);
        buildArrivalCurve(arrivalCurve1, count1, initialY1, xArr1, slopeArr1);
    }
    assert(equalCurve(calcArrivalCurve0, arrivalCurve0));
    assert(equalCurve(calcArrivalCurve1, arrivalCurve1));
    // Test intersect points
    {
        vector<double> rates0(3);
        rates0[0] = 1;
        rates0[1] = 0.5;
        rates0[2] = 0.25;
        map<double, double> bursts0;
        bursts0[1] = 2;
        bursts0[0.5] = 2;
        bursts0[0.25] = 3;
        rbCurveToArrivalCurve(calcArrivalCurve0, rates0, bursts0);

        vector<double> rates1(3);
        rates1[0] = 1;
        rates1[1] = 0.5;
        rates1[2] = 0.25;
        map<double, double> bursts1;
        bursts1[1] = 1;
        bursts1[0.5] = 3;
        bursts1[0.25] = 4;
        rbCurveToArrivalCurve(calcArrivalCurve1, rates1, bursts1);

        double initialY0 = 2;
        double xArr0[] = {0, 4};
        double slopeArr0[] = {0.5, 0.25};
        unsigned int count0 = sizeof(xArr0) / sizeof(xArr0[0]);
        buildArrivalCurve(arrivalCurve0, count0, initialY0, xArr0, slopeArr0);

        double initialY1 = 1;
        double xArr1[] = {0, 4};
        double slopeArr1[] = {1, 0.25};
        unsigned int count1 = sizeof(xArr1) / sizeof(xArr1[0]);
        buildArrivalCurve(arrivalCurve1, count1, initialY1, xArr1, slopeArr1);
    }
    assert(equalCurve(calcArrivalCurve0, arrivalCurve0));
    assert(equalCurve(calcArrivalCurve1, arrivalCurve1));
}

void testCalcPointSlopeIntersection()
{
    // Test positive slope
    {
        PointSlope p1(3, 3, 1);
        PointSlope p2(1, 1, 0.5);
        PointSlope intersectionPoint(1, 1, 1);
        assert(equalPointSlope(intersectionPoint, calcPointSlopeIntersection(p1, p2)));
        intersectionPoint.slope = p2.slope;
        assert(equalPointSlope(intersectionPoint, calcPointSlopeIntersection(p2, p1)));
    }
    // Test zero slope
    {
        PointSlope p1(3, 3, 0);
        PointSlope p2(1, 1, 0.5);
        PointSlope intersectionPoint(5, 3, 0);
        assert(equalPointSlope(intersectionPoint, calcPointSlopeIntersection(p1, p2)));
        intersectionPoint.slope = p2.slope;
        assert(equalPointSlope(intersectionPoint, calcPointSlopeIntersection(p2, p1)));
    }
    // Test negative slope
    {
        PointSlope p1(3, 3, -0.5);
        PointSlope p2(1, 1, 0.5);
        PointSlope intersectionPoint(4, 2.5, -0.5);
        assert(equalPointSlope(intersectionPoint, calcPointSlopeIntersection(p1, p2)));
        intersectionPoint.slope = p2.slope;
        assert(equalPointSlope(intersectionPoint, calcPointSlopeIntersection(p2, p1)));
    }
    // Test equal slope
    {
        PointSlope p1(2, 4, 1);
        PointSlope p2(1, 1, 1);
        assert(equalPointSlope(p1, calcPointSlopeIntersection(p1, p2)));
        assert(equalPointSlope(p2, calcPointSlopeIntersection(p2, p1)));
    }
    // Test infinite slope
    {
        PointSlope p1(0, 0, numeric_limits<double>::infinity());
        PointSlope p2(3, 7, 1);
        PointSlope intersectionPoint(0, 4, numeric_limits<double>::infinity());
        assert(equalPointSlope(intersectionPoint, calcPointSlopeIntersection(p1, p2)));
        intersectionPoint.slope = p2.slope;
        assert(equalPointSlope(intersectionPoint, calcPointSlopeIntersection(p2, p1)));
    }
}

void testPruneArrivalCurve()
{
    Curve arrivalCurve;
    Curve expectedArrivalCurve;

    // Test no prune
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3, 4};
        double slopeArr[] = {6, 5, 4, 3, 2};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(arrivalCurve, count, initialY, xArr, slopeArr);
    }
    pruneArrivalCurve(arrivalCurve, 5);
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3, 4};
        double slopeArr[] = {6, 5, 4, 3, 2};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(expectedArrivalCurve, count, initialY, xArr, slopeArr);
    }
    assert(equalCurve(arrivalCurve, expectedArrivalCurve));

    // Test prune large x values
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3, 100, 200};
        double slopeArr[] = {6, 5, 4, 3, 2, 1};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(arrivalCurve, count, initialY, xArr, slopeArr);
    }
    pruneArrivalCurve(arrivalCurve, 4);
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3};
        double slopeArr[] = {6, 5, 4, 3};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(expectedArrivalCurve, count, initialY, xArr, slopeArr);
    }
    assert(equalCurve(arrivalCurve, expectedArrivalCurve));

    // Test prune all but one point
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3, 4};
        double slopeArr[] = {6, 5, 4, 3, 2};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(arrivalCurve, count, initialY, xArr, slopeArr);
    }
    pruneArrivalCurve(arrivalCurve, 1);
    {
        double initialY = 11.0;
        double xArr[] = {0};
        double slopeArr[] = {2};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(expectedArrivalCurve, count, initialY, xArr, slopeArr);
    }
    assert(equalCurve(arrivalCurve, expectedArrivalCurve));

    // Test prune first point
    {
        double initialY = 0.6;
        double xArr[] = {0, 0.1, 1, 2, 3, 4};
        double slopeArr[] = {10, 6, 5, 4, 3, 2};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(arrivalCurve, count, initialY, xArr, slopeArr);
    }
    pruneArrivalCurve(arrivalCurve, 5);
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3, 4};
        double slopeArr[] = {6, 5, 4, 3, 2};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(expectedArrivalCurve, count, initialY, xArr, slopeArr);
    }
    assert(equalCurve(arrivalCurve, expectedArrivalCurve));

    // Test prune last point
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3, 4};
        double slopeArr[] = {6, 5, 4, 3, 2};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(arrivalCurve, count, initialY, xArr, slopeArr);
    }
    pruneArrivalCurve(arrivalCurve, 4);
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3.5};
        double slopeArr[] = {6, 5, 4, 2};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(expectedArrivalCurve, count, initialY, xArr, slopeArr);
    }
    assert(equalCurve(arrivalCurve, expectedArrivalCurve));

    // Test prune middle point
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3, 4, 6};
        double slopeArr[] = {6, 5, 4, 3, 2, 1};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(arrivalCurve, count, initialY, xArr, slopeArr);
    }
    pruneArrivalCurve(arrivalCurve, 5);
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3.5, 6};
        double slopeArr[] = {6, 5, 4, 2, 1};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(expectedArrivalCurve, count, initialY, xArr, slopeArr);
    }
    assert(equalCurve(arrivalCurve, expectedArrivalCurve));

    // Test prune multiple points
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3, 3.5, 4, 6};
        double slopeArr[] = {6, 5, 4, 3.5, 2.5, 2, 1};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(arrivalCurve, count, initialY, xArr, slopeArr);
    }
    pruneArrivalCurve(arrivalCurve, 5);
    {
        double initialY = 1.0;
        double xArr[] = {0, 1, 2, 3.5, 6};
        double slopeArr[] = {6, 5, 4, 2, 1};
        unsigned int count = sizeof(xArr) / sizeof(xArr[0]);
        buildArrivalCurve(expectedArrivalCurve, count, initialY, xArr, slopeArr);
    }
    assert(equalCurve(arrivalCurve, expectedArrivalCurve));
}

void testEqualCurve()
{
    Curve c1;
    Curve c2;

    // Test empty curves
    assert(equalCurve(c1, c2));

    // Test single point curve with same value
    PointSlope p(3, 2, 1);
    c1.push_back(p);
    c2.push_back(p);
    assert(equalCurve(c1, c2));

    // Test single point curve with different value
    c2[0].slope = 0;
    assert(!equalCurve(c1, c2));
    c2[0].slope = 1;

    // Test different sized curves
    c1.push_back(p);
    assert(!equalCurve(c1, c2));

    // Test multi-point curve with same values
    c2.push_back(p);
    assert(equalCurve(c1, c2));

    // Test multi-point curves with different values
    c2[1].y = 10;
    assert(!equalCurve(c1, c2));
    c2[1].y = 2;
}

void testCalcLatency()
{
    Curve arrivalCurve;
    Curve serviceCurve;
    double latency;

    // Test single point
    {
        double initialY = 1;
        double xArrArrival[] = {0};
        double slopeArrArrival[] = {0.5};
        unsigned int countArrival = sizeof(xArrArrival) / sizeof(xArrArrival[0]);
        buildArrivalCurve(arrivalCurve, countArrival, initialY, xArrArrival, slopeArrArrival);
        double xArrService[] = {0, 2};
        double slopeArrService[] = {0, 1};
        unsigned int countService = sizeof(xArrService) / sizeof(xArrService[0]);
        buildServiceCurve(serviceCurve, countService, xArrService, slopeArrService);
    }
    latency = calcLatency(arrivalCurve, serviceCurve);
    assert(latency == 3);
    // Test aligned y values
    {
        double initialY = 1;
        double xArrArrival[] = {0, 1, 3};
        double slopeArrArrival[] = {1, 0.5, 0.25};
        unsigned int countArrival = sizeof(xArrArrival) / sizeof(xArrArrival[0]);
        buildArrivalCurve(arrivalCurve, countArrival, initialY, xArrArrival, slopeArrArrival);
        double xArrService[] = {0, 1, 2, 4, 8};
        double slopeArrService[] = {0, 1, 0.5, 0.25, 0.1};
        unsigned int countService = sizeof(xArrService) / sizeof(xArrService[0]);
        buildServiceCurve(serviceCurve, countService, xArrService, slopeArrService);
    }
    latency = calcLatency(arrivalCurve, serviceCurve);
    assert(latency == 5);
    // Test unaligned y values
    {
        double initialY = 1;
        double xArrArrival[] = {0, 2, 6};
        double slopeArrArrival[] = {1, 0.5, 0.25};
        unsigned int countArrival = sizeof(xArrArrival) / sizeof(xArrArrival[0]);
        buildArrivalCurve(arrivalCurve, countArrival, initialY, xArrArrival, slopeArrArrival);
        double xArrService[] = {0, 1, 9, 13};
        double slopeArrService[] = {0, 0.25, 0.5, 1};
        unsigned int countService = sizeof(xArrService) / sizeof(xArrService[0]);
        buildServiceCurve(serviceCurve, countService, xArrService, slopeArrService);
    }
    latency = calcLatency(arrivalCurve, serviceCurve);
    assert(latency == 9);
    // Test mixed alignment y values
    {
        double initialY = 1;
        double xArrArrival[] = {0, 1, 5, 6.6, 10.6, 18.6, 22.6};
        double slopeArrArrival[] = {1, 0.75, 0.625, 0.5, 0.325, 0.25, 0.125};
        unsigned int countArrival = sizeof(xArrArrival) / sizeof(xArrArrival[0]);
        buildArrivalCurve(arrivalCurve, countArrival, initialY, xArrArrival, slopeArrArrival);
        double xArrService[] = {0, 1, 9, 13, 21, 23, 24.6, 28.6};
        double slopeArrService[] = {0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 1};
        unsigned int countService = sizeof(xArrService) / sizeof(xArrService[0]);
        buildServiceCurve(serviceCurve, countService, xArrService, slopeArrService);
    }
    latency = calcLatency(arrivalCurve, serviceCurve);
    assert(latency == 16.4);
    {
        double initialY = 3;
        double xArrArrival[] = {0, 1, 5, 6.6, 10.6, 18.6, 22.6};
        double slopeArrArrival[] = {1, 0.75, 0.625, 0.5, 0.325, 0.25, 0.125};
        unsigned int countArrival = sizeof(xArrArrival) / sizeof(xArrArrival[0]);
        buildArrivalCurve(arrivalCurve, countArrival, initialY, xArrArrival, slopeArrArrival);
        double xArrService[] = {0, 1, 9, 13, 21, 23, 24.6, 28.6};
        double slopeArrService[] = {0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 1};
        unsigned int countService = sizeof(xArrService) / sizeof(xArrService[0]);
        buildServiceCurve(serviceCurve, countService, xArrService, slopeArrService);
    }
    latency = calcLatency(arrivalCurve, serviceCurve);
    assert(latency == 19.6);
    {
        double initialY = 6;
        double xArrArrival[] = {0, 1, 5, 6.6, 10.6, 18.6, 22.6};
        double slopeArrArrival[] = {1, 0.75, 0.625, 0.5, 0.325, 0.25, 0.125};
        unsigned int countArrival = sizeof(xArrArrival) / sizeof(xArrArrival[0]);
        buildArrivalCurve(arrivalCurve, countArrival, initialY, xArrArrival, slopeArrArrival);
        double xArrService[] = {0, 1, 9, 13, 21, 23, 24.6, 28.6};
        double slopeArrService[] = {0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 1};
        unsigned int countService = sizeof(xArrService) / sizeof(xArrService[0]);
        buildServiceCurve(serviceCurve, countService, xArrService, slopeArrService);
    }
    latency = calcLatency(arrivalCurve, serviceCurve);
    assert(latency == 23.6);
    // Test negative value
    {
        double initialY = 0;
        double xArrArrival[] = {0, 1, 5};
        double slopeArrArrival[] = {1, 0.5, 0.25};
        unsigned int countArrival = sizeof(xArrArrival) / sizeof(xArrArrival[0]);
        buildArrivalCurve(arrivalCurve, countArrival, initialY, xArrArrival, slopeArrArrival);
        double xArrService[] = {0, 1, 5, 11};
        double slopeArrService[] = {0, 1, 0, 0.5};
        unsigned int countService = sizeof(xArrService) / sizeof(xArrService[0]);
        buildServiceCurve(serviceCurve, countService, xArrService, slopeArrService);
    }
    latency = calcLatency(arrivalCurve, serviceCurve);
    assert(latency == 2);
}

void DNCTestOneHop()
{
    NC* nc = new TestDNC();
    // Setup queues
    Json::Value queueInfo;
    queueInfo["bandwidth"] = Json::Value(1);

    queueInfo["name"] = Json::Value("Q0");
    nc->addQueue(queueInfo);

    // Setup flow's queues
    Json::Value queueList(Json::arrayValue);
    queueList.append(Json::Value("Q0"));

    // Setup client's flows
    Json::Value clientInfo;
    clientInfo["flows"] = Json::arrayValue;
    clientInfo["flows"].resize(1);
    clientInfo["SLO"] = Json::Value(1);
    clientInfo["SLOpercentile"] = Json::Value(99.9);
    Json::Value& flowInfo = clientInfo["flows"][0];
    flowInfo["queues"] = queueList;

    // Setup clients and flows
    flowInfo["name"] = Json::Value("F0");
    flowInfo["priority"] = Json::Value(1);
    flowInfo["r"] = Json::Value(0.25);
    flowInfo["b"] = Json::Value(0.5);
    clientInfo["name"] = Json::Value("C0");
    ClientId c0 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F1");
    flowInfo["priority"] = Json::Value(1);
    flowInfo["r"] = Json::Value(0.125);
    flowInfo["b"] = Json::Value(1);
    clientInfo["name"] = Json::Value("C1");
    ClientId c1 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F2");
    flowInfo["priority"] = Json::Value(2);
    flowInfo["r"] = Json::Value(0.125);
    flowInfo["b"] = Json::Value(0.25);
    clientInfo["name"] = Json::Value("C2");
    ClientId c2 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F3");
    flowInfo["priority"] = Json::Value(2);
    flowInfo["r"] = Json::Value(0.5);
    flowInfo["b"] = Json::Value(2.25);
    clientInfo["name"] = Json::Value("C3");
    ClientId c3 = nc->addClient(clientInfo);

    assert(nc->calcClientLatency(c0) == 1.5);
    assert(nc->calcClientLatency(c1) == 1.5);
    assert(nc->calcClientLatency(c2) == 6.4);
    assert(nc->calcClientLatency(c3) == 6.4);

    delete nc;
}

void DNCTestTwoHops()
{
    NC* nc = new TestDNC();
    // Setup queues
    Json::Value queueInfo;
    queueInfo["bandwidth"] = Json::Value(1);

    queueInfo["name"] = Json::Value("Q0");
    nc->addQueue(queueInfo);
    queueInfo["name"] = Json::Value("Q1");
    nc->addQueue(queueInfo);
    queueInfo["name"] = Json::Value("Q2");
    nc->addQueue(queueInfo);
    queueInfo["name"] = Json::Value("Q3");
    nc->addQueue(queueInfo);

    // Setup flow's queues
    Json::Value queueListA(Json::arrayValue);
    queueListA.append(Json::Value("Q0"));
    queueListA.append(Json::Value("Q2"));
    Json::Value queueListB(Json::arrayValue);
    queueListB.append(Json::Value("Q0"));
    queueListB.append(Json::Value("Q3"));
    Json::Value queueListC(Json::arrayValue);
    queueListC.append(Json::Value("Q1"));
    queueListC.append(Json::Value("Q2"));
    Json::Value queueListD(Json::arrayValue);
    queueListD.append(Json::Value("Q1"));
    queueListD.append(Json::Value("Q3"));

    // Setup client's flows
    Json::Value clientInfo;
    clientInfo["flows"] = Json::arrayValue;
    clientInfo["flows"].resize(1);
    clientInfo["SLO"] = Json::Value(1);
    clientInfo["SLOpercentile"] = Json::Value(99.9);
    Json::Value& flowInfo = clientInfo["flows"][0];

    // Setup clients and flows
    flowInfo["name"] = Json::Value("F0");
    flowInfo["queues"] = queueListA;
    flowInfo["priority"] = Json::Value(1);
    flowInfo["r"] = Json::Value(0.25);
    flowInfo["b"] = Json::Value(0.5);
    clientInfo["name"] = Json::Value("C0");
    ClientId c0 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F1");
    flowInfo["queues"] = queueListA;
    flowInfo["priority"] = Json::Value(1);
    flowInfo["r"] = Json::Value(0.125);
    flowInfo["b"] = Json::Value(1);
    clientInfo["name"] = Json::Value("C1");
    ClientId c1 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F2");
    flowInfo["queues"] = queueListB;
    flowInfo["priority"] = Json::Value(2);
    flowInfo["r"] = Json::Value(0.125);
    flowInfo["b"] = Json::Value(0.25);
    clientInfo["name"] = Json::Value("C2");
    ClientId c2 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F3");
    flowInfo["queues"] = queueListB;
    flowInfo["priority"] = Json::Value(2);
    flowInfo["r"] = Json::Value(0.5);
    flowInfo["b"] = Json::Value(2.25);
    clientInfo["name"] = Json::Value("C3");
    ClientId c3 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F4");
    flowInfo["queues"] = queueListC;
    flowInfo["priority"] = Json::Value(3);
    flowInfo["r"] = Json::Value(0.125);
    flowInfo["b"] = Json::Value(0.25);
    clientInfo["name"] = Json::Value("C4");
    ClientId c4 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F5");
    flowInfo["queues"] = queueListC;
    flowInfo["priority"] = Json::Value(3);
    flowInfo["r"] = Json::Value(0.125);
    flowInfo["b"] = Json::Value(0.75);
    clientInfo["name"] = Json::Value("C5");
    ClientId c5 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F6");
    flowInfo["queues"] = queueListD;
    flowInfo["priority"] = Json::Value(4);
    flowInfo["r"] = Json::Value(0.125);
    flowInfo["b"] = Json::Value(0.25);
    clientInfo["name"] = Json::Value("C6");
    ClientId c6 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F7");
    flowInfo["queues"] = queueListD;
    flowInfo["priority"] = Json::Value(4);
    flowInfo["r"] = Json::Value(0.125);
    flowInfo["b"] = Json::Value(1.25);
    clientInfo["name"] = Json::Value("C7");
    ClientId c7 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F8");
    flowInfo["queues"] = queueListD;
    flowInfo["priority"] = Json::Value(5);
    flowInfo["r"] = Json::Value(0);
    flowInfo["b"] = Json::Value(0.25);
    clientInfo["name"] = Json::Value("C8");
    ClientId c8 = nc->addClient(clientInfo);

    flowInfo["name"] = Json::Value("F9");
    flowInfo["queues"] = queueListD;
    flowInfo["priority"] = Json::Value(5);
    flowInfo["r"] = Json::Value(0);
    flowInfo["b"] = Json::Value(0.25);
    clientInfo["name"] = Json::Value("C9");
    ClientId c9 = nc->addClient(clientInfo);

    assert(nc->calcClientLatency(c0) == 1.5);
    assert(nc->calcClientLatency(c1) == 1.5);
    assert(nc->calcClientLatency(c2) == 6.4);
    assert(nc->calcClientLatency(c3) == 6.4);
    assert(nc->calcClientLatency(c4) == 4);
    assert(nc->calcClientLatency(c5) == 4);
    assert(nc->calcClientLatency(c6) == 16);
    assert(nc->calcClientLatency(c7) == 16);
    assert(nc->calcClientLatency(c8) == 52);
    assert(nc->calcClientLatency(c9) == 52);

    delete nc;
}

void DNCTest()
{
    // Test helper functions
    testEqualCurve();
    testCalcPointSlopeIntersection();
    testPruneArrivalCurve();
    testCalcLatency();

    // Setup estimators and traces
    Json::Value networkOutEstimatorInfo;
    networkOutEstimatorInfo["type"] = Json::Value("networkOut");
    networkOutEstimatorInfo["nonDataConstant"] = Json::Value(0.0);
    networkOutEstimatorInfo["nonDataFactor"] = Json::Value(1.0);
    networkOutEstimatorInfo["dataConstant"] = Json::Value(0.0);
    networkOutEstimatorInfo["dataFactor"] = Json::Value(1.0);
    Estimator* pEst0 = Estimator::create(networkOutEstimatorInfo);
    ProcessedTrace* pTrace0 = new ProcessedTrace("testTrace.csv", pEst0);

    vector<StorageBandwidth> readBandwidthTable(4);
    vector<StorageBandwidth> writeBandwidthTable(4);
    readBandwidthTable[0].requestSize = 1;
    readBandwidthTable[0].bandwidth = 0.5;
    writeBandwidthTable[0].requestSize = 1;
    writeBandwidthTable[0].bandwidth = 0.5;

    readBandwidthTable[1].requestSize = 2;
    readBandwidthTable[1].bandwidth = 1;
    writeBandwidthTable[1].requestSize = 2;
    writeBandwidthTable[1].bandwidth = 0.5;

    readBandwidthTable[2].requestSize = 4;
    readBandwidthTable[2].bandwidth = 2;
    writeBandwidthTable[2].requestSize = 4;
    writeBandwidthTable[2].bandwidth = 1;

    readBandwidthTable[3].requestSize = 6;
    readBandwidthTable[3].bandwidth = 3;
    writeBandwidthTable[3].requestSize = 6;
    writeBandwidthTable[3].bandwidth = 1.5;

    Estimator* pEst1 = new StorageSSDEstimator(readBandwidthTable, writeBandwidthTable);
    ProcessedTrace* pTrace1 = new ProcessedTrace("testTrace.csv", pEst1);

    // Test input functions
    testCalcMinRate(pTrace0, pTrace1);
    testRbGen(pTrace0, pTrace1);
    testRbCurveToArrivalCurve();

    delete pTrace0;
    delete pTrace1;

    DNCTestOneHop();
    DNCTestTwoHops();
    cout << "PASS DNCTest" << endl;
}
