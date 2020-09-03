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
//  Created by Caleb Moore on 31/8/18.
//

#ifndef luaoutputter_hpp
#define luaoutputter_hpp

#include <string>
#include <vector>
#include <ostream>

#include "document.hpp"

namespace PMMLDocument
{
    struct FieldDescription;
}

class LuaOutputter
{
public:
    static const char * OUTPUT_NAME;
    static const char * INPUT_NAME;
    static const char * OVERFLOW_NAME;
    
    typedef std::unordered_map<PMMLDocument::ConstFieldDescriptionPtr, int> OverflowedVariables;
    typedef std::unordered_map<PMMLDocument::ConstFieldDescriptionPtr, PMMLDocument::ConstFieldDescriptionPtr> AliasedVariables;
private:
    
    std::ostream & m_output;

    int m_indentLevel;
    int m_operatorPrecedence;
    enum SpaceState
    {
        AFTER_LINE_END,
        AFTER_KEYWORD,
        AFTER_SPECIAL
    };
    SpaceState m_spaceState;
    enum SyntaxState
    {
        GLOBAL,
        FUNCTION_BLOCK,
        IF_BLOCK,
        WHILE_BLOCK,
        ELSE_BLOCK,
        IF_PREDICATE,
        WHILE_PREDICATE,
        FUNCTION_ARGUMENTS,
        INSIDE_PARENTHESIS,
        INSIDE_BRACKETS
    };

    static inline bool isBlock(SyntaxState state)
    {
        return state == GLOBAL || state == FUNCTION_BLOCK || state == IF_BLOCK || state == WHILE_BLOCK || state == ELSE_BLOCK;
    }

    static inline bool isPredicate(SyntaxState state)
    {
        return state == IF_PREDICATE || state == WHILE_PREDICATE;
    }

    std::vector< SyntaxState > m_stack;
    void doIndent();
    SyntaxState getContext() const
    {
        return m_stack.empty() ? GLOBAL : m_stack.back();
    }
    
    AliasedVariables m_aliasedVariables;
    size_t m_overflowedVariables = 0;
    size_t m_maxVariables;
    const unsigned int m_options;
public:
    enum
    {
        OPTION_LOWERCASE = 1
    };
    
    bool lowercase() const { return m_options & OPTION_LOWERCASE; }
    
    enum
    {
        PRECEDENCE_TOP   = 0, //
        PRECEDENCE_POWER = 1, // ^
        PRECEDENCE_UNARY = 2, // not  - (unary)
        PRECEDENCE_TIMES = 3, // *   /
        PRECEDENCE_PLUS  = 4, // +   -
        PRECEDENCE_CONCAT = 5, // ..
        PRECEDENCE_EQUAL = 6, // <   >   <=  >=  ~=  ==
        PRECEDENCE_AND   = 7, // and
        PRECEDENCE_OR    = 8, // or
        PRECEDENCE_PARENTHESIS = 9 // ( ) ,
    };
    
    // This little guard will automatically add parenthesis when needed. This prevents distracting and redundant parenthesis from being added
    // while still maintaining logical correctness.
    class OperatorScopeHelper
    {
        LuaOutputter & m_outputter;
        const int m_oldPrecedence;
        const bool m_enabled;
        static bool NeedsParenthesis(int oldPrecedence, int newPrecedence)
        {
            if (oldPrecedence < newPrecedence)
            {
                return true;
            }
            // These two operators are unique in precedence and commutable, so grouping within this level doesn't matter.
            if (oldPrecedence == newPrecedence && oldPrecedence != PRECEDENCE_AND && oldPrecedence != PRECEDENCE_OR)
            {
                return true;
            }
            return false;
        }
    public:
        OperatorScopeHelper(LuaOutputter & outputter, int newPrecedence, bool enabled = true) : m_outputter(outputter), m_oldPrecedence(outputter.m_operatorPrecedence),
            m_enabled(enabled)
        {
            if (enabled)
            {
                if (NeedsParenthesis(m_oldPrecedence, newPrecedence))
                {
                    outputter.openParen();
                }
                outputter.m_operatorPrecedence = newPrecedence;
            }
        }
        ~OperatorScopeHelper()
        {
            if (m_enabled && NeedsParenthesis(m_oldPrecedence, m_outputter.m_operatorPrecedence))
            {
                m_outputter.closeParen();
            }
            m_outputter.m_operatorPrecedence = m_oldPrecedence;
        }
    };
    
    LuaOutputter(std::ostream & m_output, unsigned int options = 0);
    LuaOutputter & startIf();
    LuaOutputter & startElseIf();
    LuaOutputter & startElse();
    LuaOutputter & startWhile();
    LuaOutputter & function();
    LuaOutputter & function(const char * functionName);
    LuaOutputter & finishedArguments();
    LuaOutputter & doBlock();
    LuaOutputter & endPredicate();
    LuaOutputter & endBlock(bool endLine = true);

    // Keyword is only for outputting actual keywords.
    // There is no std::string varient defined, since field names usually use std::string, it's a good protection against accidently passing a
    // field name and having almost-correct-but-still-wrong behaviour.
    LuaOutputter & keyword(const char * keyword);
    LuaOutputter & endline();
    LuaOutputter & comma();

    // These are for outputting literals found as string
    LuaOutputter & literal(const char * literalString, PMMLDocument::FieldType type);
    LuaOutputter & literal(const std::string & literalString, PMMLDocument::FieldType type)
    {
        return literal(literalString.c_str(), type);
    }
    LuaOutputter & literal(const char * literalString, size_t range, PMMLDocument::FieldType type);

    LuaOutputter & literal(int literal);
    LuaOutputter & literal(float literal);
    LuaOutputter & literal(double literal);
    LuaOutputter & literal(const char * literalString)
    {
        return literal(literalString, PMMLDocument::TYPE_STRING);
    }
    LuaOutputter & literal(const std::string & literalString)
    {
        return literal(literalString.c_str(), PMMLDocument::TYPE_STRING);
    }

    LuaOutputter & nullReplacement(const char * literal, PMMLDocument::FieldType type);

    LuaOutputter & field(PMMLDocument::ConstFieldDescriptionPtr fieldDescription);
    LuaOutputter & assign(PMMLDocument::ConstFieldDescriptionPtr fieldDescription);
    
    LuaOutputter & declare(PMMLDocument::ConstFieldDescriptionPtr fieldDescription, bool hasValue);
    
    LuaOutputter & nullCheck(PMMLDocument::ConstFieldDescriptionPtr fieldDescription);
    LuaOutputter & rawField(PMMLDocument::ConstFieldDescriptionPtr fieldDescription);

    LuaOutputter & openParen();
    LuaOutputter & closeParen();
    LuaOutputter & openBracket();
    LuaOutputter & closeBracket();
    
    size_t getMaxVariables() const { return m_maxVariables; }
    void setOverflowedVariables(size_t hasOverflow)
    {
        m_overflowedVariables = hasOverflow;
    }
    void setAliasedVariables(AliasedVariables && aliasedVariables)
    {
        m_aliasedVariables = aliasedVariables;
    }
    size_t nOverflowedVariables() const
    {
        return m_overflowedVariables;
    }
};

#endif /* luaoutputter_hpp */
