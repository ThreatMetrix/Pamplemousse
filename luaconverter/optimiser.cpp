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
//  Created by Caleb Moore on 25/11/18.
//
//  Lua only allows 200 local variables per function, which makes things like Neural Networks difficult
//  This system does two things to fix the problem.
//  The first and least important is removing dead code and inlining useless variables... this way the variable count goes down.
//  The second is working out which variables to "overflow", that is, to shove in an array and access indirectly.
//  It also culls unneeded branches and other rubbish that doesn't need to be in the final output, making the output faster and easier to read.

#include "optimiser.hpp"

#include "ast.hpp"
#include "analyser.hpp"
#include "conversioncontext.hpp"
#include "luaoutputter.hpp"
#include "functiondispatch.hpp"

#include <algorithm>
#include <limits>

enum VisitorResponse
{
    RESPONSE_CONTINUE,
    RESPONSE_KILL_NODE_AND_CONTINUE
};

static bool isSpecialVar(const PMMLDocument::FieldDescription * description)
{
    return description->origin == PMMLDocument::ORIGIN_SPECIAL;
}


template<bool maintainAssertions, typename ASTVisitor>
static VisitorResponse traverseNode(Analyser::AnalyserContext & ctx, AstNode & node, size_t & counter, ASTVisitor & visitor, Analyser::NonNoneAssertionStackGuard * parentAssertions);

template<bool maintainAssertions, typename ASTVisitor>
class AstTraverser
{
public:
    static void process(Function::IfChain, Analyser::AnalyserContext & ctx, AstNode & node, size_t & counter, ASTVisitor & visitor, Analyser::NonNoneAssertionStackGuard * parentAssertions)
    {
        size_t savedCounter = counter;
        counter++; // The conditions have their own counter value, which is different to the counter values of internal instructions. The conditions' counter value will still be in savedCounter.
        bool isProceduralBit = true; // Every second node is a block or other procedural instruction.
        Analyser::NonNoneAssertionStackGuard::AssertionIntersection intersection;
        Analyser::ChildAssertionIterator iter(ctx, node, maintainAssertions);
        for (; iter.valid(); ++iter)
        {
            auto & child = const_cast<AstNode &>(*iter);
            
            // We want to find out what the statements are all asserting
            Analyser::NonNoneAssertionStackGuard innerAssertions(ctx);
            VisitorResponse childResponse = traverseNode<maintainAssertions>(ctx, child, isProceduralBit ? counter : savedCounter, visitor, &innerAssertions);
            
            if (isProceduralBit && parentAssertions != nullptr)
            {
                // This bit copies the logic of AstNode::addAssertionsForCheck, but in here, to cut down on the crazy recursion. Unfortunately using AstNode::addAssertionsForCheck here is neat but makes conversion far too slow.
                if (iter.index() == 0)
                {
                    intersection.add(iter);
                    intersection.add(innerAssertions);
                }
                else
                {
                    intersection.intersect(iter, innerAssertions);
                }
            }
            
            // If the bit inside the if contains a normal instruction (not a block) bump the counter
            if (isProceduralBit && child.function().functionType != Function::BLOCK)
            {
                counter++;
            }
            
            // Because removing nodes from an if chain changes the meaning of the other nodes, just blank it out into an empty block
            // Remember not to remove until traversal has finished, to preserve consistency of IDs
            if (childResponse == RESPONSE_KILL_NODE_AND_CONTINUE)
            {
                child.pFunction = &AstBuilder::BLOCK_DEF;
                child.children.clear();
            }
            
            isProceduralBit = !isProceduralBit;
        }
        
        if (parentAssertions)
        {
            // Add a virtual else clause to the intersection if needed, to cover all paths.
            // See AstNode::addAssertionsForCheck
            if (node.children.size() % 2 == 0)
            {
                intersection.intersect(iter);
            }
            intersection.apply(*parentAssertions);
        }
    }
    
    
    static void process(Function::Lambda, Analyser::AnalyserContext & ctx, AstNode & node, size_t & counter, ASTVisitor & visitor, Analyser::NonNoneAssertionStackGuard *)
    {
        // Do not touch the parameters, skip to the body.
        if (!node.children.empty())
        {
            Analyser::NonNoneAssertionStackGuard innerAssertions(ctx);
            traverseNode<maintainAssertions>(ctx, node.children.back(), counter, visitor, &innerAssertions);
        }
    }
    
    static void process(Function::Block, Analyser::AnalyserContext & ctx, AstNode & node, size_t & counter, ASTVisitor & visitor, Analyser::NonNoneAssertionStackGuard * parentAssertions)
    {
        std::unordered_set<unsigned int> nodesToKill;
        // Don't use ChildAssertionIterator, it's simply too costly here, given the number of times we dart up and down the heirachy, particularly with if statements.
        for (auto & child : node.children)
        {
            if (traverseNode<maintainAssertions>(ctx, child, counter, visitor, parentAssertions) == RESPONSE_KILL_NODE_AND_CONTINUE)
            {
                nodesToKill.insert(child.id);
            }
            counter++;
        }
        
        if (!nodesToKill.empty())
        {
            auto iter = std::remove_if(node.children.begin(), node.children.end(), [&](const AstNode & child){ return nodesToKill.count(child.id) > 0; });
            node.children.erase(iter, node.children.end());
        }
    }

    static void process(Function::FunctionTypeBase, Analyser::AnalyserContext & ctx, AstNode & node, size_t & counter, ASTVisitor & visitor, Analyser::NonNoneAssertionStackGuard * parentAssertions)
    {
        std::unordered_set<unsigned int> nodesToKill;
        for (Analyser::ChildAssertionIterator iter(ctx, node, maintainAssertions); iter.valid(); ++iter)
        {
            auto & child = const_cast<AstNode &>(*iter);
            if (traverseNode<maintainAssertions>(ctx, child, counter, visitor, nullptr) == RESPONSE_KILL_NODE_AND_CONTINUE)
            {
                nodesToKill.insert(child.id);
            }
        }
        // This will potentially re-iterate the loop above, in order to find out what it's saying about the inputs.
        // However, these situations are typically one statement at the most, meaning
        if (parentAssertions)
        {
            parentAssertions->addAssertionsForCheck(node, Analyser::NO_ASSUMPTIONS);
        }
        
        if (!nodesToKill.empty())
        {
            auto iter = std::remove_if(node.children.begin(), node.children.end(), [&](const AstNode & child){ return nodesToKill.count(child.id) > 0; });
            node.children.erase(iter, node.children.end());
        }
    }
};

// This traverses an AST, node by node, calling the visitor when entering and leaving a statement.
// counter should be initialized to zero and is incremented for each statement, it is used for analysing variable usage.
// maintainAssertions is whether or not to populate the non none assertions when traversing. This is very useful for culling dead code, but is expensive.
template<bool maintainAssertions, typename ASTVisitor>
static VisitorResponse traverseNode(Analyser::AnalyserContext & ctx, AstNode & node, size_t & counter, ASTVisitor & visitor, Analyser::NonNoneAssertionStackGuard * parentAssertions)
{
    // If we don't want to maintain assertions, we have nothing to propagate up.
    if (maintainAssertions == false)
    {
        parentAssertions = nullptr;
    }
    size_t savedCounter = counter;
    visitor.enterNode(ctx, node, counter);
    
    AstTraverser<maintainAssertions, ASTVisitor> traverser;
    Function::dispatchFunctionType<void>(traverser, node.function().functionType, ctx, node, counter, visitor, parentAssertions);
    
    return visitor.exitNode(ctx, node, savedCounter);
}

template<bool maintainAssertions, typename ASTVisitor>
void traverseTree(Analyser::AnalyserContext & ctx, AstNode & node, ASTVisitor & visitor)
{
    size_t counter = 1;
    Analyser::NonNoneAssertionStackGuard assertions(ctx);
    traverseNode<maintainAssertions>(ctx, node, counter, visitor, &assertions);
}

static const size_t COUNT_UNINITIALIZED = 0;

struct VariableInfo
{
    size_t firstDeclared;
    size_t firstSet;
    size_t lastSet;
    size_t setNTimes;
    size_t firstUsed;
    size_t lastUsed;
    size_t usedNTimes;
    bool unmovable;

    VariableInfo(size_t counter, bool hasInit) :
        firstDeclared(counter),
        firstSet(hasInit ? counter : 0),
        lastSet(hasInit ? counter : 0),
        setNTimes(hasInit ? 1 : 0),
        firstUsed(0),
        lastUsed(0),
        usedNTimes(0),
        unmovable(false)
    {}

    void used(size_t counter, bool inLambda)
    {
        if (inLambda)
        {
            unmovable = true;
        }
        
        if (firstUsed == COUNT_UNINITIALIZED)
        {
            firstUsed = counter;
        }
        
        // We don't have particularly clever tracking of variables so typically we need many iterations to remove all dead code.
        // However, in the case of variables being incremented, don't count that as being used, which will allow dead code to be removed more quickly.
        if (lastSet != counter)
        {
            lastUsed = counter;
        }
        usedNTimes++;
    }

    void assign(size_t counter)
    {
        if (firstSet == COUNT_UNINITIALIZED)
        {
            firstSet = counter;
        }
        lastSet = counter;
        setNTimes++;
    }
};

typedef std::unordered_map<PMMLDocument::ConstFieldDescriptionPtr, VariableInfo> VariableInfoMap;
// This is a helpful structure for creating lists of variables sorted on a particular parameter. Used in both recycling and overflowing variables.
struct SortedVariableInfoReference
{
    size_t sortKey;
    VariableInfoMap::iterator infoMapReference;
    bool operator<(const SortedVariableInfoReference & other) const { return sortKey < other.sortKey; }
    SortedVariableInfoReference(size_t sort, VariableInfoMap::iterator iter) : sortKey(sort), infoMapReference(iter) {}
};

// This scans the AST to find what variables are being used where
class BuildVariableInfoMapVisitor
{
    VariableInfoMap & m_map;
    int inLambda;
public:
    BuildVariableInfoMapVisitor(VariableInfoMap & map, Analyser::AnalyserContext &) :
        m_map(map),
        inLambda(0)
    {
        map.clear();
    }
    
    void enterNode(Analyser::AnalyserContext &, AstNode & node, size_t counter)
    {
        Function::dispatchFunctionType<void>(*this, node.function().functionType, node, counter);
    }
    
    void process(Function::Lambda, AstNode &, size_t)
    {
        inLambda++;
    }
        
    void process(Function::FieldRef, AstNode & node, size_t counter)
    {
        auto variable = m_map.find(node.fieldDescription);
        if (variable != m_map.end())
        {
            variable->second.used(counter, inLambda > 0);
        }
        // We allow inputs to be undeclared and assumed to come in from the top.
        else if (node.fieldDescription->origin == PMMLDocument::ORIGIN_DATA_DICTIONARY)
        {
            auto inserted = m_map.emplace(std::piecewise_construct,
                                          std::forward_as_tuple(node.fieldDescription),
                                          std::forward_as_tuple(0, !node.children.empty()));
            inserted.first->second.used(counter, inLambda > 0);
        }
    }

    void process(Function::Declartion, AstNode & node, size_t counter)
    {
        m_map.emplace(std::piecewise_construct,
                      std::forward_as_tuple(node.fieldDescription),
                      std::forward_as_tuple(counter, !node.children.empty()));
    }
    
    void process(Function::Assignment, AstNode & node, size_t counter)
    {
        auto variable = m_map.find(node.fieldDescription);
        if (variable != m_map.end())
        {
            variable->second.assign(counter);
        }
    }

    void process(Function::Functionlike, AstNode & node, size_t counter)
    {
        // For custom functions and functions defined in Lua require their definitions to be evaluated to ensure they are still used.
        if (node.fieldDescription != nullptr)
        {
            auto variable = m_map.find(node.fieldDescription);
            if (variable != m_map.end())
            {
                variable->second.used(counter, true);
            }
        }
    }
    
    void process(Function::FunctionTypeBase, AstNode &, size_t)
    {
    }
        
    VisitorResponse exitNode(Analyser::AnalyserContext &, AstNode & node, size_t)
    {
        if (node.function().functionType == Function::LAMBDA)
        {
            inLambda--;
        }
        return RESPONSE_CONTINUE;
    }
};

// This automatically inlines not-particularly-useful variables into the expression that generated it. It only works for SSA and SSA-like variables.
class InlineVariableVisitor
{
    VariableInfoMap & m_map;
    std::unordered_map<std::string, std::pair<AstNode, int>> m_replacements;
    bool m_killedAnything;
    unsigned int currentDeclaration;
    int currentCost;
    // This is how many instructions it is willing to bloat by in order remove a variable.
    int m_priceOfVariable = 5;
    static constexpr int COST_OF_REF = 1;
    void clearCurrentDeclaration() { currentDeclaration = std::numeric_limits<unsigned int>::max(); }
public:
    InlineVariableVisitor(VariableInfoMap & map, int variablePrice) :
        m_map(map),
        m_killedAnything(false),
        m_priceOfVariable(variablePrice)
    {
        clearCurrentDeclaration();
    }
    
    void enterNode(Analyser::AnalyserContext &, AstNode & node, size_t counter)
    {
        switch (node.function().functionType)
        {
            case Function::DECLARATION:
                if (!isSpecialVar(node.fieldDescription.get()) && node.children.size() == 1)
                {
                    auto value = m_map.find(node.fieldDescription);
                    if (value->second.lastSet == counter)
                    {
                        // If this is an ordinary assignment and the variable is not re-assigned later, it is a candidate for inlining
                        // Mark it and start counting the cost
                        currentCost = 0;
                        currentDeclaration = node.id;
                    }
                }
                else
                {
                    currentCost += 1;
                }
                break;
            case Function::FIELD_REF:
                {
                    auto found = m_map.find(node.fieldDescription);
                    // If this field is set again after this, anything referring to it cannot be inlined
                    if (found != m_map.end() && found->second.lastSet > counter)
                    {
                        clearCurrentDeclaration();
                    }
                    currentCost += COST_OF_REF;
                }
                break;
                
            case Function::FUNCTIONLIKE:
                currentCost += 4;
                break;
                
            case Function::CONSTANT:
                currentCost += 1;
                break;
                
            default:
                currentCost += 1;
        }
    }
    
    VisitorResponse exitNode(Analyser::AnalyserContext &, AstNode & node, size_t)
    {
        if (node.function().functionType == Function::FIELD_REF)
        {
            // Rewrite already-repaced variable in field refs.
            auto found = m_replacements.find(node.content);
            if (found != m_replacements.end())
            {
                // Maintain the coerced type from where it is applied.
                const PMMLDocument::FieldType oldType = node.coercedType;
                node = found->second.first;
                node.coercedType = oldType;
                // Apply additional cost of the substitution back to this node.
                currentCost += (found->second.second - COST_OF_REF);
            }
        }
        else if (node.id == currentDeclaration)
        {
            auto found = m_map.find(node.fieldDescription);
            if (found != m_map.end())
            {
                int extraCost = currentCost - COST_OF_REF;
                // Work out if we are better off without this variable.
                // Subtract one from each count to make up for that we would be removing at one reference for every inlining
                if (found->second.usedNTimes == 0 || extraCost * int(found->second.usedNTimes - 1) <= m_priceOfVariable)
                {
                    m_replacements.emplace(node.content, std::make_pair(node.children.front(), extraCost));
                    m_killedAnything = true;
                    return RESPONSE_KILL_NODE_AND_CONTINUE;
                }
            }
        }
        return RESPONSE_CONTINUE;
    }
    bool hasKilledAnything() const { return m_killedAnything; }
};

// This removes code that will either never be executed, or if executed, will not change the outcome.
class RemoveDeadCodeVisitor
{
    const VariableInfoMap & m_map;
    bool m_killedAnything;
    struct StackValue
    {
        Function::FunctionType type;
        Analyser::TrivialValue triv;
    };
    std::vector<StackValue> m_stack;
public:
    RemoveDeadCodeVisitor(const VariableInfoMap & map) :
        m_map(map),
        m_killedAnything(false)
    {}
    
    void enterNode(Analyser::AnalyserContext &, AstNode & node, size_t)
    {
        StackValue newValue = {node.function().functionType, Analyser::RUNTIME_EVALUATION_NEEDED};
        m_stack.push_back(newValue);
    }
    
    VisitorResponse exitNode(Analyser::AnalyserContext & ctx, AstNode & node, size_t counter)
    {
        Analyser::TrivialValue triv = m_stack.back().triv;
        m_stack.pop_back();
        // Constants can never be simplified further.
        if (node.function().functionType == Function::CONSTANT)
        {
            return RESPONSE_CONTINUE;
        }
        
        if (triv == Analyser::RUNTIME_EVALUATION_NEEDED)
        {
            // We haven't been marked as trivial from below, check the node itself
            triv = ctx.checkIfTrivial(node);
        }
        
        // Now the top of the stack is the parent, which is what we want.
        if (triv != Analyser::RUNTIME_EVALUATION_NEEDED)
        {
            m_killedAnything = true;
            if (not m_stack.empty())
            {
                const Function::FunctionType type = m_stack.back().type;
                // And/or clauses can remove single values or can recursively remove themselves.
                if (type == Function::BOOLEAN_AND ||
                    type == Function::BOOLEAN_OR)
                {
                    // Propagate up true for OR and false for AND
                    if ((triv == Analyser::ALWAYS_FALSE) == (type == Function::BOOLEAN_AND))
                    {
                        m_stack.back().triv = triv;
                    }
                    return RESPONSE_KILL_NODE_AND_CONTINUE;
                }
            }
            
            // Replace with a boolean const at least.
            node.simplifyTrivialValue(triv);
            return RESPONSE_CONTINUE;
        }
        
        return Function::dispatchFunctionType<VisitorResponse>(*this, node.function().functionType, ctx, node, counter);
    }
    bool hasKilledAnything() const { return m_killedAnything; }
    
    VisitorResponse process(Function::IfChain, Analyser::AnalyserContext & ctx, AstNode & node, size_t)
    {
        // Look through the procedural bits to see if the last final clauses are empty (even nubered children are blocks)
        // Useless crap can be removed from the end, but not the beginning
        size_t lastUsefulProceduralBit = 0;
        for (size_t i = 0; i < node.children.size(); i += 2)
        {
            AstNode & child = node.children[i];
            if (child.function().functionType != Function::BLOCK ||
                !child.children.empty())
            {
                lastUsefulProceduralBit = i + 2;
            }
        }
        
        // If nothing is useful, kill the whole stupid node.
        if (lastUsefulProceduralBit == 0)
        {
            m_killedAnything = true;
            return RESPONSE_KILL_NODE_AND_CONTINUE;
        }
        
        // Otherwise, cull to the last non-empty block
        if (lastUsefulProceduralBit < node.children.size())
        {
            m_killedAnything = true;
            node.children.erase(node.children.begin() + lastUsefulProceduralBit, node.children.end());
        }
        
        // This second pass will find trivial predicates in the ifChain (odd numbered children are predicates)
        Analyser::NonNoneAssertionStackGuard ifChainAssertions(ctx);
        for (size_t i = 1; i < node.children.size(); )
        {
            AstNode & child = node.children[i];
            Analyser::TrivialValue triv = ctx.checkIfTrivial(child);
            if (triv == Analyser::ALWAYS_TRUE)
            {
                // If a predicate is always true, remove it and and all following content. Its content becomes the else clause.
                node.children.erase(node.children.begin() + i, node.children.end());
                m_killedAnything = true;
            }
            else if (triv == Analyser::ALWAYS_FALSE)
            {
                // If a predicate is always false, remove it and it's corresponding block
                node.children.erase(node.children.begin() + i - 1, node.children.begin() + i + 1);
                m_killedAnything = true;
            }
            else
            {
                ifChainAssertions.addAssertionsForCheck(child, Analyser::ASSUME_FALSE);
                i += 2;
            }
        }
        
        // If an if chain now only has an else clause (because everything else had been culled, or it had nothing else to begin with)
        // just remove it and pop everything up. This helps for later optimisation passes.
        if (node.children.size() == 1)
        {
            AstNode child = std::move(node.children[0]);
            node = std::move(child);
            m_killedAnything = true;
        }
        return RESPONSE_CONTINUE;
    }
    
    // This checks if a value is used after where it was last set, if not, it can be culled
    VisitorResponse process(Function::DeclartionOrAssignment, Analyser::AnalyserContext &, AstNode & node, size_t counter)
    {
        auto variable = m_map.find(node.fieldDescription);
        if (variable != m_map.end())
        {
            if (counter >= variable->second.lastUsed && !isSpecialVar(node.fieldDescription.get()))
            {
                m_killedAnything = true;
                return RESPONSE_KILL_NODE_AND_CONTINUE;
            }
        }
        return RESPONSE_CONTINUE;
    }
    
    // This checks to see if a ternary's predicate can be known in advance
    VisitorResponse process(Function::TernaryMacro, Analyser::AnalyserContext & ctx, AstNode & node, size_t)
    {
        Analyser::TrivialValue triv = ctx.checkIfTrivial(node.children[0]);
        if (triv == Analyser::ALWAYS_TRUE)
        {
            AstNode child = std::move(node.children[1]);
            node = child;
            m_killedAnything = true;
        }
        else if (triv == Analyser::ALWAYS_FALSE && node.function().functionType == Function::TERNARY_MACRO)
        {
            AstNode child = std::move(node.children[2]);
            node = child;
            m_killedAnything = true;
        }
        return RESPONSE_CONTINUE;
    }
    
    VisitorResponse process(Function::BoundMacro, Analyser::AnalyserContext & ctx, AstNode & node, size_t counter)
    {
        return process(Function::TernaryMacro(), ctx, node, counter);
    }
    
    // This tells if a default statement's alternative value can ever be used.
    VisitorResponse process(Function::DefaultMacro, Analyser::AnalyserContext & ctx, AstNode & node, size_t)
    {
        if (!ctx.mightBeMissing(node.children[0]))
        {
            AstNode child = std::move(node.children[0]);
            node = std::move(child);
            m_killedAnything = true;
        }
        return RESPONSE_CONTINUE;
    }
    
    // Catch all default
    VisitorResponse process(Function::FunctionTypeBase, Analyser::AnalyserContext &, AstNode &, size_t)
    {
        return RESPONSE_CONTINUE;
    }
};

// This step is between the dead-code removal step and the variable aliasing step.
// Since variable aliasing only works within a block, it is advantagious to make the blocks as large as possible.
// This visitor rolls blocks into super-blocks so that variables can be re-used more efficiently.
class FlatternNodesVisitor
{
public:
    void enterNode(Analyser::AnalyserContext &, AstNode &, size_t)
    {
    }
    VisitorResponse exitNode(Analyser::AnalyserContext &, AstNode & node, size_t)
    {
        if (node.function().functionType != Function::BLOCK)
        {
            return RESPONSE_CONTINUE;
        }
        
        bool foundBlock = false;
        for (const AstNode & child : node.children)
        {
            if (child.function().functionType == Function::BLOCK)
            {
                foundBlock = true;
            }
        }
        
        if (!foundBlock)
        {
            return RESPONSE_CONTINUE;
        }
        
        // Flattern any blocks included into a single block. This will help the optimiser.
        std::vector<AstNode> flattenedChildren;
        flattenedChildren.reserve(node.children.size() + 1);
        for (AstNode & child : node.children)
        {
            if (child.function().functionType == Function::BLOCK)
            {
                std::move(child.children.begin(), child.children.end(), std::back_inserter(flattenedChildren));
            }
            else
            {
                flattenedChildren.push_back(std::move(child));
            }
        }
        
        node.children.swap(flattenedChildren);
        return RESPONSE_CONTINUE;
    }
};


// Aliasing allows LuaOutputter to choose an existing variable that isn't being used to store another variable
// It means less variables need to be overflowed.
// It is not done by modifying to keep it in relaxed Single Static Assignment form and to allow null assertions to work properly.
class SetupAliasVisitor
{
    VariableInfoMap & m_map;
public:
    LuaOutputter::AliasedVariables m_aliasMap;
    SetupAliasVisitor(VariableInfoMap & map) :
        m_map(map)
    {}
    void enterNode(Analyser::AnalyserContext &, AstNode & node, size_t)
    {
        if (node.function().functionType != Function::BLOCK)
        {
            return;
        }
        std::vector<SortedVariableInfoReference> usageStart;
        std::vector<SortedVariableInfoReference> usageEnd;
        
        for (const AstNode & child : node.children)
        {
            if (child.function().functionType == Function::DECLARATION)
            {
                auto iter = m_map.find(child.fieldDescription);
                if (iter != m_map.end())
                {
                    usageStart.emplace_back(iter->second.firstDeclared, iter);
                    usageEnd.emplace_back(iter->second.lastUsed, iter);
                }
            }
        }
        std::sort(usageStart.begin(), usageStart.end());
        std::sort(usageEnd.begin(), usageEnd.end());
        
        std::vector<SortedVariableInfoReference>::iterator usageStartIter = usageStart.begin();
        std::vector<SortedVariableInfoReference>::iterator usageEndIter = usageEnd.begin();
        
        // Re-use variable in a LIFO way, this leads to more readable code.
        std::vector<VariableInfoMap::iterator> spaceVarStack;
        while (usageStartIter != usageStart.end() && usageEndIter != usageEnd.end())
        {
            // End variable usage in preference to start the next. An instruction may re-assign a variable at the same time as it uses it.
            if (usageEndIter->sortKey <= usageStartIter->sortKey)
            {
                spaceVarStack.push_back(usageEndIter->infoMapReference);
                ++usageEndIter;
            }
            else
            {
                if (!spaceVarStack.empty())
                {
                    VariableInfoMap::iterator aliasedFrom = usageStartIter->infoMapReference;
                    VariableInfoMap::iterator aliasedTo = spaceVarStack.back();
                    VariableInfo & myInfo = aliasedFrom->second;
                    VariableInfo & aliasedToInfo = aliasedTo->second;
                    spaceVarStack.pop_back();
                    
                    // Correct the map, now that we're using this variable to store two.
                    // Variables may be still overflowed after they are aliased.
                    aliasedToInfo.usedNTimes += myInfo.usedNTimes;
                    aliasedToInfo.lastUsed = myInfo.lastUsed;
                    
                    m_aliasMap.emplace(aliasedFrom->first, aliasedTo->first);
                    
                    // Change the end of usage for this variable to the one that it is now aliasing.
                    auto range = std::equal_range(usageEnd.begin(), usageEnd.end(), SortedVariableInfoReference(myInfo.lastUsed, aliasedFrom));
                    for (auto rangeIter = range.first; rangeIter != range.second; ++rangeIter)
                    {
                        if (rangeIter->infoMapReference == aliasedFrom)
                        {
                            rangeIter->infoMapReference = aliasedTo;
                        }
                    }
                }
                ++usageStartIter;
            }
        }
    }
    VisitorResponse exitNode(Analyser::AnalyserContext &, AstNode &, size_t)
    {
        return RESPONSE_CONTINUE;
    }
};

// This traverses the tree giving places in the overflow array to variables who have been booted out of the global space.
// It allocates them start to finish for readability (of output) and performance reasons
class OverflowAssignmentVisitor
{
    const std::unordered_set<std::shared_ptr<const PMMLDocument::FieldDescription>> & m_overflowVariables;
    int m_counter;
public:
    OverflowAssignmentVisitor(Analyser::AnalyserContext &, const std::unordered_set<std::shared_ptr<const PMMLDocument::FieldDescription>> & overflowVariables, int counter) :
        m_overflowVariables(overflowVariables),
        m_counter(counter)
    {
    }
    void enterNode(Analyser::AnalyserContext &, AstNode & node, size_t)
    {
        if (node.function().functionType == Function::DECLARATION)
        {
            if (m_overflowVariables.count(node.fieldDescription) > 0)
            {
                node.fieldDescription->overflowAssignment = m_counter++;
            }
        }
    }
    VisitorResponse exitNode(Analyser::AnalyserContext &, AstNode &, size_t)
    {
        return RESPONSE_CONTINUE;
    }
    size_t counter() const { return m_counter; }
};

// This is a last ditch effort to make sure that we can fit in the space.
// It creates an array as a local variable and stores all the locals that won't fit into the frame in there.
size_t setupOverflow(Analyser::AnalyserContext & ctx, AstNode & node, VariableInfoMap & map, const size_t maxTempVars)
{
    std::vector<SortedVariableInfoReference> referencesToNames;
    referencesToNames.reserve(map.size());
    for (auto iter = map.begin(); iter != map.end(); ++iter)
    {
        referencesToNames.emplace_back(iter->second.usedNTimes, iter);
    }
    
    std::sort(referencesToNames.begin(), referencesToNames.end());
    
    std::unordered_set<std::shared_ptr<const PMMLDocument::FieldDescription>> overflowVariables;
    size_t currentTempVars = map.size();
    // Add a temp var for the overflow array to live
    currentTempVars++;
    // Iterate from least-used to most-used
    for (auto iter = referencesToNames.cbegin(); iter != referencesToNames.cend() && currentTempVars > maxTempVars; ++iter)
    {
        overflowVariables.insert(iter->infoMapReference->first);
        currentTempVars--;
    }
    
    int counter = 1;
    // Inputs are effectively at counter=0, at the very beginning of the document
    for (auto iter = map.begin(); iter != map.end(); ++iter)
    {
        if (iter->second.firstDeclared == 0 && overflowVariables.count(iter->first) > 0)
        {
            iter->first->overflowAssignment = counter++;
        }
    }
    
    OverflowAssignmentVisitor assigner(ctx, overflowVariables, counter);
    traverseTree<false>(ctx, node, assigner);
    return assigner.counter();
}

void PMMLDocument::optimiseAST(AstNode & node, LuaOutputter & outputter)
{
    Analyser::AnalyserContext context;
top:
    // This is an important step before aliasing. Flatten nested blocks into single blocks. This means block structure will better reflect the structure
    // of the outputted code, as the aliaser will not work between blocks for fear of messing up scope.
    FlatternNodesVisitor flattenNode;
    traverseTree<false>(context, node, flattenNode);
    
    VariableInfoMap map;
    BuildVariableInfoMapVisitor seeker(map, context);
    traverseTree<false>(context, node, seeker);

    // Cut out code that is not needed.
    RemoveDeadCodeVisitor reaper(map);
    traverseTree<true>(context, node, reaper);
    if (reaper.hasKilledAnything())
    {
        // Removing dead code leads to other opportunities to remove dead code, however, counters will
        // have changed, so we need to rescan
        goto top;
    }

    // Take variables and replace them opportunistically with their constituant expressions.
    // Inline much more aggressively if we're running short of variables.
    InlineVariableVisitor gatherer(map, map.size() > outputter.getMaxVariables() ? 5 : 1);
    traverseTree<false>(context, node, gatherer);
    if (gatherer.hasKilledAnything())
    {
        // Inlining nodes may also give an opportunity to perform further optimisation
        goto top;
    }

    // This next phase, instead of semantically altering the AST, configures the LuaOutputter
    // Remove anything unmovable or not a temp.
    size_t unmovableVars = 0;
    for (auto iter = map.begin(); iter != map.end();)
    {
        if (iter->first->origin == PMMLDocument::ORIGIN_PARAMETER ||
            iter->first->origin == PMMLDocument::ORIGIN_SPECIAL)
        {
            map.erase(iter++);
        }
        else if (iter->second.unmovable)
        {
            unmovableVars++;
            map.erase(iter++);
        }
        else
        {
            ++iter;
        }
    }

    
    SetupAliasVisitor setupAlias(map);
    traverseTree<false>(context, node, setupAlias);
    // Don't bother overflowing variables that have already been aliased.
    for (const auto & alias : setupAlias.m_aliasMap)
    {
        map.erase(alias.first);
    }
    // 1 special variable, output must be left in place, as well as anything "unmovable", so they are deducted from the max allowed.
    const size_t maxTempVars = outputter.getMaxVariables() - 2 - unmovableVars;
    if (map.size() > maxTempVars)
    {
        outputter.setOverflowedVariables(setupOverflow(context, node, map, maxTempVars));
    }
    
    outputter.setAliasedVariables(std::move(setupAlias.m_aliasMap));
}
