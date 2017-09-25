// SolverGLPK.cpp - Solver adapter for the GLPK linear program (LP) solver.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <cmath>
#include "../glpk/glpk.h"
#include "Solver.hpp"

using namespace std;

SolverGLPK::SolverGLPK()
{
    // Make solver quiet
    glp_term_out(GLP_OFF);
    // Create problem
    prob = glp_create_prob();
}

SolverGLPK::~SolverGLPK()
{
    glp_delete_prob(prob);
}

VariableHandle SolverGLPK::addVariable(double lb, double ub, enum VarType type, const char* name)
{
    const int typeTranslation[] = {GLP_CV, GLP_BV, GLP_IV};
    VariableHandle var = glp_add_cols(prob, 1);
    if (isfinite(lb)) {
        if (isfinite(ub)) {
            glp_set_col_bnds(prob, var, GLP_DB, lb, ub);
        } else {
            glp_set_col_bnds(prob, var, GLP_LO, lb, ub);
        }
    } else {
        if (isfinite(ub)) {
            glp_set_col_bnds(prob, var, GLP_UP, lb, ub);
        } else {
            glp_set_col_bnds(prob, var, GLP_FR, lb, ub);
        }
    }
    glp_set_col_kind(prob, var, typeTranslation[type]);
    if (name) {
        glp_set_col_name(prob, var, name);
    }
    return var;
}

ConstraintHandle SolverGLPK::addConstraint(int count, const double* coeffs, const VariableHandle* vars, enum ConstraintType type, double rhs, const char* name)
{
    const int typeTranslation[] = {GLP_UP, GLP_FX, GLP_LO};
    ConstraintHandle constraint = glp_add_rows(prob, 1);
    glp_set_mat_row(prob, constraint, count, &vars[-1], &coeffs[-1]); // GLPK is 1-indexed
    glp_set_row_bnds(prob, constraint, typeTranslation[type], rhs, rhs);
    if (name) {
        glp_set_row_name(prob, constraint, name);
    }
    return constraint;
}

void SolverGLPK::setObjectiveDirection(enum ObjectiveType type)
{
    const int typeTranslation[] = {GLP_MIN, GLP_MAX};
    glp_set_obj_dir(prob, typeTranslation[type]);
}

void SolverGLPK::setObjectiveCoeff(double coeff, VariableHandle var)
{
    glp_set_obj_coef(prob, var, coeff);
}

bool SolverGLPK::solve()
{
    simplexMethod = false;
    glp_scale_prob(prob, GLP_SF_AUTO);
    int status = glp_interior(prob, NULL);
    // Fall back to simplex method
    if (status != 0) {
        simplexMethod = true;
        status = glp_simplex(prob, NULL);
        // Refine solution by solving exact version
        if ((status == 0) && (glp_get_status(prob) == GLP_OPT)) {
            status = glp_exact(prob, NULL);
        }
    }
    if (status == 0) {
        if (simplexMethod) {
            return (glp_get_status(prob) == GLP_OPT);
        } else {
            return (glp_ipt_status(prob) == GLP_OPT);
        }
    } else {
        return false;
    }
}

double SolverGLPK::getSolution()
{
    if (simplexMethod) {
        return glp_get_obj_val(prob);
    } else {
        return glp_ipt_obj_val(prob);
    }
}

double SolverGLPK::getSolutionVariable(VariableHandle var)
{
    if (simplexMethod) {
        return glp_get_col_prim(prob, var);
    } else {
        return glp_ipt_col_prim(prob, var);
    }
}

void SolverGLPK::changeRHS(ConstraintHandle constraint, double rhs)
{
    glp_set_row_bnds(prob, constraint, glp_get_row_type(prob, constraint), rhs, rhs);
}
