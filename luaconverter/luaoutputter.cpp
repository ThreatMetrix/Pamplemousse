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

#include "luaoutputter.hpp"
#include "conversioncontext.hpp"
#include <assert.h>
#include <algorithm>
#include <ctype.h>

const char * LuaOutputter::OUTPUT_NAME = "output";
const char * LuaOutputter::INPUT_NAME = "input";
const char * LuaOutputter::OVERFLOW_NAME = "overflow";
const char * LuaOutputter::LUA_INFINITY = "math.huge";


LuaOutputter::LuaOutputter(std::ostream & output, unsigned int options) :
    m_output(output),
    m_indentLevel(0),
    m_operatorPrecedence(PRECEDENCE_PARENTHESIS),
    m_spaceState(AFTER_LINE_END),
    m_maxVariables(195),
    m_options(options)
{
    m_output.precision(17); // 17 digits is required to ensure that value is rounded down to its original value.
}

LuaOutputter & LuaOutputter::startIf()
{
    assert(isBlock(getContext()));
    keyword("if");
    m_indentLevel++;
    m_stack.push_back(IF_PREDICATE);
    return *this;
}

LuaOutputter & LuaOutputter::startElseIf()
{
    assert(getContext() == IF_BLOCK);
    m_indentLevel--;
    keyword("elseif");
    m_indentLevel++;
    // Instead of popping the IF_BLOCK and pushing the IF_PREDICATE, just overwrite it.
    m_stack.back() = IF_PREDICATE;
    return *this;
}

LuaOutputter & LuaOutputter::startElse()
{
    assert(getContext() == IF_BLOCK);
    m_indentLevel--;
    keyword("else").endline();
    m_indentLevel++;
    // Instead of popping the IF_BLOCK and pushing the ELSE_BLOCK, just overwrite it.
    m_stack.back() = ELSE_BLOCK;
    return *this;
}

LuaOutputter & LuaOutputter::startWhile()
{
    assert(isBlock(getContext()));
    keyword("while");
    m_indentLevel++;
    m_stack.push_back(WHILE_PREDICATE);
    return *this;
}

LuaOutputter & LuaOutputter::function()
{
    keyword("function(");
    m_indentLevel++;
    m_stack.push_back(FUNCTION_BLOCK);
    m_stack.push_back(FUNCTION_ARGUMENTS);
    return *this;
}

LuaOutputter & LuaOutputter::function(const char * functionName)
{
    keyword("function").keyword(functionName).keyword("(");
    m_indentLevel++;
    m_stack.push_back(FUNCTION_BLOCK);
    m_stack.push_back(FUNCTION_ARGUMENTS);
    return *this;
}

LuaOutputter & LuaOutputter::finishedArguments()
{
    assert(getContext() == FUNCTION_ARGUMENTS);
    keyword(")").endline();
    m_stack.pop_back();
    return *this;
}

LuaOutputter & LuaOutputter::doBlock()
{
    keyword("do").endline();
    m_indentLevel++;
    m_stack.push_back(FUNCTION_BLOCK);
    return *this;
}


LuaOutputter & LuaOutputter::endPredicate()
{
    SyntaxState state = getContext();
    switch(state)
    {
        case IF_PREDICATE:
            keyword("then").endline();
            m_stack.back() = IF_BLOCK;
            break;
        case WHILE_PREDICATE:
            keyword("do").endline();
            m_stack.back() = WHILE_BLOCK;
            break;
        default:
            assert(false);
            return *this;
    }

    return *this;
}

LuaOutputter & LuaOutputter::endBlock(bool shouldEndLine)
{
    assert(isBlock(getContext()));
    m_stack.pop_back();
    m_indentLevel--;
    keyword("end");
    if (shouldEndLine)
    {
        endline();
    }
    return *this;
}

void LuaOutputter::doIndent()
{
    if (m_spaceState == AFTER_LINE_END)
    {
        for (int i = 0; i < m_indentLevel; i++)
        {
            m_output << "  ";
        }
    }
    else if (m_spaceState == AFTER_KEYWORD)
    {
        m_output << " ";
    }
}

LuaOutputter & LuaOutputter::keyword(const char * keyword)
{
    doIndent();
    m_output << keyword;
    m_spaceState = AFTER_KEYWORD;
    return *this;
}

LuaOutputter & LuaOutputter::endline()
{
    if (m_spaceState != AFTER_LINE_END)
    {
        m_output << std::endl;
        m_spaceState = AFTER_LINE_END;
    }
    return *this;
}

LuaOutputter & LuaOutputter::comma()
{
    m_output << ",";
    m_spaceState = AFTER_KEYWORD;
    return *this;
}

LuaOutputter & LuaOutputter::literal(const char * literal, PMMLDocument::FieldType type)
{
    doIndent();
    if (type == PMMLDocument::TYPE_STRING)
    {
        std::string literalString;
        literalString.reserve(strlen(literal));

        for (const char * thisChar = literal; *thisChar != '\0'; thisChar++)
        {
            if (*thisChar == '\n')
            {
                literalString += "\\n";
            }
            else if (*thisChar == '\t')
            {
                literalString += "\\t";
            }
            else if (*thisChar == '\r')
            {
                literalString += "\\r";
            }
            else if (*thisChar == '\\')
            {
                literalString += "\\\\";
            }
            else if (*thisChar == '\"')
            {
                literalString += "\\\"";
            }
            else if (!isprint(*thisChar))
            {
                static const char hexchars[17] = "0123456789abcdef";
                literalString.push_back('\\');
                literalString.push_back('x');
                literalString.push_back(hexchars[(*thisChar >> 4)]);
                literalString.push_back(hexchars[(*thisChar & 0xf)]);
            }
            else
            {
                literalString.push_back(*thisChar);
            }
        }

        if (m_options & OPTION_LOWERCASE)
        {
            std::transform(literalString.begin(), literalString.end(), literalString.begin(), ::tolower);
        }
        m_output << '"' << literalString << '"';
    }
    else if (type == PMMLDocument::TYPE_BOOL)
    {
        std::string literalString(literal);
        std::transform(literalString.begin(), literalString.end(), literalString.begin(), ::tolower);
        m_output << literalString;
    }
    else
    {
        m_output << literal;
    }
    m_spaceState = AFTER_KEYWORD;
    return *this;
}

LuaOutputter & LuaOutputter::literal(const char * literal, size_t range, PMMLDocument::FieldType type)
{
    doIndent();
    if (type == PMMLDocument::TYPE_STRING)
    {
        // TODO: probably should escape literal somehow.
        if (m_options & OPTION_LOWERCASE)
        {
            std::string toWrite;
            toWrite.resize(range);
            std::transform(literal, literal + range, toWrite.begin(), ::tolower);
            m_output << '"' << toWrite << '"';
        }
        else
        {
            m_output << '"';
            m_output.write(literal, range);
            m_output << '"';
        }
    }
    else if (type == PMMLDocument::TYPE_BOOL)
    {
        std::string literalString(literal, range);
        std::transform(literalString.begin(), literalString.end(), literalString.begin(), ::tolower);
        m_output << literalString;
    }
    else
    {
        m_output.write(literal, range);
    }

    m_spaceState = AFTER_KEYWORD;
    return *this;
}

LuaOutputter & LuaOutputter::literal(int literal)
{
    doIndent();
    m_output << literal;
    m_spaceState = AFTER_KEYWORD;
    return *this;
}

LuaOutputter & LuaOutputter::literal(float literal)
{
    doIndent();
    m_output << literal;
    m_spaceState = AFTER_KEYWORD;
    return *this;
}

LuaOutputter & LuaOutputter::literal(double literal)
{
    doIndent();
    m_output << literal;
    m_spaceState = AFTER_KEYWORD;
    return *this;
}

LuaOutputter & LuaOutputter::field(PMMLDocument::ConstFieldDescriptionPtr fieldDescription)
{
    rawField(fieldDescription);
    return *this;
}

// This returns nil if value is nil, non nil (and not false) if value is not nil.
LuaOutputter & LuaOutputter::nullCheck(PMMLDocument::ConstFieldDescriptionPtr fieldDescription)
{
    if (fieldDescription && fieldDescription->field.dataType == PMMLDocument::TYPE_BOOL)
    {
        // False will trigger the short circuit condition too... so explicitly compare bools with nil
        // use the "or nil" to return a nil if equal to nil, rather than a false.
        OperatorScopeHelper orScope(*this, PRECEDENCE_OR);
        return rawField(fieldDescription).keyword("~= nil").keyword("or nil");
    }
    else
    {
        return rawField(fieldDescription);
    }
}

LuaOutputter & LuaOutputter::declare(PMMLDocument::ConstFieldDescriptionPtr fieldDescription,
                                     bool hasValue)
{
    auto alias = m_aliasedVariables.find(fieldDescription);
    bool aliased = (alias != m_aliasedVariables.end()) && (alias->second != fieldDescription);
    if (!aliased && fieldDescription->overflowAssignment == 0)
    {
        keyword("local");
    }
    
    rawField(fieldDescription);
    
    keyword("=");
    if (!hasValue)
    {
        if (fieldDescription->field.dataType == PMMLDocument::TYPE_TABLE ||
            fieldDescription->field.dataType == PMMLDocument::TYPE_STRING_TABLE)
        {
            keyword("{}");
        }
        else
        {
            keyword("nil");
        }
    }
    return *this;
}

LuaOutputter & LuaOutputter::assign(PMMLDocument::ConstFieldDescriptionPtr fieldDescription)
{
    return LuaOutputter::rawField(fieldDescription).keyword("=");
}

LuaOutputter & LuaOutputter::nullReplacement(const char * literal, PMMLDocument::FieldType type)
{
    if (type == PMMLDocument::TYPE_BOOL)
    {
        if (PMMLDocument::strcasecmp(literal, "true") == 0)
        {
            return keyword("~= false");
        }
        else
        {
            return keyword("== true");
        }
    }
    else
    {
        return keyword("or").literal(literal, type);
    }
}

LuaOutputter & LuaOutputter::rawField(PMMLDocument::ConstFieldDescriptionPtr fieldDescription)
{
    auto aliased = m_aliasedVariables.find(fieldDescription);
    if (aliased != m_aliasedVariables.end())
    {
        fieldDescription = aliased->second;
    }
    
    if (fieldDescription->overflowAssignment)
    {
        keyword(OVERFLOW_NAME).openBracket().literal(int(fieldDescription->overflowAssignment)).closeBracket();
        return *this;
    }

    doIndent();
    m_output << fieldDescription->luaName;
    m_spaceState = AFTER_KEYWORD;
    return *this;
}

LuaOutputter & LuaOutputter::openParen()
{
    keyword("(");
    m_spaceState = AFTER_SPECIAL;
    m_stack.push_back(INSIDE_PARENTHESIS);
    return *this;
}

LuaOutputter & LuaOutputter::closeParen()
{
    assert(getContext() == INSIDE_PARENTHESIS);
    m_stack.pop_back();
    m_output << ")";
    m_spaceState = AFTER_KEYWORD;
    return *this;
}

LuaOutputter & LuaOutputter::openBracket()
{
    keyword("[");
    m_spaceState = AFTER_SPECIAL;
    m_stack.push_back(INSIDE_BRACKETS);
    return *this;
}

LuaOutputter & LuaOutputter::closeBracket()
{
    assert(getContext() == INSIDE_BRACKETS);
    m_stack.pop_back();
    m_output << "]";
    m_spaceState = AFTER_KEYWORD;
    return *this;
}

