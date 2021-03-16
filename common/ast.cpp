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
//  Created by Caleb Moore on 7/11/18.
//
//  This function allows a target language independent representation of a part of a PMML document.
//  All syntax here are within PMML's own terms, namely, unknown values propagate in the way they do in PMML.
//  This tree is taken by a converter (LuaConverter) and used to create the actual code.
//
//  The rationale is to seperate the actual code generation (which is nasty) from the parsing and logic code (which is fairly simple)
//  It also reduces the risk for screwups regarding operator precedence as order of operations is explicit in this form.
//  Finally, it allows the code generator a bit more depth of context, as well as re-using logic.

#include "ast.hpp"
#include "analyser.hpp"
#include "conversioncontext.hpp"
#include "luaconverter/luaoutputter.hpp"
#include <sstream>
#include <algorithm>
#include <assert.h>

AstNode::AstNode(unsigned int nodeID,
        const Function::Definition & f,
        PMMLDocument::ConstFieldDescriptionPtr t,
        Children && child) :
id(nodeID),
children(std::move(child)),
pFunction(&f),
content(t->luaName),
type(t->field.dataType),
coercedType(t->field.dataType),
fieldDescription(t)
{
    assert(t);
}

// Make something trivial into a simple boolean constant.
void AstNode::simplifyTrivialValue(Analyser::TrivialValue triv)
{
    if (triv != Analyser::RUNTIME_EVALUATION_NEEDED &&
        pFunction != &AstBuilder::CONSTANT_DEF)
    {
        children.clear();
        pFunction = &AstBuilder::CONSTANT_DEF;
        content = triv == Analyser::ALWAYS_TRUE ? "true" : "false";
    }
}

const Function::Definition AstBuilder::CONSTANT_DEF =
{
    nullptr,
    Function::CONSTANT,
    PMMLDocument::TYPE_INVALID,
    LuaOutputter::PRECEDENCE_TOP, Function::NEVER_MISSING
};

const Function::Definition AstBuilder::FIELD_DEF =
{
     nullptr,
    Function::FIELD_REF,
    PMMLDocument::TYPE_INVALID,
    LuaOutputter::PRECEDENCE_TOP, Function::MAYBE_MISSING
};

const Function::Definition AstBuilder::BLOCK_DEF =
{
    nullptr,
    Function::BLOCK,
    PMMLDocument::TYPE_VOID,
    LuaOutputter::PRECEDENCE_TOP, Function::NEVER_MISSING
};

const Function::Definition AstBuilder::IF_CHAIN_DEF =
{
    nullptr,
    Function::IF_CHAIN,
    PMMLDocument::TYPE_VOID,
    LuaOutputter::PRECEDENCE_TOP, Function::NEVER_MISSING
};

const Function::Definition AstBuilder::ASSIGNMENT_DEF =
{
    nullptr,
    Function::ASSIGNMENT,
    PMMLDocument::TYPE_VOID,
    LuaOutputter::PRECEDENCE_TOP, Function::NEVER_MISSING
};

const Function::Definition AstBuilder::DECLARATION_DEF =
{
    nullptr,
    Function::DECLARATION,
    PMMLDocument::TYPE_VOID,
    LuaOutputter::PRECEDENCE_TOP, Function::NEVER_MISSING
};

// This internal function is a represenation of the application of a default value
const Function::Definition AstBuilder::DEFAULT_DEF =
{
    nullptr,
    Function::DEFAULT_MACRO,
    PMMLDocument::TYPE_INVALID,
    LuaOutputter::PRECEDENCE_OR, Function::NEVER_MISSING
};

const Function::Definition AstBuilder::LAMBDA_DEF =
{
    nullptr,
    Function::LAMBDA,
    PMMLDocument::TYPE_INVALID,
    LuaOutputter::PRECEDENCE_PARENTHESIS, Function::NEVER_MISSING
};

void AstBuilder::field(PMMLDocument::ConstFieldDescriptionPtr description)
{
    m_stack.emplace_back(m_nextID++, FIELD_DEF, description, AstNode::Children());
}

void AstBuilder::field(const PMMLDocument::MiningField * miningField)
{
    switch(miningField->outlierTreatment)
    {
        case PMMLDocument::OUTLIER_TREATMENT_AS_EXTREME_VALUES:
            field(miningField->variable);
            constant(miningField->maxValue);
            function(Function::functionTable.names.min, 2);
            constant(miningField->minValue);
            function(Function::functionTable.names.max, 2);
            break;
        case PMMLDocument::OUTLIER_TREATMENT_AS_MISSING_VALUES:
            field(miningField->variable);
            constant(miningField->minValue);
            function(Function::functionTable.names.greaterOrEqual, 2);
            field(miningField->variable);
            constant(miningField->maxValue);
            function(Function::functionTable.names.lessOrEqual, 2);
            function(Function::functionTable.names.fnAnd, 2);
            field(miningField->variable);
            function(Function::boundFunction, 2);
            break;
        default:
            field(miningField->variable);
    }
    if (miningField->hasReplacementValue)
    {
        defaultValue(miningField->replacementValue.c_str());
    }
}

void AstBuilder::fieldIndirect(PMMLDocument::ConstFieldDescriptionPtr description, size_t nIndirections)
{
    std::vector<AstNode> children;
    popNodesIntoVector(children, nIndirections);
    m_stack.emplace_back(m_nextID++, FIELD_DEF, description, std::move(children));
    PMMLDocument::FieldType type = m_stack.back().type;
    if (type == PMMLDocument::TYPE_TABLE)
    {
        m_stack.back().type = PMMLDocument::TYPE_NUMBER;
        m_stack.back().coercedType = PMMLDocument::TYPE_NUMBER;
    }
    else if (type == PMMLDocument::TYPE_STRING_TABLE)
    {
        m_stack.back().type = PMMLDocument::TYPE_STRING;
        m_stack.back().coercedType = PMMLDocument::TYPE_STRING;
    }
}

void AstBuilder::constant(const char * constantValue, PMMLDocument::FieldType type)
{
    m_stack.emplace_back(m_nextID++, CONSTANT_DEF, type, std::string(constantValue), AstNode::Children());
}

void AstBuilder::constant(const std::string & constantValue, PMMLDocument::FieldType type)
{
    m_stack.emplace_back(m_nextID++, CONSTANT_DEF, type, std::string(constantValue), AstNode::Children());
}

void AstBuilder::constant(const char * constantValue, size_t length, PMMLDocument::FieldType type)
{
    m_stack.emplace_back(m_nextID++, CONSTANT_DEF, type, std::string(constantValue, length), AstNode::Children());
}

void AstBuilder::constant(int literal)
{
    std::stringstream ss;
    ss << literal;
    constant(ss.str(), PMMLDocument::TYPE_NUMBER);
}

void AstBuilder::constant(float literal)
{
    std::stringstream ss;
    ss << literal;
    constant(ss.str(), PMMLDocument::TYPE_NUMBER);
}

void AstBuilder::constant(double literal)
{
    std::stringstream ss;
    ss.precision(17);
    ss << literal;
    constant(ss.str(), PMMLDocument::TYPE_NUMBER);
}

const AstNode & AstBuilder::topNode() const
{
    assert(m_stack.size() >= 1);
    return m_stack.back();
}

AstNode & AstBuilder::topNode()
{
    assert(m_stack.size() >= 1);
    return m_stack.back();
}

void AstBuilder::popNodesIntoVector(std::vector<AstNode> & nodes, size_t nInstructions)
{
    assert(m_stack.size() >= nInstructions);
    std::vector<AstNode>::iterator endIterator = m_stack.end();
    std::advance(endIterator, -nInstructions);
    
    nodes.reserve(nInstructions);
    std::move(endIterator, m_stack.end(), std::back_inserter(nodes));
    for (size_t i = 0; i < nInstructions; i++)
    {
        m_stack.pop_back();
    }
}

void AstBuilder::defaultValue(const char * replacementValue)
{
    std::vector<AstNode> children;
    popNodesIntoVector(children, 1);
    PMMLDocument::FieldType type = children.front().type;
    m_stack.emplace_back(m_nextID++, DEFAULT_DEF, type, std::string(replacementValue), std::move(children));
}

void AstBuilder::function(const Function::Definition & definition, size_t nArgs)
{
    assert(definition.functionType != Function::UNSUPPORTED);
    PMMLDocument::FieldType dataType = definition.outputType;
    std::vector<AstNode> children;
    popNodesIntoVector(children, nArgs);
    // If type is invalid, that means this function's output is based on its input.
    // Currently this looks through the children in reverse as ternaries/bounds functions put the predicate first and the return value last
    // If we ever distinguish between float and int, this will need to be more complex.
    for (auto iter = children.crbegin(); iter !=children.crend() && dataType == PMMLDocument::TYPE_INVALID; ++iter)
    {
        dataType = iter->coercedType;
    }

    m_stack.emplace_back(m_nextID++, definition, dataType, std::string(), std::move(children));
}

void AstBuilder::customNode(const Function::Definition & definition, PMMLDocument::FieldType type, const std::string & content, size_t nArgs)
{
    std::vector<AstNode> children;
    popNodesIntoVector(children, nArgs);
    m_stack.emplace_back(m_nextID++, definition, type, std::string(content), std::move(children));
}

// This adds a variable declaration (in Lua, a local)
// If "hasInitialValue" is true, it pops an extra value and adds an assignment.
void AstBuilder::declare(PMMLDocument::ConstFieldDescriptionPtr description, HasInitialValue hasInitialValue)
{
    std::vector<AstNode> children;
    if (hasInitialValue == HAS_INITIAL_VALUE)
    {
        popNodesIntoVector(children, 1);
    }
    m_stack.emplace_back(m_nextID++, DECLARATION_DEF, description, std::move(children));
}

// This creates a direct assignment. The value is at the top, the variable is just under the top.
// That is to say:
// element_1 = element_0
void AstBuilder::assign(PMMLDocument::ConstFieldDescriptionPtr description)
{
    std::vector<AstNode> children;
    popNodesIntoVector(children, 1);
    m_stack.emplace_back(m_nextID++, ASSIGNMENT_DEF, description, std::move(children));
}

// This creates an indirect assignment. The value to be assigned is at the top of the stack, the variable is at nIndirections + 2. Between them are indirections.
// That is to say (when nodes = 2)
// element_3[element_2][element_1] = element_0
void AstBuilder::assignIndirect(PMMLDocument::ConstFieldDescriptionPtr description, size_t nIndirections)
{
    std::vector<AstNode> children;
    popNodesIntoVector(children, 1 + nIndirections);
    m_stack.emplace_back(m_nextID++, ASSIGNMENT_DEF, description, std::move(children));
}

// This outputs a "block"
// That is to say (where nInstruction = 3):
// element_2;
// element_1;
// element_0
// You can imagine it to be something like Go. The block's value is the final element of it (i.e. the top of the stack when you call this).
void AstBuilder::block(size_t nInstructions)
{
    std::vector<AstNode> children;
    popNodesIntoVector(children, nInstructions);
    const PMMLDocument::FieldType dataType = children.empty() ? PMMLDocument::TYPE_INVALID : children.back().type;
    m_stack.emplace_back(m_nextID++, BLOCK_DEF, dataType, std::string(), std::move(children));
}

// This outputs an "if chain" element.
// That is to say (where nInstructions = 5)
// if element_3 then
//   element_4
// else if element_1
//   element_2
// else
//   element_0
// Note, the conditions come AFTER their associated clause.
void AstBuilder::ifChain(size_t nInstructions)
{
    function(IF_CHAIN_DEF, nInstructions);
}

// This outputs a lambda element. The top node is the body of the lambda, the next nArguments elements are the parameters.
// That is to say (when nArguments = 2)
// [] (argument_2, argument_1) { return argument_0; }
void AstBuilder::lambda(size_t nArguments)
{
    std::vector<AstNode> children;
    popNodesIntoVector(children, nArguments + 1);
    const PMMLDocument::FieldType dataType = children.back().type;
    
    m_stack.emplace_back(m_nextID++, LAMBDA_DEF, dataType, std::string(), std::move(children));
}

// This method will change the type of the top nEntries elements of the stack to be the same type, i.e. the most permissive type
// out of all of them (as per the PMML standard). Also, bool is very special and may not be mixed with any type.
// It returns true if coersion is successful and false if coersion was unsuccessful.
bool AstBuilder::coerceToSameType(size_t nEntries)
{
    if (nEntries == 0)
    {
        return true;
    }
    std::vector<AstNode>::iterator startIterator = m_stack.end();
    std::advance(startIterator, -nEntries);
    // Pick the most permissive of all arguments
    PMMLDocument::FieldType type = std::min_element(startIterator, m_stack.end(), [](const AstNode & a, const AstNode & b)
    {
        return static_cast<int>(a.type) < static_cast<int>(b.type);
    })->type;

    
    // Boolean values are special and cannot be mixed with other data types
    bool isOK = (type == PMMLDocument::TYPE_BOOL) ||
        std::none_of(startIterator, m_stack.end(), [](const AstNode & a)
    {
        return a.type == PMMLDocument::TYPE_BOOL;
    });
    
    for (auto iter = startIterator; iter != m_stack.end(); ++iter)
    {
        iter->coercedType = type;
    }
    return isOK;
}

// This method will change the type of the top nEntries elements of the stack to be the types given in types.
// types must be at least nEntries long.
// It returns true if coersion is successful and false if coersion was unsuccessful.
bool AstBuilder::coerceToSpecificTypes(size_t nEntries, const PMMLDocument::FieldType * types)
{
    std::vector<AstNode>::iterator iter = m_stack.end();
    std::advance(iter, -nEntries);
    bool isOK = true;
    for (size_t i = 0; i < nEntries; ++i, ++iter)
    {
        if (types[i] != PMMLDocument::TYPE_INVALID)
        {
            if (static_cast<int>(types[i]) > static_cast<int>(iter->type) )
            {
                // Backward coersion is probably not going to work.
                isOK = false;
            }
            iter->coercedType = types[i];
        }
    }
    return isOK;
}

void AstBuilder::parsingError(const char * error_message, int line_num) const
{
    if (m_customErrorHook)
    {
        m_customErrorHook->error(error_message, line_num);
    }
    else
    {
        std::fprintf(stderr, "%s at %i\n", error_message, line_num);
    }
}

void AstBuilder::parsingError(const char * error_message, const char * error_param, int line_num) const
{
    if (m_customErrorHook)
    {
        m_customErrorHook->errorWithArg(error_message, error_param, line_num);
    }
    else
    {
        std::fprintf(stderr, "%s (%s) at %i\n", error_message, error_param, line_num);
    }
}

// This method swaps two nodes in the builder's stack
void AstBuilder::swapNodes(int a, int b)
{
    if (a < 0)
    {
        a = m_stack.size() + a;
    }
    if (b < 0)
    {
        b = m_stack.size() + b;
    }

    std::swap(m_stack[a], m_stack[b]);
}

#ifdef DEBUG_AST_BUILDING
AstBuilderAssertGuard::~AstBuilderAssertGuard()
{
    assert(m_builder.stackSize() == m_startingStackSize + 1);
}
#endif
