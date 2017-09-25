// Solver.hpp - generic interface for a linear program (LP) solver.
// Currently, there is only one adapter to GLPK, but the design allows for other solvers to be utilized via an adapter class.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef SOLVER_HPP
#define SOLVER_HPP

#include <cstring>
#include "../glpk/glpk.h"

using namespace std;

typedef int VariableHandle;
typedef int ConstraintHandle;

const VariableHandle InvalidVariableHandle = -2;

enum VarType {
    VAR_CONTINUOUS = 0,
    VAR_BINARY,
    VAR_INTEGER,
};

enum ConstraintType {
    CONSTRAINT_LE = 0,
    CONSTRAINT_EQ,
    CONSTRAINT_GE,
};

enum ObjectiveType {
    OBJECTIVE_MIN = 0,
    OBJECTIVE_MAX,
};

// Represents an LP constraint
class ConstraintExpression
{
public:
    double* coeffs;
    VariableHandle* vars;
    int count;
    int capacity;

    ConstraintExpression()
        : coeffs(NULL),
          vars(NULL),
          count(0),
          capacity(0)
    {}
    ConstraintExpression(const ConstraintExpression& other) {
        if (other.coeffs) {
            coeffs = new double[other.capacity];
            memcpy(coeffs, other.coeffs, sizeof(double) * other.count);
        } else {
            coeffs = NULL;
        }
        if (other.vars) {
            vars = new VariableHandle[other.capacity];
            memcpy(vars, other.vars, sizeof(VariableHandle) * other.count);
        } else {
            vars = NULL;
        }
        count = other.count;
        capacity = other.capacity;
    }
    ConstraintExpression(int maxSize)
        : coeffs(NULL),
          vars(NULL),
          count(0),
          capacity(maxSize)
    { init(maxSize); }
    ~ConstraintExpression() {
        if (coeffs) {
            delete[] coeffs;
        }
        if (vars) {
            delete[] vars;
        }
    }
    // Initializes an LP constraint with a given maximum number of variables
    void init(int maxSize) {
        if (coeffs) {
            delete[] coeffs;
        }
        coeffs = new double[maxSize];
        if (vars) {
            delete[] vars;
        }
        vars = new VariableHandle[maxSize];
        count = 0;
        capacity = maxSize;
    }
    // Append an LP variable to the constraint
    void append(double coeff, VariableHandle var) {
        coeffs[count] = coeff;
        vars[count] = var;
        count++;
    }
};

// Abstract base class
class Solver
{
public:
    // Add an LP variable with the given lower and upper bounds; name is an optional identifier for the variable
    virtual VariableHandle addVariable(double lb, double ub, enum VarType type, const char* name) = 0;
    // Add an LP constraint sum_i=0...(count-1) coeff[i]*vars[i] = rhs; name is an optional identifier for the constraint
    virtual ConstraintHandle addConstraint(int count, const double* coeffs, const VariableHandle* vars, enum ConstraintType type, double rhs, const char* name) = 0;
    // Add an LP constraint in the form of a ConstraintExpression
    ConstraintHandle addConstraintExpression(const ConstraintExpression& expr, enum ConstraintType type, double rhs, const char* name) {
        return addConstraint(expr.count, expr.coeffs, expr.vars, type, rhs, name);
    }
    // Set a min/max objective direction
    virtual void setObjectiveDirection(enum ObjectiveType type) = 0;
    // Set the coefficient and variable for the objective
    virtual void setObjectiveCoeff(double coeff, VariableHandle var) = 0;
    // Solve the LP
    virtual bool solve() = 0;
    // Get the solved LP objective value
    virtual double getSolution() = 0;
    // Get the solved LP variable value
    virtual double getSolutionVariable(VariableHandle var) = 0;
    // Change the right-hand-size value of a constraint
    virtual void changeRHS(ConstraintHandle constraint, double rhs) = 0;
};

// GLPK solver
class SolverGLPK : public Solver
{
private:
    glp_prob* prob;
    bool simplexMethod;

public:
    SolverGLPK();
    virtual ~SolverGLPK();

    virtual VariableHandle addVariable(double lb, double ub, enum VarType type, const char* name);
    virtual ConstraintHandle addConstraint(int count, const double* coeffs, const VariableHandle* vars, enum ConstraintType type, double rhs, const char* name);
    virtual void setObjectiveDirection(enum ObjectiveType type);
    virtual void setObjectiveCoeff(double coeff, VariableHandle var);
    virtual bool solve();
    virtual double getSolution();
    virtual double getSolutionVariable(VariableHandle var);
    virtual void changeRHS(ConstraintHandle constraint, double rhs);
};

#endif // SOLVER_HPP
