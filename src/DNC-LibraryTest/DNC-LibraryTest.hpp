// DNC-LibraryTest.hpp - function definitions for unit test code.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef _UNIT_TEST_HPP
#define _UNIT_TEST_HPP

#include <cmath>
#include <json/json.h>

using namespace std;

inline bool between(double x, double lower, double upper, double epsilon = 1e-9)
{
    return ((lower - epsilon) <= x) && (x <= (upper + epsilon));
}

inline bool approxEqual(double x, double y, double epsilon = 1e-9)
{
    return (x == y) ? true : ((abs(x - y) / max(max(abs(x), abs(y)), 1.0)) <= epsilon);
}

void TraceReaderTest();
void NetworkEstimatorTest();
void StorageSSDEstimatorTest();
void ProcessedTraceTest();
void serializeJSONTest();
void SolverGLPKTest();
void NCTest();
void DNCTest();
void WorkloadCompactorTest();

#endif // _UNIT_TEST_HPP
