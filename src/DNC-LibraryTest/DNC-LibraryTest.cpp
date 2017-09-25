// DNC-LibraryTest.cpp - Unit test code.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <iostream>
#include "DNC-LibraryTest.hpp"

using namespace std;

int main(int argc, char** argv)
{
    TraceReaderTest();
    NetworkEstimatorTest();
    StorageSSDEstimatorTest();
    ProcessedTraceTest();
    serializeJSONTest();
    SolverGLPKTest();
    NCTest();
    DNCTest();
    WorkloadCompactorTest();
    cout << "PASS" << endl;
    return 0;
}
