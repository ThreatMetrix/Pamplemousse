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
//  Created by Caleb Moore on 15/6/20.
//
//  This file provides methods to analyse syntax in AST form in order to optimise it

#ifndef analyser_hpp
#define analyser_hpp

#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "ast.hpp"

namespace Analyser
{
    enum Assumption
    {
        NO_ASSUMPTIONS,
        ASSUME_NOT_MISSING,
        ASSUME_MISSING,
        ASSUME_TRUE,
        ASSUME_FALSE,
        ASSUME_NOT_TRUE,
        ASSUME_NOT_FALSE
    };
    
    // This represents a particular point of execution and what is known (or can be inferred) to be true, false or not missing at that point.
    // It is created empty (representing the totally unknown state at the beginning of the generated program) and is mutated by
    // NonNoneAssertionStackGuard and ChildAssertionIterator to represent moving through the tree.
    class AnalyserContext
    {
    public:
        // This checks if an assertion has already been made up the stack that the variable isn't missing.
        // It allows us to skip a bunch of unneeded conditions.
        bool mightVariableBeMissing(const PMMLDocument::FieldDescription & field) const
        {
            return m_assertNotMissing.find(field.id) == m_assertNotMissing.end();
        }
        
        // This checks if an assertion has already been made up the stack that this expression isn't missing.
        // It allows us to skip a bunch of unneeded conditions.
        bool mightClauseBeMissing(unsigned int clauseID) const
        {
            return m_assertClauseNotMissing.find(clauseID) == m_assertClauseNotMissing.end();
        }
        
        // Returns true if the node in this context may possibly evaluate to an unknown value.
        bool mightBeMissing(const AstNode & node);
        // This function checks to see if the result of a predicate can be determined statically on this context.
        TrivialValue checkIfTrivial(const AstNode & node);
    private:
        // This is a set of variables that cannot be null in this context
        std::unordered_map<unsigned int, size_t> m_assertNotMissing;
        // This is a set of ASTNode ids that cannot be null in this context
        std::unordered_map<unsigned int, size_t> m_assertClauseNotMissing;
        
        friend class NonNoneAssertionStackGuard;
    };
    
    class ChildAssertionIterator;
    
    // Missing value handling is a very important part of the PMML specification. Missing values are generally handled as "nil" in outputted code,
    // however, this does not work correctly in terms of most operations.
    // Expressions are generally expressed as "(nullity clause) and (value clause)", the idea being that a null value will short circuit the second half of the expression
    // and propagate the unknown value back. This of course may be different for different expressions.
    
    // This stack guard is used to cull back redundent checks against nil. This stores a list of input fields that may not be none in a position of logical flow.
    // The idea being that if we know an expression is only true if value is known, the value may not be unknown in dependant expressions.
    class NonNoneAssertionStackGuard
    {
        AnalyserContext & m_context;
        std::vector<std::unordered_map<unsigned int, size_t>::iterator> m_frameContentVariables;
        std::vector<std::unordered_map<unsigned int, size_t>::iterator> m_frameContentClauses;
        NonNoneAssertionStackGuard(const NonNoneAssertionStackGuard &) = delete;
        
    public:
        // Used to find an intersection of assertions, finding the minimum set of assertions of any of multiple branches.
        class AssertionIntersection
        {
            typedef std::unordered_set<unsigned int> VarSet;
            typedef std::unordered_set<unsigned int> ClauseSet;
            VarSet variables;
            ClauseSet clauses;
            void addToIntersection(VarSet & vs, ClauseSet & cs, const NonNoneAssertionStackGuard & src) const;
            void addToIntersection(VarSet & vs, ClauseSet & cs, const ChildAssertionIterator & src) const;
            void addIfIntersects(VarSet &, ClauseSet &) const {}
            
            template<typename T, typename... Args>
            void addIfIntersects(VarSet & vs, ClauseSet & cs, const T & first, const Args & ... args) const
            {
                addToIntersection(vs, cs, first);
                addIfIntersects(vs, cs, args...);
            }
            
        public:
            void add(const NonNoneAssertionStackGuard & src);
            void add(const ChildAssertionIterator & src);
            
            // This uses a variadic template so you can intersect against a union more efficiently than combining things first.
            template <typename... Args>
            void intersect(const Args & ... args)
            {
                VarSet new_variables;
                ClauseSet new_clauses;
                new_variables.reserve(variables.size());
                new_clauses.reserve(clauses.size());
                addIfIntersects(new_variables, new_clauses, args...);
                
                variables.swap(new_variables);
                clauses.swap(new_clauses);
            }
            void apply(NonNoneAssertionStackGuard & guard) const;
        };
        NonNoneAssertionStackGuard(AnalyserContext & context) :
        m_context(context)
        {}
        
        AnalyserContext & context() { return m_context; }
        // Mark a variable as not being missing
        void addVariableAssertionByID(unsigned int fieldID)
        {
            auto inserted = m_context.m_assertNotMissing.emplace(fieldID, 0);
            // Increase ref count
            ++inserted.first->second;
            m_frameContentVariables.push_back(inserted.first);
        }
        
        void addVariableAssertion(const PMMLDocument::FieldDescription & field)
        {
            addVariableAssertionByID(field.id);
        }
        // Mark an AST noode as not being missing
        void addClauseAssertion(unsigned int clauseID)
        {
            auto inserted = m_context.m_assertClauseNotMissing.emplace(clauseID, 0);
            // Increase ref count
            ++inserted.first->second;
            m_frameContentClauses.push_back(inserted.first);
        }
        
        void clear()
        {
            for (const auto & i : m_frameContentVariables)
            {
                if (--(i->second) == 0)
                {
                    m_context.m_assertNotMissing.erase(i);
                }
            }
            for (const auto & i : m_frameContentClauses)
            {
                if (--(i->second) == 0)
                {
                    m_context.m_assertClauseNotMissing.erase(i);
                }
            }
            m_frameContentVariables.clear();
            m_frameContentClauses.clear();
        }
        
        ~NonNoneAssertionStackGuard()
        {
            clear();
        }
        
        // Add a set of assertions to the context that apply when this node evaluates to a particular value
        void addAssertionsForCheck(const AstNode & node, Assumption assumption);
    };


    // This is a helper to allow you to iterate through children of a node and have assertions added to your context as they
    // apply to the node that its currently pointing to.
    // For instance, if it's outputting the iftrue side of a ternary expression (pred ? iftrue : iffalse),
    // then it will assume that the pred evaluated to true and everything that requires to be so, for that predicate
    // to be true will be assumed to be so.
    // This applies with if chains (this predicate will evaluate to true, others will evaluate to false)
    // As well as AND and OR (previous arguments evaluated to true / false respectively)
    // But remember, it HOLDs assertions, so make sure you destroy it or reset it when you're no longer using it.
    class ChildAssertionIterator : public std::iterator<std::forward_iterator_tag, // iterator_category
                                                        const AstNode,             // value_type
                                                        size_t,                    // difference_type
                                                        const AstNode *,           // pointer
                                                        const AstNode &            // reference
                                                        >
    {
        const AstNode & node;
        const bool maintainAssertions;
        size_t i;
        NonNoneAssertionStackGuard blockAssertions;
        NonNoneAssertionStackGuard runningAssertions;
        void fixAssertions();
    public:
        typedef size_t difference_type;
        ChildAssertionIterator(const ChildAssertionIterator & other) = delete;
        
        ChildAssertionIterator(AnalyserContext & c, const AstNode & n, bool mi = true) :
            node(n),
            maintainAssertions(mi),
            i(0),
            blockAssertions(c),
            runningAssertions(c)
        {
            fixAssertions();
        }
        void reset()
        {
            blockAssertions.clear();
            runningAssertions.clear();
            i = 0;
            fixAssertions();
        }
        bool valid() const;
        size_t index() const { return i; }
        ChildAssertionIterator & operator++()
        {
            i++;
            fixAssertions();
            return *this;
        }
        const AstNode & operator*();
        const AstNode * operator->();
        friend class NonNoneAssertionStackGuard::AssertionIntersection;
    };

}

#endif /* analyser_hpp */
