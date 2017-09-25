// SolverGLPKTest.cpp - SolverGLPK test code.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <cassert>
#include <iostream>
#include <json/json.h>
#include "../DNC-Library/Solver.hpp"
#include "DNC-LibraryTest.hpp"

using namespace std;

void SolverGLPKTest()
{
    SolverGLPK s;
    VariableHandle x = s.addVariable(0, 10, VAR_CONTINUOUS, NULL);
    VariableHandle y = s.addVariable(0, 10, VAR_CONTINUOUS, NULL);
    VariableHandle z = s.addVariable(0, 100, VAR_CONTINUOUS, NULL);
    VariableHandle obj = s.addVariable(0, 100, VAR_CONTINUOUS, NULL);
    ConstraintHandle c;
    {
        double coeffs[] = {1, 1};
        VariableHandle vars[] = {x, y};
        c = s.addConstraint(2, coeffs, vars, CONSTRAINT_LE, 16, NULL); // x + y <= 16
    }
    {
        double coeffs[] = {1, -1, -1};
        VariableHandle vars[] = {x, y, z};
        s.addConstraint(3, coeffs, vars, CONSTRAINT_EQ, 0, NULL); // x - y - z = 0
    }
    {
        ConstraintExpression e(2);
        e.append(1, x);
        e.append(1, y);
        s.addConstraintExpression(e, CONSTRAINT_GE, 4, NULL); // x + y >= 4
    }
    {
        double coeffs[] = {1, 1, 5, -1};
        VariableHandle vars[] = {x, y, z, obj};
        s.addConstraint(4, coeffs, vars, CONSTRAINT_EQ, 0, NULL); // x + y + 5*z - obj = 0
    }
    s.setObjectiveCoeff(1, obj);

    const double epsilon = 1e-6;
    s.setObjectiveDirection(OBJECTIVE_MIN);
    assert(s.solve());
    assert(approxEqual(s.getSolution(), 4.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(x), 2.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(y), 2.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(z), 0.0, epsilon));

    s.setObjectiveDirection(OBJECTIVE_MAX);
    assert(s.solve());
    assert(approxEqual(s.getSolution(), 60.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(x), 10.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(y), 0.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(z), 10.0, epsilon));

    s.changeRHS(c, 8);

    s.setObjectiveDirection(OBJECTIVE_MIN);
    assert(s.solve());
    assert(approxEqual(s.getSolution(), 4.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(x), 2.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(y), 2.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(z), 0.0, epsilon));

    s.setObjectiveDirection(OBJECTIVE_MAX);
    assert(s.solve());
    assert(approxEqual(s.getSolution(), 48.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(x), 8.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(y), 0.0, epsilon));
    assert(approxEqual(s.getSolutionVariable(z), 8.0, epsilon));
    cout << "PASS SolverGLPKTest" << endl;
}
