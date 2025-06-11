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
//  Created by Caleb Moore on 3/9/18.
//

#ifndef predicate_hpp
#define predicate_hpp

#include "document.hpp"
#include "tinyxml2.h"
#include "ast.hpp"
#include "function.hpp"


class PMMLArrayIterator
{
    const char * const m_endPtr;
    const char * m_stringStart;
    const char * m_upto;
    bool m_hasUnterminatedQuote;
    void getNext();
public:
    PMMLArrayIterator(const char * content);
    PMMLArrayIterator & operator++();
    PMMLArrayIterator operator++(int);
    bool isValid() const { return m_upto != m_stringStart; }
    bool hasMore() const { return m_upto < m_endPtr; }
    bool hasUnterminatedQuote() const { return m_hasUnterminatedQuote; }
    const char * stringStart() const { return m_stringStart; }
    const char * stringEnd() const { return m_upto; }
};

namespace Predicate
{
    // This converts an expression specified by null to code. It adds non-null assertion to the scope specified by "guard".
    // These assertions apply if the outputted code evaluates to true.
    bool parse(AstBuilder & builder, const tinyxml2::XMLElement * node);
}

#endif /* predicate_hpp */
