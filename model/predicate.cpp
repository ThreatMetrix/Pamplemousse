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
//  This file is responsible for converting PMML predicates into Lua expressions
//
//  Created by Caleb Moore on 3/9/18.
//

#include "predicate.hpp"
#include "ast.hpp"
#include "conversioncontext.hpp"
#include "function.hpp"
#include <algorithm>
#include <assert.h>

namespace
{
    // These two lists must be kept in sync and ordered
    enum PredicateType
    {
        PREDICATE_COMPOUND,
        PREDICATE_FALSE,
        PREDICATE_SIMPLE,
        PREDICATE_SIMPLE_SET,
        PREDICATE_TRUE,
        PREDICATE_INVALID
    };

    const char * const PREDICATE_NAME[6]
    {
        "CompoundPredicate",
        "False",
        "SimplePredicate",
        "SimpleSetPredicate",
        "True"
    };

    PredicateType getPredicateTypeFromString(const char * name)
    {
        auto found = std::equal_range(PREDICATE_NAME, PREDICATE_NAME + static_cast<int>(PREDICATE_INVALID), name, PMMLDocument::stringIsBefore);
        if (found.first != found.second)
        {
            return static_cast<PredicateType>(found.first - PREDICATE_NAME);
        }
        else
        {
            return PREDICATE_INVALID;
        }
    }

    // Covert a SimplePredicate into Lua
    // defaultIfMissing is the boolean value that will be returned if the requisite input value is missing.
    bool parseSimple(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        const char * field = node->Attribute("field");
        const char * operatorAttr = node->Attribute("operator");
        if (field == nullptr || operatorAttr == nullptr)
        {
            fprintf(stderr, "Missing parameter in SimplePredicate at %i\n", node->GetLineNum());
            return false;
        }

        const Function::Definition * operatorOut = Function::findBuiltInFunctionDefinition(operatorAttr);
        // According to the standard, only a subset of functions are available here. Make sure that this one is allowed.
        if (operatorOut == nullptr || (operatorOut->functionType != Function::COMPARISON && operatorOut->functionType != Function::IS_MISSING))
        {
            fprintf(stderr, "Unknown comparison %s in SimplePredicate at %i\n", operatorAttr, node->GetLineNum());
            return false;
        }

        const PMMLDocument::MiningField * fieldDefinition = builder.context().getMiningField(field);
        if (fieldDefinition == nullptr)
        {
            fprintf(stderr, "Unknown field %s referenced in SimplePredicate at %i\n", field, node->GetLineNum());
            return false;
        }
        
        builder.field(fieldDefinition);

        if (operatorOut->functionType != Function::IS_MISSING)
        {
            const char * value = node->Attribute("value");
            if (value == nullptr)
            {
                fprintf(stderr, "Missing parameter in SimplePredicate at %i\n", node->GetLineNum());
                return false;
            }
            builder.constant(value, builder.topNode().coercedType);
            builder.function(*operatorOut, 2);
        }
        else
        {
            builder.function(*operatorOut, 1);
        }

        return true;
    }

    bool readArray(AstBuilder & builder, const tinyxml2::XMLElement * array, size_t & nArgs)
    {
        const char * type = array->Attribute("type");
        if (type == nullptr)
        {
            fprintf(stderr, "Missing type in Array at %i\n", array->GetLineNum());
            return false;
        }
        PMMLDocument::FieldType fieldType = PMMLDocument::dataTypeFromString(type);
        if (fieldType == PMMLDocument::TYPE_INVALID)
        {
            fprintf(stderr, "Unknown data type %s in Array at %i\n", type, array->GetLineNum());
            return false;
        }
        
        // Array is stored in space delimeted string, chop it up into potential variables while honouring quotations
        // and stick it into a Lua set: e.g. {["valuea"] = true, ["valueb"] = true}
        PMMLArrayIterator iterator(array->GetText());
        for (; iterator.isValid(); ++iterator)
        {
            builder.constant(iterator.stringStart(), iterator.stringEnd() - iterator.stringStart(), fieldType);
            nArgs++;
        }
        if (iterator.hasUnterminatedQuote())
        {
            fprintf(stderr, "Unterminated quote in array at %i\n", array->GetLineNum());
            return false;
        }
        return true;
    }
    
    bool parseSimpleSet(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        const char * field = node->Attribute("field");
        const char * booleanOperator = node->Attribute("booleanOperator");
        const tinyxml2::XMLElement * array = node->FirstChildElement("Array");
        if (field == nullptr || booleanOperator == nullptr)
        {
            fprintf(stderr, "Missing parameter in SimpleSetPredicate at %i\n", node->GetLineNum());
            return false;
        }
        
        const PMMLDocument::MiningField * fieldDefinition = builder.context().getMiningField(field);
        if (fieldDefinition == nullptr)
        {
            fprintf(stderr, "Unknown field %s referenced in SimpleSetPredicate at %i\n", field, node->GetLineNum());
            return false;
        }
        
        builder.field(fieldDefinition);

        const Function::Definition * operatorOut = Function::findBuiltInFunctionDefinition(booleanOperator);
        if (operatorOut == nullptr || operatorOut->functionType != Function::IS_IN)
        {
            fprintf(stderr, "Unknown booleanOperator: %s at %i\n", booleanOperator, node->GetLineNum());
            return false;
        }

        if (array == nullptr)
        {
            fprintf(stderr, "Missing array in SimpleSetPredicate at %i\n", node->GetLineNum());
            return false;
        }

        size_t nArgs = 1;
        if (!readArray(builder, array, nArgs))
        {
            return false;
        }
        builder.function(*operatorOut, nArgs);
        
        return true;
    }
    
    bool parseCompoundPredicate(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        const char * booleanOperator = node->Attribute("booleanOperator");
        if (booleanOperator == nullptr)
        {
            fprintf(stderr, "CompoundPredicate without booleanOperator at %i\n", node->GetLineNum());
            return false;
        }
        const Function::Definition * operatorOut = Function::findBuiltInFunctionDefinition(booleanOperator);
        if (operatorOut == nullptr || (operatorOut->functionType != Function::BOOLEAN_AND &&
                                       operatorOut->functionType != Function::BOOLEAN_OR ))
        {
            if (strcmp(booleanOperator, "xor") == 0)
            {
                operatorOut = &Function::xorFunction;
            }
            else if (strcmp(booleanOperator, "surrogate") == 0)
            {
                operatorOut = &Function::surrogateFunction;
            }
            else
            {
                fprintf(stderr, "Unknown booleanOperator: %s at %i\n", booleanOperator, node->GetLineNum());
                return false;
            }
        }
        
        size_t count = 0;
        for (const tinyxml2::XMLElement * subNode = PMMLDocument::skipExtensions(node->FirstChildElement()); subNode;
             subNode = PMMLDocument::skipExtensions(subNode->NextSiblingElement()))
        {
            if (!Predicate::parse(builder, subNode))
            {
                return false;
            }
            count++;
        }
        if (count == 0)
        {
            fprintf(stderr, "Empty CompoundPredicate at %i\n", node->GetLineNum());
            return false;
        }
        
        builder.function(*operatorOut, count);
        
        return true;
    }
}

// This will output a lua expression that is equivalent to the predicate expression. If value is missing that is required to calculate a simple predicate, the value in defaultIfMissing is used.
bool Predicate::parse(AstBuilder & builder, const tinyxml2::XMLElement * node)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
    const char * name = node->Name();
    switch(getPredicateTypeFromString(name))
    {
        case PREDICATE_SIMPLE:
            return parseSimple(builder, node);
        case PREDICATE_SIMPLE_SET:
            return parseSimpleSet(builder, node);
        case PREDICATE_COMPOUND:
            return parseCompoundPredicate(builder, node);
        case PREDICATE_TRUE:
            builder.constant("true", PMMLDocument::TYPE_BOOL);
            return true;
        case PREDICATE_FALSE:
            builder.constant("false", PMMLDocument::TYPE_BOOL);
            return true;
        case PREDICATE_INVALID:
            fprintf(stderr, "Unknown predicate %s at %i\n", name, node->GetLineNum());
    }

    return false;
}

void PMMLArrayIterator::getNext()
{
    m_upto = std::find_if_not(m_upto, m_endPtr, isspace);
    if (m_upto == m_endPtr)
    {
        m_stringStart = m_upto;
        return;
    }
    m_stringStart = m_upto;
    if (*m_stringStart == '\"')
    {
        m_stringStart++;
        do
        {
            m_upto = std::find(m_upto + 1, m_endPtr, '\"');
        }
        while (m_upto != m_endPtr && *(m_upto - 1) == '\\');
        
        // If upto reached the endptr without finding a quote, that's bad!
        m_hasUnterminatedQuote = m_upto == m_endPtr;
    }
    else
    {
        m_upto = std::find_if(m_upto + 1, m_endPtr, isspace);
    }
}
PMMLArrayIterator::PMMLArrayIterator(const char * content) : m_endPtr(content + strlen(content)), m_upto(content), m_hasUnterminatedQuote(false)
{
    getNext();
}
PMMLArrayIterator & PMMLArrayIterator::operator++()
{
    if (*m_upto == '\"')
    {
        m_upto++;
    }
    getNext();
    return *this;
}
PMMLArrayIterator PMMLArrayIterator::operator++(int)
{
    PMMLArrayIterator oldVal = *this;
    ++m_upto;
    return oldVal;
}

