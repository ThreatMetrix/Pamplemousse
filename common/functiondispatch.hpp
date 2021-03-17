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
//  Created by Caleb Moore on 14/7/20.
//

#ifndef functiondispatch_hpp
#define functiondispatch_hpp

#include <function.hpp>

namespace Function
{
    class FunctionTypeBase{};
    class UnaryOperator : public FunctionTypeBase{};
    class NotOperator : public UnaryOperator{};
    class Operator : public FunctionTypeBase{};
    class Comparison : public Operator{};
    class BooleanXor : public Operator{};
    class Functionlike : public FunctionTypeBase{};
    class RoundMacro : public Functionlike{};
    class Log10Macro : public Functionlike{};
    class MeanMacro : public FunctionTypeBase{};
    class TernaryMacro : public FunctionTypeBase{};
    class BoundMacro : public FunctionTypeBase{};
    class IsMissing : public FunctionTypeBase{};
    class IsNotMissing : public FunctionTypeBase{};
    class IsIn : public FunctionTypeBase{};
    class SubstringMacro : public FunctionTypeBase{};
    class TrimBlank : public FunctionTypeBase{};
    class Constant : public FunctionTypeBase{};
    class FieldRef : public FunctionTypeBase{};
    class SurrogateMacro : public FunctionTypeBase{};
    class BooleanAndOr : public FunctionTypeBase{};
    class BooleanAnd : public BooleanAndOr{};
    class BooleanOr : public BooleanAndOr{};
    class DefaultMacro : public FunctionTypeBase{};
    class ThresholdMacro : public FunctionTypeBase{};
    class Block : public FunctionTypeBase{};
    class DeclartionOrAssignment : public FunctionTypeBase{};
    class Declartion : public DeclartionOrAssignment{};
    class Assignment : public DeclartionOrAssignment{};
    class IfChain : public FunctionTypeBase{};
    class MakeTuple : public FunctionTypeBase{};
    class Lambda : public FunctionTypeBase{};
    class RunLambda : public FunctionTypeBase{};
    class ReturnStatement : public FunctionTypeBase{};
    
    
    template<typename ReturnType, class Dispatcher, typename... Ts>
    ReturnType dispatchFunctionType(Dispatcher & dispatcher, FunctionType type, Ts&... args)
    {
        switch(type)
        {
            case UNARY_OPERATOR:
                return dispatcher.process(UnaryOperator(), args...);
                
            case NOT_OPERATOR:
                return dispatcher.process(NotOperator(), args...);
                
            case OPERATOR:
                return dispatcher.process(Operator(), args...);
                
            case FUNCTIONLIKE:
                return dispatcher.process(Functionlike(), args...);
                
            case MEAN_MACRO:
                return dispatcher.process(MeanMacro(), args...);
                
            case ROUND_MACRO:
                return dispatcher.process(RoundMacro(), args...);
                
            case TERNARY_MACRO:
                return dispatcher.process(TernaryMacro(), args...);
                
            case BOUND_MACRO:
                return dispatcher.process(BoundMacro(), args...);
                
            case LOG10_MACRO:
                return dispatcher.process(Log10Macro(), args...);
                
            case COMPARISON:
                return dispatcher.process(Comparison(), args...);
                
            case IS_MISSING:
                return dispatcher.process(IsMissing(), args...);
                
            case IS_NOT_MISSING:
                return dispatcher.process(IsNotMissing(), args...);
                
            case IS_IN:
                return dispatcher.process(IsIn(), args...);
                
            case SUBSTRING_MACRO:
                return dispatcher.process(SubstringMacro(), args...);
                
            case TRIMBLANK_MACRO:
                return dispatcher.process(TrimBlank(), args...);
                
            case CONSTANT:
                return dispatcher.process(Constant(), args...);
                
            case FIELD_REF:
                return dispatcher.process(FieldRef(), args...);
                
            case SURROGATE_MACRO:
                return dispatcher.process(SurrogateMacro(), args...);
                
            case BOOLEAN_AND:
                return dispatcher.process(BooleanAnd(), args...);
                
            case BOOLEAN_OR:
                return dispatcher.process(BooleanOr(), args...);
                
            case BOOLEAN_XOR:
                return dispatcher.process(BooleanXor(), args...);
                
            case DEFAULT_MACRO:
                return dispatcher.process(DefaultMacro(), args...);
                
            case THRESHOLD_MACRO:
                return dispatcher.process(ThresholdMacro(), args...);
                
            case BLOCK:
                return dispatcher.process(Block(), args...);
                
            case DECLARATION:
                return dispatcher.process(Declartion(), args...);
                
            case ASSIGNMENT:
                return dispatcher.process(Assignment(), args...);
                
            case IF_CHAIN:
                return dispatcher.process(IfChain(), args...);
                
            case MAKE_TUPLE:
                return dispatcher.process(MakeTuple(), args...);
                
            case LAMBDA:
                return dispatcher.process(Lambda(), args...);
                
            case RUN_LAMBDA:
                return dispatcher.process(RunLambda(), args...);
                
            case RETURN_STATEMENT:
                return dispatcher.process(ReturnStatement(), args...);
                
            case UNSUPPORTED:
                break;
        }
        return ReturnType();
    }
}

#endif /* functiondispatch_h */
