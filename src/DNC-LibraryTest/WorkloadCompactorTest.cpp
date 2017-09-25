// WorkloadCompactorTest.cpp - WorkloadCompactor test code.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <cassert>
#include <cstdlib>
#include <limits>
#include <iostream>
#include <vector>
#include <json/json.h>
#include "../common/serializeJSON.hpp"
#include "../DNC-Library/DNC.hpp"
#include "../DNC-Library/WorkloadCompactor.hpp"
#include "DNC-LibraryTest.hpp"

using namespace std;

void WorkloadCompactorTest()
{
    const double epsilon = 1e-6;
    WorkloadCompactor* wc = new WorkloadCompactor();
    // Setup queues
    Json::Value queueInfo;
    queueInfo["bandwidth"] = Json::Value(1);

    queueInfo["name"] = Json::Value("Q0");
    wc->addQueue(queueInfo);

    // Setup flow's queues
    Json::Value queueList(Json::arrayValue);
    queueList.append(Json::Value("Q0"));

    // Setup client's flows
    Json::Value clientInfo;
    clientInfo["flows"] = Json::arrayValue;
    clientInfo["flows"].resize(1);
    Json::Value& flowInfo = clientInfo["flows"][0];

    // Setup clients and flows
    flowInfo["name"] = Json::Value("F0");
    flowInfo["queues"] = queueList;
    {
        double r[] = {1, 0.2, 0.1};
        double b[] = {1, 1.5, 5};
        unsigned int count = sizeof(r) / sizeof(r[0]);
        vector<double> rates;
        map<double, double> bursts;
        for (unsigned int i = 0; i < count; i++) {
            rates.push_back(r[i]);
            bursts[r[i]] = b[i];
        }
        Curve arrivalCurve;
        rbCurveToArrivalCurve(arrivalCurve, rates, bursts);
        arrivalCurve.erase(arrivalCurve.begin());
        serializeJSON(flowInfo, "arrivalInfo", arrivalCurve);
    }
    clientInfo["SLO"] = Json::Value(5.1);
    clientInfo["name"] = Json::Value("C0");
    ClientId c0 = wc->addClient(clientInfo);

    // Test shaperCurves and latency
    wc->calcAllLatency();
    {
        const Client* c = wc->getClient(c0);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.1, 0.11, epsilon));
        assert(between(shaperCurve.b, 5, 5.1, epsilon));
        assert(between(c->latency, 5, 5.1, epsilon));
    }

    flowInfo["name"] = Json::Value("F1");
    flowInfo["queues"] = queueList;
    flowInfo["priority"] = Json::Value(1);
    {
        double r[] = {1, 0.3, 0.2};
        double b[] = {2, 11, 15};
        unsigned int count = sizeof(r) / sizeof(r[0]);
        vector<double> rates;
        map<double, double> bursts;
        for (unsigned int i = 0; i < count; i++) {
            rates.push_back(r[i]);
            bursts[r[i]] = b[i];
        }
        Curve arrivalCurve;
        rbCurveToArrivalCurve(arrivalCurve, rates, bursts);
        arrivalCurve.erase(arrivalCurve.begin());
        serializeJSON(flowInfo, "arrivalInfo", arrivalCurve);
    }
    clientInfo["SLO"] = Json::Value(23);
    clientInfo["name"] = Json::Value("C1");
    ClientId c1 = wc->addClient(clientInfo);

    // Test shaperCurves and latency
    wc->calcAllLatency();
    {
        const Client* c = wc->getClient(c0);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.1, 0.11, epsilon));
        assert(between(shaperCurve.b, 5, 5.1, epsilon));
        assert(between(c->latency, 5, 5.1, epsilon));
    }
    {
        const Client* c = wc->getClient(c1);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.2, 0.21, epsilon));
        assert(between(shaperCurve.b, 15, 16, epsilon));
        assert(between(c->latency, 20.0/0.9, 23, epsilon));
    }

    flowInfo["name"] = Json::Value("F2");
    flowInfo["queues"] = queueList;
    {
        double r[] = {1, 0.6, 0.4, 0.3};
        double b[] = {5, 10, 50, 95};
        unsigned int count = sizeof(r) / sizeof(r[0]);
        vector<double> rates;
        map<double, double> bursts;
        for (unsigned int i = 0; i < count; i++) {
            rates.push_back(r[i]);
            bursts[r[i]] = b[i];
        }
        Curve arrivalCurve;
        rbCurveToArrivalCurve(arrivalCurve, rates, bursts);
        arrivalCurve.erase(arrivalCurve.begin());
        serializeJSON(flowInfo, "arrivalInfo", arrivalCurve);
    }
    clientInfo["SLO"] = Json::Value(165);
    clientInfo["name"] = Json::Value("C2");
    ClientId c2 = wc->addClient(clientInfo);

    // Test shaperCurves and latency
    wc->calcAllLatency();
    {
        const Client* c = wc->getClient(c0);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.1, 0.11, epsilon));
        assert(between(shaperCurve.b, 5, 5.1, epsilon));
        assert(between(c->latency, 5, 5.1, epsilon));
    }
    {
        const Client* c = wc->getClient(c1);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.2, 0.21, epsilon));
        assert(between(shaperCurve.b, 15, 16, epsilon));
        assert(between(c->latency, 20.0/0.9, 23, epsilon));
    }
    {
        const Client* c = wc->getClient(c2);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.3, 0.31, epsilon));
        assert(between(shaperCurve.b, 95, 100, epsilon));
        assert(between(c->latency, 115.0/0.7, 165, epsilon));
    }

    flowInfo["name"] = Json::Value("F3");
    flowInfo["queues"] = queueList;
    {
        double r[] = {1, 0.5, 0.1};
        double b[] = {1, 2, 3};
        unsigned int count = sizeof(r) / sizeof(r[0]);
        vector<double> rates;
        map<double, double> bursts;
        for (unsigned int i = 0; i < count; i++) {
            rates.push_back(r[i]);
            bursts[r[i]] = b[i];
        }
        Curve arrivalCurve;
        rbCurveToArrivalCurve(arrivalCurve, rates, bursts);
        arrivalCurve.erase(arrivalCurve.begin());
        serializeJSON(flowInfo, "arrivalInfo", arrivalCurve);
    }
    clientInfo["SLO"] = Json::Value(4);
    clientInfo["name"] = Json::Value("C3");
    ClientId c3 = wc->addClient(clientInfo);

    // Test shaperCurves and latency
    wc->calcAllLatency();
    {
        const Client* c = wc->getClient(c0);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.19, 0.21, epsilon));
        assert(between(shaperCurve.b, 1.4, 1.6, epsilon));
        assert(between(c->latency, 5, 5.1, epsilon));
    }
    {
        const Client* c = wc->getClient(c1);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.25, 0.35, epsilon));
        assert(between(shaperCurve.b, 10, 12, epsilon));
        assert(between(c->latency, 15.5/0.7, 23, epsilon));
    }
    {
        const Client* c = wc->getClient(c2);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.35, 0.45, epsilon));
        assert(between(shaperCurve.b, 45, 55, epsilon));
        assert(between(c->latency, 65.5/0.4, 165, epsilon));
    }
    {
        const Client* c = wc->getClient(c3);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.1, 0.11, epsilon));
        assert(between(shaperCurve.b, 3, 4, epsilon));
        assert(between(c->latency, 3, 4, epsilon));
    }

    flowInfo["name"] = Json::Value("F4");
    flowInfo["queues"] = queueList;
    {
        double r[] = {1, 0.4, 0.3};
        double b[] = {1, 4, 5};
        unsigned int count = sizeof(r) / sizeof(r[0]);
        vector<double> rates;
        map<double, double> bursts;
        for (unsigned int i = 0; i < count; i++) {
            rates.push_back(r[i]);
            bursts[r[i]] = b[i];
        }
        Curve arrivalCurve;
        rbCurveToArrivalCurve(arrivalCurve, rates, bursts);
        arrivalCurve.erase(arrivalCurve.begin());
        serializeJSON(flowInfo, "arrivalInfo", arrivalCurve);
    }
    clientInfo["SLO"] = Json::Value(300);
    clientInfo["name"] = Json::Value("C4");
    ClientId c4 = wc->addClient(clientInfo);
    wc->delClient(c3); // swap c3 and c4, which should change r,b pairs

    // Test shaperCurves and latency
    wc->calcAllLatency();
    {
        const Client* c = wc->getClient(c0);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.1, 0.11, epsilon));
        assert(between(shaperCurve.b, 5, 5.1, epsilon));
        assert(between(c->latency, 5, 5.1, epsilon));
    }
    {
        const Client* c = wc->getClient(c1);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.2, 0.21, epsilon));
        assert(between(shaperCurve.b, 15, 16, epsilon));
        assert(between(c->latency, 20.0/0.9, 23, epsilon));
    }
    {
        const Client* c = wc->getClient(c2);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.3, 0.31, epsilon));
        assert(between(shaperCurve.b, 90, 100, epsilon));
        assert(between(c->latency, 110.0/0.7, 165, epsilon));
    }
    {
        const Client* c = wc->getClient(c4);
        const SimpleArrivalCurve& shaperCurve = wc->getShaperCurve(c->flowIds.front());
        assert(between(shaperCurve.r, 0.3, 0.4, epsilon));
        assert(between(shaperCurve.b, 4, 5, epsilon));
        assert(between(c->latency, 119.0/0.4, 300, epsilon));
    }

    delete wc;
    cout << "PASS WorkloadCompactorTest" << endl;
}
