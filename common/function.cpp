//  Copyright 2018-2020 Lexis Nexis Risk Solutions
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//
//  Created by Caleb Moore on 11/9/18.
//

#include "function.hpp"
#include "ast.hpp"
#include "luaconverter/luaoutputter.hpp"
#include "model/transformation.hpp"
#include <algorithm>
#include <cstring>
#include <math.h>

namespace Function
{
    // This is a table of function definitions that are available to "apply" (some of which are available to predicates as well).
    // This MUST be in ascending order for binary search. Must be kept in sync with definitions in function.hpp
    
    // Columns are as so:
    // pmmlFunction   - the name that function is referred to in the PMML document
    // luaFunction    - the name of the equivalent Lua function
    // functionType   - type of logic used by code outputter
    // outputType     - the type of the return value (TYPE_INVALID in this context means "same as the arguments")
    // operatorLevel  - operator precedence level (in Lua)
    // nullityType    - logic to use for calculating potential nulity
    // minArgs        - minimum number of arguments that may be passed in
    // maxArgs        - maximum number of arguments that may be passed in
    
const FunctionTable functionTable = {{
    {"*",           "*",            OPERATOR,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TIMES, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"+",           "+",            OPERATOR,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_PLUS,  MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"-",           "-",            OPERATOR,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_PLUS,  MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"/",           "/",            OPERATOR,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TIMES, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"abs",         "math.abs",     FUNCTIONLIKE,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"acos",        "math.acos",    FUNCTIONLIKE,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"and",         "and",          BOOLEAN_AND,   PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_AND,   MAYBE_MISSING_IF_ANY_ARGUMENT_IS_MISSING,                  1, std::numeric_limits<size_t>::max()},
    {"asin",        "math.asin",    FUNCTIONLIKE,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"atan",        "math.atan",    FUNCTIONLIKE,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"avg",         "+",            MEAN_MACRO,    PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TIMES, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, std::numeric_limits<size_t>::max()},
    {"ceil",        "math.ceil",    FUNCTIONLIKE,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TIMES, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"concat",      "..",           OPERATOR,      PMMLDocument::TYPE_STRING, LuaOutputter::PRECEDENCE_CONCAT,MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, std::numeric_limits<size_t>::max()},
    {"cos",         "math.cos",     FUNCTIONLIKE,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP, MISSING_IF_ANY_ARGUMENT_IS_MISSING,   1, 1},
    {"cosh",        "math.cosh",    FUNCTIONLIKE,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP, MISSING_IF_ANY_ARGUMENT_IS_MISSING,   1, 1},
    {"dateDaysSinceYear", "",       UNSUPPORTED,   PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"dateSecondsSinceMidnight","", UNSUPPORTED,   PMMLDocument::TYPE_NUMBER,LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"dateSecondsSinceYear", "",    UNSUPPORTED,   PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"equal",       "==",           COMPARISON,    PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_EQUAL, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"erf",         "erf",          RUN_LAMBDA,    PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"exp",         "math.exp",     FUNCTIONLIKE,  PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"expm1",       "",             UNSUPPORTED,   PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"floor",       "math.floor",   FUNCTIONLIKE,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"formatDatetime","",           UNSUPPORTED,   PMMLDocument::TYPE_STRING, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"formatNumber","string.format",FUNCTIONLIKE,      PMMLDocument::TYPE_STRING, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"greaterOrEqual", ">=",        COMPARISON,    PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_EQUAL, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"greaterThan", ">",            COMPARISON,    PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_EQUAL, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"if",          nullptr,        TERNARY_MACRO, PMMLDocument::TYPE_INVALID,LuaOutputter::PRECEDENCE_TOP,   MAYBE_MISSING_IF_ANY_ARGUMENT_IS_MISSING,              2, 3},
    {"isIn",        "==",           IS_IN,         PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_OR,    MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, std::numeric_limits<size_t>::max()},
    {"isMissing",   "==",           IS_MISSING,    PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_EQUAL, NEVER_MISSING,                      1, 1},
    {"isNotIn",     "~=",           IS_IN,         PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_AND,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, std::numeric_limits<size_t>::max()},
    {"isNotMissing","not",          IS_NOT_MISSING,PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_UNARY, NEVER_MISSING,                      1, 1},
    // These aren't actually correct as we treat missing and invalid the same way, but will give the right result mostly.
    {"isNotValid",  "==",           IS_MISSING,    PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_EQUAL, NEVER_MISSING,                      1, 1},
    {"isValid",     "not",          IS_NOT_MISSING,PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_UNARY, NEVER_MISSING,                      1, 1},
    {"lessOrEqual", "<=",           COMPARISON,    PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_EQUAL, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"lessThan",    "<",            COMPARISON,    PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_EQUAL, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"log10",       "math.log",     LOG10_MACRO,   PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"ln",          "math.log",     FUNCTIONLIKE,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"lowercase",   "string.lower", FUNCTIONLIKE,      PMMLDocument::TYPE_STRING, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"matches",     "",             UNSUPPORTED,   PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"max",         "math.max",     FUNCTIONLIKE,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, std::numeric_limits<size_t>::max()},
    {"median",      "",             UNSUPPORTED,   PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, std::numeric_limits<size_t>::max()},
    {"min",         "math.min",     FUNCTIONLIKE,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, std::numeric_limits<size_t>::max()},
    {"modulo",      "%",            OPERATOR,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TIMES, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"normalCDF",   "normalCDF",    UNSUPPORTED,   PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 3, 3},
    {"normalIDF",   "normalIDF",    UNSUPPORTED,   PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 3, 3},
    {"normalPDF",   "normalPDF",    UNSUPPORTED,   PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 3, 3},
    {"not",         "not",          NOT_OPERATOR,  PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_UNARY, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"notEqual",    "~=",           COMPARISON,    PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_EQUAL, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"or",          "or",           BOOLEAN_OR,    PMMLDocument::TYPE_BOOL,   LuaOutputter::PRECEDENCE_OR,    MAYBE_MISSING_IF_ANY_ARGUMENT_IS_MISSING,                   1, std::numeric_limits<size_t>::max()},
    {"pow",         "^",            OPERATOR,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_POWER, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"product",     "*",            OPERATOR,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TIMES, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, std::numeric_limits<size_t>::max()},
    {"replace",     "",             UNSUPPORTED,   PMMLDocument::TYPE_STRING, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, std::numeric_limits<size_t>::max()},
    {"round",       "math.floor",   ROUND_MACRO,   PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"sin",         "math.sin",     FUNCTIONLIKE,  PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"sinh",        "math.sinh",    FUNCTIONLIKE,  PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"stdNormalCDF","stdNormalCDF", RUN_LAMBDA,    PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"stdNormalIDF","stdNormalIDF", RUN_LAMBDA,    PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"stdNormalPDF","",             UNSUPPORTED,   PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"substring",   "string.sub",   SUBSTRING_MACRO,PMMLDocument::TYPE_STRING,LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 3, 3},
    {"sum",         "+",            OPERATOR,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_PLUS,  MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, std::numeric_limits<size_t>::max()},
    {"tan",         "math.tan",     FUNCTIONLIKE,  PMMLDocument::TYPE_NUMBER,  LuaOutputter::PRECEDENCE_TOP,  MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"tanh",        "math.tanh",    FUNCTIONLIKE,  PMMLDocument::TYPE_NUMBER,  LuaOutputter::PRECEDENCE_TOP,  MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"threshold",   nullptr,        THRESHOLD_MACRO,PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_OR,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2},
    {"trimBlanks",  nullptr,        TRIMBLANK_MACRO,PMMLDocument::TYPE_STRING, LuaOutputter::PRECEDENCE_OR,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"uppercase",   "string.upper", FUNCTIONLIKE,      PMMLDocument::TYPE_STRING, LuaOutputter::PRECEDENCE_TOP,   MISSING_IF_ANY_ARGUMENT_IS_MISSING, 1, 1},
    {"x-modulo",    "%",            OPERATOR,      PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_TIMES, MISSING_IF_ANY_ARGUMENT_IS_MISSING, 2, 2}
}};

    // These are "internal definition", functions that are not available within the PMML document, but are added automatically in other places.
    // These may be added in various models or for functions that are implemented in lua (e.g. stdNormal*DF)
    
    // Columns are as so:
    // luaFunction    - the name of the equivalent Lua function
    // functionType   - type of logic used by code outputter
    // outputType     - the type of the return value
    // operatorLevel  - operator precedence level (in Lua)
    // nullityType    - logic to use for calculating potential nulity
    
    // This is a special case of the "if" expression with only two operands, it returns an unknown value if the predicate is false.
    // It is used here and in transformations to implement normalization
    const Definition boundFunction = {nullptr, BOUND_MACRO, PMMLDocument::TYPE_INVALID, LuaOutputter::PRECEDENCE_OR, MAYBE_MISSING};
    
    const Definition unaryMinus = {"-", UNARY_OPERATOR, PMMLDocument::TYPE_INVALID, LuaOutputter::PRECEDENCE_UNARY, MISSING_IF_ANY_ARGUMENT_IS_MISSING};
    
    const Definition makeTuple = {nullptr, MAKE_TUPLE, PMMLDocument::TYPE_INVALID, LuaOutputter::PRECEDENCE_TOP, MISSING_IF_ANY_ARGUMENT_IS_MISSING};
    
    const Definition runLambda = {nullptr, RUN_LAMBDA, PMMLDocument::TYPE_INVALID, LuaOutputter::PRECEDENCE_TOP, Function::MAYBE_MISSING};

    const Definition runLambdaArgsMissing = {nullptr, RUN_LAMBDA, PMMLDocument::TYPE_INVALID, LuaOutputter::PRECEDENCE_TOP, Function::MAYBE_MISSING_IF_ANY_ARGUMENT_IS_MISSING};

    const Definition runLambdaNeverMissing = {nullptr, RUN_LAMBDA, PMMLDocument::TYPE_INVALID, LuaOutputter::PRECEDENCE_TOP, Function::NEVER_MISSING};
        
    const Definition sqrtFunction = {"math.sqrt", FUNCTIONLIKE, PMMLDocument::TYPE_INVALID, LuaOutputter::PRECEDENCE_TOP, MISSING_IF_ANY_ARGUMENT_IS_MISSING};

    // These are used for "median" mode of MiningModel.
    const Definition sortTableDef = {"table.sort", FUNCTIONLIKE, PMMLDocument::TYPE_VOID, LuaOutputter::PRECEDENCE_TOP, MISSING_IF_ANY_ARGUMENT_IS_MISSING };
    const Definition insertToTableDef = {"table.insert", FUNCTIONLIKE, PMMLDocument::TYPE_VOID,LuaOutputter::PRECEDENCE_TOP, MISSING_IF_ANY_ARGUMENT_IS_MISSING};
    const Definition listLengthDef = {"#", UNARY_OPERATOR, PMMLDocument::TYPE_NUMBER, LuaOutputter::PRECEDENCE_UNARY, MISSING_IF_ANY_ARGUMENT_IS_MISSING};

    // These are for predicate
    // Surrogate's lua function is defined as an "or" to allow it to be expressed as A or B or C... iff the type is not bool
    const Definition surrogateFunction = {"or", SURROGATE_MACRO, PMMLDocument::TYPE_INVALID, LuaOutputter::PRECEDENCE_OR, MAYBE_MISSING_IF_ANY_ARGUMENT_IS_MISSING};
    const Definition xorFunction = {"~=", BOOLEAN_XOR, PMMLDocument::TYPE_INVALID, LuaOutputter::PRECEDENCE_EQUAL, MISSING_IF_ANY_ARGUMENT_IS_MISSING};

}

const Function::BuiltInDefinition * Function::findBuiltInFunctionDefinition(const char * pmmlFunction)
{
    auto found = std::equal_range(functionTable.table, functionTable.table + functionTableSize, pmmlFunction, PMMLDocument::stringIsBefore);
    if (found.first != found.second)
    {
        return found.first;
    }
    return nullptr;
}

namespace Function
{
    typedef std::unordered_set<std::string> NodeSet;
    
    static void gatherAllFunctionNames(const AstNode & node, NodeSet & out)
    {
        if (const char * thisFunctionName = node.function().luaFunction)
        {
            out.emplace(thisFunctionName);
        }
        for (const AstNode & child : node.children)
        {
            gatherAllFunctionNames(child, out);
        }
    }
// A is a "magic value" here as part of the approximation below... it keeps the error within 4 decimal places for all real values.
constexpr double magicValueForErf = 0.147;
#define MAGIC_VALUE_FOR_ERF "0.147"

    // This outputs the expression sgn(x) * sqrt(1 - exp( -x^2 * (4/pi + a * x^2) / (1 + a * x^2) ))
    // This is Winizki's approximation of the Error Function, it is used in various statistical functions below.
    PMMLDocument::ConstFieldDescriptionPtr writeErfGuts(AstBuilder & builder, PMMLDocument::ConstFieldDescriptionPtr xparam)
    {
        // START OF FIRST STATEMENT
        // erfValue = sqrt(1 - exp( -x^2 * (2 * M_2_PI + 0.147 * x*x) / (1 + 0.147 * x*x) );
        
        builder.constant(1);
        
        // -x^2
        builder.field(xparam);
        builder.field(xparam);
        builder.function(functionTable.names.times, 2);
        builder.function(unaryMinus, 1);
        
        // * ( 4/pi + A x^2 )
        builder.constant(M_2_PI * 2);
        builder.constant(MAGIC_VALUE_FOR_ERF, PMMLDocument::TYPE_NUMBER);
        builder.field(xparam);
        builder.field(xparam);
        builder.function(functionTable.names.times, 3);
        builder.function(functionTable.names.plus, 2);
        builder.function(functionTable.names.times, 2);
        
        // / (1 + A x^2)
        builder.constant(1);
        builder.constant(MAGIC_VALUE_FOR_ERF, PMMLDocument::TYPE_NUMBER);
        builder.field(xparam);
        builder.field(xparam);
        builder.function(functionTable.names.times, 3);
        builder.function(functionTable.names.plus, 2);
        builder.function(functionTable.names.divide, 2);

        builder.function(functionTable.names.exp, 1);
        builder.function(functionTable.names.minus, 2);
        
        builder.function(sqrtFunction, 1);
        
        PMMLDocument::ScopedVariableDefinitionStackGuard scope(builder.context());
        auto erfValue = scope.addDataField("erfValue", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_TEMPORARY, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.declare(erfValue, AstBuilder::HAS_INITIAL_VALUE);
        
        // END OF FIRST STATEMENT
        // START OF SECOND STATEMENT
        // if (x < 0) erfValue = -erfValue;
        
        builder.field(erfValue);
        builder.function(unaryMinus, 1);
        builder.assign(erfValue);
        
        builder.field(xparam);
        builder.constant(0);
        builder.function(functionTable.names.lessThan, 2);
        builder.ifChain(2);
        
        // END OF SECOND STATEMENT
        return erfValue;
    }
    
    typedef std::unordered_map<std::string, PMMLDocument::ConstFieldDescriptionPtr> Fixups;
    void applyDefinedFunctionToNodes(AstBuilder & builder, AstNode & node, const Fixups & fixups)
    {
        if (const char * thisFunctionName = node.function().luaFunction)
        {
            auto found = fixups.find(thisFunctionName);
            if (found != fixups.end())
            {
                builder.field(found->second);
                node.children.emplace_back(builder.topNode());
                builder.popNode();
            }
        }
        for (AstNode & child : node.children)
        {
            applyDefinedFunctionToNodes(builder, child, fixups);
        }
    }
}

// Some of our functions are not available as standard Lua functions. This creates them as local functions defined in Lua.
bool Function::prologue(AstBuilder & builder)
{
    NodeSet allFunctionNames;
    gatherAllFunctionNames(builder.topNode(), allFunctionNames);
    
    Fixups fixups;
    int added = 0;
    if (allFunctionNames.count("elliott"))
    {
        // This function is needed for some neural networks.
        PMMLDocument::ScopedVariableDefinitionStackGuard scope(builder.context());
        auto zparam = scope.addDataField("Z", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_PARAMETER, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.field(zparam);
        
        // activation(Z) = Z/(1+|Z|)
        builder.field(zparam);
        builder.constant(1);
        builder.field(zparam);
        builder.function(functionTable.names.abs, 1);
        builder.function(functionTable.names.plus, 2);
        builder.function(functionTable.names.divide, 2);
        
        builder.lambda(1);
        
        auto def = scope.addDataField("elliott", PMMLDocument::TYPE_LAMBDA, PMMLDocument::ORIGIN_PARAMETER, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.declare(def, AstBuilder::HAS_INITIAL_VALUE);
        fixups.emplace(def->luaName, def);
        added++;
    }
    
    if (allFunctionNames.count("stdNormalCDF"))
    {
        // This function is also needed for some regressison model
        PMMLDocument::ScopedVariableDefinitionStackGuard scope(builder.context());
        auto xparam = scope.addDataField("X", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_PARAMETER, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.field(xparam);
        
        auto xvar = scope.addDataField("x", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_TEMPORARY, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.field(xparam);
        builder.constant(sqrt(2.0));
        builder.function(functionTable.names.divide, 2);
        builder.declare(xvar, AstBuilder::HAS_INITIAL_VALUE);
        
        auto erfValue = writeErfGuts(builder, xvar);
        
        builder.field(erfValue);
        builder.constant(1);
        builder.function(functionTable.names.plus, 2);
        builder.constant(0.5);
        builder.function(functionTable.names.times, 2);
        
        builder.block(4);
        
        builder.lambda(1);
        
        auto def = scope.addDataField("stdNormalCDF", PMMLDocument::TYPE_LAMBDA, PMMLDocument::ORIGIN_PARAMETER, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.declare(def, AstBuilder::HAS_INITIAL_VALUE);
        fixups.emplace(def->luaName, def);
        added++;
    }
    
    if (allFunctionNames.count("stdNormalIDF"))
    {
        double TwoOverPiA = M_2_PI / magicValueForErf;
        PMMLDocument::ScopedVariableDefinitionStackGuard scope(builder.context());
        auto pParam = scope.addDataField("p", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_PARAMETER, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.field(pParam);
        
        auto logOneMinusXSquare = scope.addDataField("logOneMinusXSquare", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_TEMPORARY, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.constant(1);
        builder.constant(2);
        builder.field(pParam);
        builder.function(functionTable.names.times, 2);
        builder.constant(1);
        builder.function(functionTable.names.minus, 2);
        builder.constant(2);
        builder.function(functionTable.names.pow, 2);
        builder.function(functionTable.names.minus, 2);
        builder.function(functionTable.names.ln, 1);
        builder.declare(logOneMinusXSquare, AstBuilder::HAS_INITIAL_VALUE);
        
        auto chunkybit = scope.addDataField("chunkybit", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_TEMPORARY, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.constant(TwoOverPiA);
        builder.field(logOneMinusXSquare);
        builder.constant(2);
        builder.function(functionTable.names.divide, 2);
        builder.function(functionTable.names.plus, 2);
        builder.declare(chunkybit, AstBuilder::HAS_INITIAL_VALUE);
        
        auto invErf = scope.addDataField("invErf", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_TEMPORARY, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.field(chunkybit);
        builder.field(chunkybit);
        builder.function(functionTable.names.times, 2);
        builder.field(logOneMinusXSquare);
        builder.constant(MAGIC_VALUE_FOR_ERF, PMMLDocument::TYPE_NUMBER);
        builder.function(functionTable.names.divide, 2);
        builder.function(functionTable.names.minus, 2);
        builder.function(sqrtFunction, 1);
        builder.field(chunkybit);
        builder.function(functionTable.names.minus, 2);
        builder.function(sqrtFunction, 1);
        builder.declare(invErf, AstBuilder::HAS_INITIAL_VALUE);
        
        builder.field(invErf);
        builder.function(unaryMinus, 1);
        builder.assign(invErf);
        builder.field(pParam);
        builder.constant(0.5);
        builder.function(functionTable.names.lessThan, 2);
        builder.ifChain(2);
        
        builder.constant(sqrt(2.0));
        builder.field(invErf);
        builder.function(functionTable.names.times, 2);
        
        builder.block(5);
        
        builder.lambda(1);
        
        auto def = scope.addDataField("stdNormalIDF", PMMLDocument::TYPE_LAMBDA, PMMLDocument::ORIGIN_PARAMETER, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.declare(def, AstBuilder::HAS_INITIAL_VALUE);
        fixups.emplace(def->luaName, def);
        added++;
    }
    
    if (allFunctionNames.count("erf"))
    {
        PMMLDocument::ScopedVariableDefinitionStackGuard scope(builder.context());
        auto xParam = scope.addDataField("x", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_PARAMETER, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.field(xParam);
        auto erfValue = writeErfGuts(builder, xParam);
        builder.field(erfValue);
        
        builder.block(3);
        builder.lambda(1);
        
        auto def = scope.addDataField("erf", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_PARAMETER, PMMLDocument::OPTYPE_CONTINUOUS);
        builder.declare(def, AstBuilder::HAS_INITIAL_VALUE);
        fixups.emplace(def->luaName, def);
        added++;
    }
    
    if (added > 1)
    {
        builder.block(added);
    }
    
    if (!fixups.empty())
    {
        // Put the main body on the top of the stack.
        builder.swapNodes(-1, -2);
        applyDefinedFunctionToNodes(builder, builder.topNode(), fixups);
        // Put them back into the correct order.
        builder.swapNodes(-1, -2);
    }
    
    return added > 0;
}


Function::CustomDefinition::CustomDefinition(const PMMLDocument::ConstFieldDescriptionPtr & d, PMMLDocument::FieldType ot,
                                             const Definition * ld,
                                             std::vector<PMMLDocument::FieldType> && parameterList) :
    outputType(ot),
    lambdaDefinition(ld),
    functionVariable(d),
    parameters(std::move(parameterList))
{
}
