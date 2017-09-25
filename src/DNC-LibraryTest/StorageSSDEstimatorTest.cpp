// StorageSSDEstimatorTest.cpp - StorageSSDEstimator test code.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <cassert>
#include <iostream>
#include <json/json.h>
#include "../Estimator/Estimator.hpp"
#include "DNC-LibraryTest.hpp"

using namespace std;

void StorageSSDEstimatorTest()
{
    Json::Value estimatorInfo;
    estimatorInfo["name"] = Json::Value("testEstimator");
    estimatorInfo["type"] = Json::Value("storageSSD");
    Json::Value& bwTable = estimatorInfo["bandwidthTable"];
    bwTable[0]["requestSize"] = Json::Value(1);
    bwTable[0]["readBandwidth"] = Json::Value(0.5);
    bwTable[0]["writeBandwidth"] = Json::Value(0.5);

    bwTable[1]["requestSize"] = Json::Value(2);
    bwTable[1]["readBandwidth"] = Json::Value(1.0);
    bwTable[1]["writeBandwidth"] = Json::Value(0.5);

    bwTable[2]["requestSize"] = Json::Value(4);
    bwTable[2]["readBandwidth"] = Json::Value(2.0);
    bwTable[2]["writeBandwidth"] = Json::Value(1.0);

    bwTable[3]["requestSize"] = Json::Value(6);
    bwTable[3]["readBandwidth"] = Json::Value(3.0);
    bwTable[3]["writeBandwidth"] = Json::Value(1.5);

    Estimator* pEst = Estimator::create(estimatorInfo);
    assert(pEst->estimateWork(1, true) == 2);
    assert(pEst->estimateWork(2, true) == 2);
    assert(pEst->estimateWork(3, true) == 2);
    assert(pEst->estimateWork(4, true) == 2);
    assert(pEst->estimateWork(5, true) == 2);
    assert(pEst->estimateWork(6, true) == 2);
    assert(pEst->estimateWork(1, false) == 2);
    assert(pEst->estimateWork(2, false) == 4);
    assert(pEst->estimateWork(3, false) == 4);
    assert(pEst->estimateWork(4, false) == 4);
    assert(pEst->estimateWork(5, false) == 4);
    assert(pEst->estimateWork(6, false) == 4);
    delete pEst;
    cout << "PASS StorageSSDEstimatorTest" << endl;
}
