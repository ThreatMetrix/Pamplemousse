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

// Coverage for the small string -> enum converters in
// common/pmmldocumentdefs.{cpp,hpp}. The optypeFromString and
// outlierTreatmentFromString converters use std::equal_range over hand-sorted
// const arrays, which silently returns an empty range if the array is not
// actually sorted - so a maintenance mistake (inserting a new entry out of
// alphabetical order) would make a known-valid input parse to INVALID with no
// error or warning. The tests here lock down both the value mapping and the
// sortedness invariant.

#include "Cuti.h"

#include "pmmldocumentdefs.hpp"

#include <cstring>

TEST_CLASS (TestPMMLDocumentDefs)
{
public:
    void testDataTypeFromString()
    {
        using namespace PMMLDocument;

        // Numeric variants - all of these are PMML 4.4 dataType values that
        // should collapse to TYPE_NUMBER. Listed in the order they appear in
        // the strcmp chain to make missed updates easy to spot.
        const char * const numericNames[] = {
            "double", "float",
            "long", "int", "integer", "short",
            "byte", "unsignedLong", "unsignedInt",
            "unsignedShort", "unsignedByte"
        };
        for (const char * name : numericNames)
        {
            CPPUNIT_ASSERT_EQUAL_MESSAGE(name, TYPE_NUMBER, dataTypeFromString(name));
        }

        CPPUNIT_ASSERT_EQUAL(TYPE_BOOL,   dataTypeFromString("boolean"));
        CPPUNIT_ASSERT_EQUAL(TYPE_STRING, dataTypeFromString("string"));

        // Unknown / empty / case-sensitive-mismatch all map to TYPE_INVALID.
        CPPUNIT_ASSERT_EQUAL(TYPE_INVALID, dataTypeFromString(""));
        CPPUNIT_ASSERT_EQUAL(TYPE_INVALID, dataTypeFromString("Double"));   // wrong case
        CPPUNIT_ASSERT_EQUAL(TYPE_INVALID, dataTypeFromString("nonsense"));
    }

    void testOpTypeFromString()
    {
        using namespace PMMLDocument;

        // Every PMML optype value must round-trip to the matching enum.
        // optypeFromString uses equal_range over a sorted name array, so this
        // test also implicitly verifies the array is still sorted: if the
        // array is mis-ordered, equal_range returns an empty range and the
        // assertion fails.
        CPPUNIT_ASSERT_EQUAL(OPTYPE_CATEGORICAL, optypeFromString("categorical"));
        CPPUNIT_ASSERT_EQUAL(OPTYPE_CONTINUOUS,  optypeFromString("continuous"));
        CPPUNIT_ASSERT_EQUAL(OPTYPE_ORDINAL,     optypeFromString("ordinal"));

        CPPUNIT_ASSERT_EQUAL(OPTYPE_INVALID, optypeFromString(""));
        CPPUNIT_ASSERT_EQUAL(OPTYPE_INVALID, optypeFromString("Categorical"));   // wrong case
        CPPUNIT_ASSERT_EQUAL(OPTYPE_INVALID, optypeFromString("nominal"));
    }

    void testOutlierTreatmentFromString()
    {
        using namespace PMMLDocument;

        // Same shape as OpType: equal_range over a sorted name array. Tests
        // both the value mapping and (implicitly) the sortedness invariant.
        CPPUNIT_ASSERT_EQUAL(OUTLIER_TREATMENT_AS_EXTREME_VALUES, outlierTreatmentFromString("asExtremeValues"));
        CPPUNIT_ASSERT_EQUAL(OUTLIER_TREATMENT_AS_IS,             outlierTreatmentFromString("asIs"));
        CPPUNIT_ASSERT_EQUAL(OUTLIER_TREATMENT_AS_MISSING_VALUES, outlierTreatmentFromString("asMissingValues"));

        CPPUNIT_ASSERT_EQUAL(OUTLIER_TREATMENT_INVALID, outlierTreatmentFromString(""));
        CPPUNIT_ASSERT_EQUAL(OUTLIER_TREATMENT_INVALID, outlierTreatmentFromString("AsIs"));   // wrong case
        CPPUNIT_ASSERT_EQUAL(OUTLIER_TREATMENT_INVALID, outlierTreatmentFromString("clip"));
    }

    CPPUNIT_TEST_SUITE(TestPMMLDocumentDefs);
    CPPUNIT_TEST(testDataTypeFromString);
    CPPUNIT_TEST(testOpTypeFromString);
    CPPUNIT_TEST(testOutlierTreatmentFromString);
    CPPUNIT_TEST_SUITE_END();
};
