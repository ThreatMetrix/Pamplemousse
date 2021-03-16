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

#ifndef function_hpp
#define function_hpp

#include "pmmldocumentdefs.hpp"

#include <vector>

class LuaOutputter;

namespace PMMLDocument
{
    class ScopedVariableDefinitionStackGuard;
}

namespace tinyxml2
{
    class XMLElement;
}

struct AstNode;
class AstBuilder;

namespace Function
{
    enum FunctionType
    {
        UNARY_OPERATOR,
        NOT_OPERATOR,
        OPERATOR,
        FUNCTIONLIKE,
        MEAN_MACRO,
        ROUND_MACRO,
        TERNARY_MACRO,
        BOUND_MACRO,
        LOG10_MACRO,
        COMPARISON,
        IS_MISSING,
        IS_NOT_MISSING,
        IS_IN,
        SUBSTRING_MACRO,
        TRIMBLANK_MACRO,
        CONSTANT,
        FIELD_REF,
        SURROGATE_MACRO,
        BOOLEAN_AND,
        BOOLEAN_OR,
        BOOLEAN_XOR,
        DEFAULT_MACRO,
        THRESHOLD_MACRO,
        BLOCK,
        DECLARATION,
        ASSIGNMENT,
        IF_CHAIN,
        MAKE_TUPLE,
        LAMBDA,
        RUN_LAMBDA,
        RETURN_STATEMENT,
        UNSUPPORTED
    };
    enum MissingValueRule
    {
        NEVER_MISSING,
        MISSING_IF_ANY_ARGUMENT_IS_MISSING,
        MAYBE_MISSING_IF_ANY_ARGUMENT_IS_MISSING,
        MAYBE_MISSING
    };

    struct Definition
    {
        constexpr Definition(const char * l, FunctionType t, PMMLDocument::FieldType o, int ol, MissingValueRule n) :
            luaFunction(l),
            functionType(t),
            outputType(o),
            operatorLevel(ol),
            missingValueRule(n)
        {}
        const char * luaFunction;
        FunctionType functionType;
        PMMLDocument::FieldType outputType;
        int operatorLevel;
        MissingValueRule missingValueRule;
    };

    struct BuiltInDefinition : public Definition
    {
        operator const char*() const
        {
            return pmmlFunction;
        }
        BuiltInDefinition(const char * p, const char * l, FunctionType t, PMMLDocument::FieldType o, int ol, MissingValueRule n, size_t mn, size_t mx) :
            Definition(l, t, o, ol, n),
            pmmlFunction(p),
            minArgs(mn),
            maxArgs(mx)
        {}
        const char * pmmlFunction;
        size_t minArgs;
        size_t maxArgs;
    };
    
    // Union to allow built in functions to be referenced by string and identifier
    // Must be kept in sync with table in function.cpp
    const size_t functionTableSize = 66;
    union FunctionTable
    {
        BuiltInDefinition table[functionTableSize];
        struct
        {
            BuiltInDefinition times;
            BuiltInDefinition plus;
            BuiltInDefinition minus;
            BuiltInDefinition divide;
            BuiltInDefinition abs;
            BuiltInDefinition acos;
            BuiltInDefinition fnAnd;
            BuiltInDefinition asin;
            BuiltInDefinition atan;
            BuiltInDefinition avg;
            BuiltInDefinition ceil;
            BuiltInDefinition concat;
            BuiltInDefinition cos;
            BuiltInDefinition cosh;
            BuiltInDefinition dateDaysSinceYear;
            BuiltInDefinition dateSecondsSinceMidnight;
            BuiltInDefinition dateSecondsSinceYear;
            BuiltInDefinition equal;
            BuiltInDefinition erf;
            BuiltInDefinition exp;
            BuiltInDefinition expm1;
            BuiltInDefinition floor;
            BuiltInDefinition formatDatetime;
            BuiltInDefinition formatNumber;
            BuiltInDefinition greaterOrEqual;
            BuiltInDefinition greaterThan;
            BuiltInDefinition ternary;
            BuiltInDefinition isIn;
            BuiltInDefinition isMissing;
            BuiltInDefinition isNotIn;
            BuiltInDefinition isNotMissing;
            BuiltInDefinition isNotValid;
            BuiltInDefinition isValid;
            BuiltInDefinition lessOrEqual;
            BuiltInDefinition lessThan;
            BuiltInDefinition log10;
            BuiltInDefinition ln;
            BuiltInDefinition lowercase;
            BuiltInDefinition matches;
            BuiltInDefinition max;
            BuiltInDefinition median;
            BuiltInDefinition min;
            BuiltInDefinition modulo;
            BuiltInDefinition normalCDF;
            BuiltInDefinition normalIDF;
            BuiltInDefinition normalPDF;
            BuiltInDefinition fnNot;
            BuiltInDefinition notEqual;
            BuiltInDefinition fnOr;
            BuiltInDefinition pow;
            BuiltInDefinition product;
            BuiltInDefinition replace;
            BuiltInDefinition round;
            BuiltInDefinition sin;
            BuiltInDefinition sinh;
            BuiltInDefinition stdNormalCDF;
            BuiltInDefinition stdNormalIDF;
            BuiltInDefinition stdNormalPDF;
            BuiltInDefinition substring;
            BuiltInDefinition sum;
            BuiltInDefinition tan;
            BuiltInDefinition tanh;
            BuiltInDefinition threshold;
            BuiltInDefinition trimBlanks;
            BuiltInDefinition uppercase;
            BuiltInDefinition xmodulo;
        } names;
    };
    
    // These are functions that are used internally, but not actually accessable as built-in functions.
    extern const FunctionTable functionTable;
    extern const Definition boundFunction;
    extern const Definition unaryMinus;
    extern const Definition makeTuple;
    extern const Definition runLambda;
    extern const Definition runLambdaArgsMissing;
    extern const Definition runLambdaNeverMissing;
    extern const Definition sqrtFunction;
    
    extern const Definition surrogateFunction;
    extern const Definition xorFunction;
    
    extern const Definition sortTableDef;
    extern const Definition insertToTableDef;
    extern const Definition listLengthDef;
    
    struct CustomDefinition
    {
        CustomDefinition(const PMMLDocument::ConstFieldDescriptionPtr & d, PMMLDocument::FieldType ot,
                         const Definition * ld, std::vector<PMMLDocument::FieldType> && parameterList);
        PMMLDocument::FieldType outputType;
        const Definition * lambdaDefinition;
        PMMLDocument::ConstFieldDescriptionPtr functionVariable;
        std::vector<PMMLDocument::FieldType> parameters;
    };

    const BuiltInDefinition * findBuiltInFunctionDefinition(const char * pmmlFunction);

    bool parse(AstBuilder & builder, const tinyxml2::XMLElement * node);
    bool prologue(AstBuilder & builder);
}

#endif /* function_hpp */
