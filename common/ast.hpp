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
//
//  The AST should be in "relaxed SSA" form. Try to make it SSA, if you can't, just don't set a value that wasn't null to null and nothing should break.

#ifndef ast_hpp
#define ast_hpp

#include <list>
#include <string>
#include "pmmldocumentdefs.hpp"
#include "function.hpp"
#include "conversioncontext.hpp"
#include <assert.h>

namespace Analyser
{
    enum TrivialValue
    {
        ALWAYS_TRUE,
        ALWAYS_FALSE,
        RUNTIME_EVALUATION_NEEDED
    };
}

struct AstNode
{
    typedef std::vector<AstNode> Children;

    AstNode(unsigned int nodeID,
            const Function::Definition & f,
            PMMLDocument::FieldType t,
            std::string && cont,
            Children && child) :
        id(nodeID),
        children(std::move(child)),
        pFunction(&f),
        content(std::move(cont)),
        type(t),
        coercedType(t)
    {
    }

    AstNode(unsigned int nodeID,
            const Function::Definition & f,
            PMMLDocument::ConstFieldDescriptionPtr t,
            Children && child);


    // This is a special type to allow this thing to be set to and check an "invalid" state.
    struct invalidNode_t {};
    static constexpr invalidNode_t invalidNode = {};
    AstNode(invalidNode_t) :
        id(0),
        pFunction(nullptr)
    {
    }
    
    bool operator==(invalidNode_t)
    {
        return pFunction == nullptr;
    }
    
    const Function::Definition & function() const
    {
        assert(pFunction);
        return *pFunction;
    }
    
    void simplifyTrivialValue(Analyser::TrivialValue triv);

    unsigned int id;
    Children children;
    const Function::Definition * pFunction;
    // The value for a literal or the name for a field. Empty for functions.
    std::string content;
    PMMLDocument::FieldType type;
    PMMLDocument::FieldType coercedType;
    PMMLDocument::ConstFieldDescriptionPtr fieldDescription;
};

// This is a Reverse Polish Notation builder of AST trees. It allows trees to be built easily.
class AstBuilder
{
public:
    PMMLDocument::ConversionContext & context() { return m_context; }
    const PMMLDocument::ConversionContext & context() const { return m_context; }
    // These all push 1 value to the stack
    void field(PMMLDocument::ConstFieldDescriptionPtr description);
    void field(const PMMLDocument::MiningField * miningField);
    void constant(const char * constantValue, PMMLDocument::FieldType type);
    void constant(const std::string & constantValue, PMMLDocument::FieldType type);
    void constant(const char * constantValue, size_t length, PMMLDocument::FieldType type);
    void constant(int literal);
    void constant(float literal);
    void constant(double literal);
    // This pops 1 value from the stack and pushes one. It will return the replacementValue when it would have returned unknown.
    void defaultValue(const char * replacementValue);
    // This pops nArgs values from the stack and then pushes 1 value to the stack
    void function(const Function::Definition & definition, size_t nArgs);
    // This pops nArgs values from the stack and then pushes 1 value to the stack
    void customNode(const Function::Definition & definition, PMMLDocument::FieldType type, const std::string & content, size_t nArgs);
    // This pops 1 value off the stack (if hasInitialValue is true) or 0 otherwise. It pushes 1 value to the stack.
    enum HasInitialValue
    {
        HAS_INITIAL_VALUE,
        NO_INITIAL_VALUE
    };
    void declare(PMMLDocument::ConstFieldDescriptionPtr description, HasInitialValue hasInitialValue);
    // This pops 1 value and pushes 1 value to the stack.
    void assign(PMMLDocument::ConstFieldDescriptionPtr description);
    // This pops 1 + nIndirections values and pushes 1 value to the stack.
    void assignIndirect(PMMLDocument::ConstFieldDescriptionPtr description, size_t nIndirections);
    // This pops nIndirections values and pushes 1 value to the stack.
    void fieldIndirect(PMMLDocument::ConstFieldDescriptionPtr description, size_t nIndirections);
    // This pops nInstructions from the stack and pushes 1 instruction
    void block(size_t nInstructions);
    // This pops nInstructions from the stack and pushes 1 instruction
    void ifChain(size_t nInstructions);
    // This pops nArguments + 1 from the stack and pushes 1 instruction
    void lambda(size_t nArguments);
    // This pushes an already-created node back to the top of the stack
    void pushNode(const AstNode & toPush)
    {
        m_stack.push_back(toPush);
    }
    void pushNode(AstNode && toPush)
    {
        m_stack.push_back(std::move(toPush));
    }
    const AstNode & topNode() const;
    AstNode & topNode();
    // this pops 1 value from the stack.
    AstNode popNode()
    {
        AstNode out = std::move(m_stack.back());
        m_stack.pop_back();
        return out;
    }
    void popNodesIntoVector(std::vector<AstNode> & nodes, size_t nInstructions);
    void swapNodes(int a, int b);
    bool coerceToSameType(size_t nEntries);
    bool coerceToSpecificTypes(size_t nEntries, const PMMLDocument::FieldType * types);
    size_t stackSize() const { return m_stack.size(); }
    
    static const Function::Definition CONSTANT_DEF;
    static const Function::Definition FIELD_DEF;
    static const Function::Definition BLOCK_DEF;
    static const Function::Definition IF_CHAIN_DEF;
    static const Function::Definition ASSIGNMENT_DEF;
    static const Function::Definition DECLARATION_DEF;
    static const Function::Definition DEFAULT_DEF;
    static const Function::Definition LAMBDA_DEF;
    static const Function::Definition NIL_DEF;

private:
    PMMLDocument::ConversionContext m_context;
    std::vector<AstNode> m_stack;
    unsigned int m_nextID = 0;
};

#ifdef DEBUG_AST_BUILDING

class AstBuilderAssertGuard
{
    const AstBuilder & m_builder;
    const size_t m_startingStackSize;
public:
    AstBuilderAssertGuard(const AstBuilder & builder) : m_builder(builder), m_startingStackSize(builder.stackSize()) {}
    ~AstBuilderAssertGuard();
};

#define ASSERT_AST_BUILDER_ONE_NEW_NODE(builder) AstBuilderAssertGuard astGuard##__COUNTER__ (builder)

#else

#define ASSERT_AST_BUILDER_ONE_NEW_NODE(builder)

#endif

#endif /* ast_hpp */
