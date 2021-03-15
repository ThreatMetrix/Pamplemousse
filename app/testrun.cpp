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
//  Created by Caleb Moore on 7/10/18.
//

#include "testrun.hpp"
#include "modeloutput.hpp"
#include "basicexport.hpp"
#include "luaconverter/luaoutputter.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <math.h>
#include <algorithm>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}


namespace
{
    std::string lowercaseString(const char * start, const char * end)
    {
        std::string toPush;
        toPush.resize(end - start);
        std::transform(start, end, toPush.begin(), ::tolower);
        return toPush;
    }

    // Push a string to the lua stack with start and end pointer
    void pushString(lua_State *L, const char * start, const char * end, bool insensitive)
    {
        if (end == nullptr)
        {
            end = start + strlen(start);
        }
        
        // Strings need to be converted to lower case to match OTIN's behavior
        if (insensitive)
        {
            std::string toPush = lowercaseString(start, end);
            lua_pushlstring(L, toPush.data(), toPush.length());
        }
        else
        {
            lua_pushlstring(L, start, end - start);
        }
    }
    
    // Push a number to the lua stack (checking its validity)
    void pushNumber(lua_State *L, const char * start, const char * end)
    {
        char * endOfNumber;
        double doubleVal = std::strtod(start, &endOfNumber);
        if ((end != nullptr && endOfNumber == end) || (end == nullptr && *endOfNumber == '\0'))
        {
            lua_pushnumber(L, doubleVal);
        }
        else
        {
            lua_pushnil(L);
            size_t len = end ? (end - start) : strlen(start);
            fprintf(stderr, "Found something that does not look like a number: %s)\n", std::string(start, len).c_str());
        }
    }
    
    // Push a bool to the lua stack
    void pushBool(lua_State *L, const char * start, const char * end)
    {
        int result = end ? strncasecmp(start, "true", end-start) : strcasecmp(start, "true");
        lua_pushboolean(L, result == 0);
    }

    // This function is for working out the binary size of a Lua blob
    int
    stringCounter( lua_State *, const void*, size_t sz, void* ud )
    {
        size_t * counter = (size_t*)ud;
        *counter += sz;
        return 0;
    }
    
    // Remove newlines from a line read from a CSV file.
    void stripNewlines(std::string & lineBuffer)
    {
        while (!lineBuffer.empty() && (lineBuffer.back() == '\r' || lineBuffer.back() == '\n'))
        {
            lineBuffer.pop_back();
        }
    }

    bool readColumnNames(std::vector<std::string> & inputColumns, std::ifstream & inputData, bool insensitive)
    {
        std::string lineBuffer;
        if (getline(inputData, lineBuffer).fail())
        {
            return false;
        }
        stripNewlines(lineBuffer);
        
        const char * token = lineBuffer.c_str();
        while (const char * nextToken = strchr(token, ','))
        {
            if (insensitive)
            {
                inputColumns.emplace_back(lowercaseString(token, nextToken));
            }
            else
            {
                inputColumns.emplace_back(token, nextToken - token);
            }
            token = nextToken + 1;
        }
        
        if (insensitive)
        {
            inputColumns.emplace_back(lowercaseString(token, token + strlen(token)));
        }
        else
        {
            inputColumns.emplace_back(token);
        }
        return true;
    }

    // Create a new Lua environment with the given source code already loaded.
    lua_State * buildEnv(const char * sourceFile, std::string & sourceCode, bool lowercase, std::vector<PMMLExporter::ModelOutput> & inputColumns, std::vector<PMMLExporter::ModelOutput> & customOutputs, size_t & nOverflowedVariables)
    {
        std::stringstream mystream;
        LuaOutputter output(mystream, lowercase ? LuaOutputter::OPTION_LOWERCASE : 0);

        if (!PMMLExporter::createScript(sourceFile, output, inputColumns, customOutputs))
        {
            return nullptr;
        }
    
        nOverflowedVariables = output.nOverflowedVariables();
        
        lua_State * L = luaL_newstate();
        sourceCode = mystream.str();
        if (luaL_loadstring(L, sourceCode.c_str()))
        {
            std::cerr << lua_tostring( L , -1 ) << std::endl;
            std::cerr << sourceCode;
            return nullptr;
        }

        size_t count = 0;
#if LUA_VERSION_NUM >= 503
        lua_dump(L, stringCounter, &count, 0 );
#else
        lua_dump(L, stringCounter, &count);
#endif
        if (lua_pcall(L, 0, LUA_MULTRET, 0))
        {
            std::cerr << lua_tostring( L , -1 ) << std::endl;
            std::cerr << sourceCode;
            return nullptr;
        }

        luaL_openlibs(L);

        printf("Loaded model (%zu bytes source, %zu bytes compiled)\n", sourceCode.length(), count);
        
        return L;
    }
    
    // Display the heading of the outputs (can also be used for inputs, if you really want)
    void printColumnHeaders(std::ostream & output, const std::vector<PMMLExporter::ModelOutput> & outputs)
    {
        bool first = true;
        for (const auto & column : outputs)
        {
            if (column.field)
            {
                if (!first)
                {
                    output << ',';
                }
                first = false;
                output << column.variableOrAttribute;
            }
        }
        output << std::endl;
    }

    // Print the outputs from executing a model once, in the same order as the outputDictionary
    static void printOutputs(std::ostream & output, lua_State * L, const std::vector<PMMLExporter::ModelOutput> & outputs, size_t nOutputs)
    {
        // Figure out the first offset (number of expected returns)
        int outTableIndex = lua_gettop(L) - nOutputs;
        if (outTableIndex < 0)
        {
            output << "wrong number of return values";
            return;
        }
        
        bool first = true;
        for (auto & element : outputs)
        {
            if (element.field)
            {
                outTableIndex++;
                if (!first)
                {
                    output << ',';
                }
                first = false;
                if (lua_isnumber(L, outTableIndex))
                {
                    output << (lua_tonumber(L, outTableIndex) * element.factor + element.coefficient);
                }
                else if (const char * asString = lua_tostring(L, outTableIndex))
                {
                    output << asString;
                }
                else
                {
                    output << "nullptr";
                }
            }
        }
        output << std::endl;
    }
    
    // Print an error message based on a verification mismatch
    void complain(lua_State * L, const char * asString, const char * expecting, const char * endOfExpecting, const std::string & column, size_t line)
    {
        std::string expectingString;
        if (endOfExpecting != nullptr)
        {
            expectingString.assign(expecting, endOfExpecting - expecting);
        }
        else
        {
            expectingString.assign(expecting);
        }
        
        const char * typeOfOutput = lua_typename(L, lua_type(L, -1));
        std::cerr << "Verification failed at line " << line << " value " << column << ": expecting: " << expectingString << " got: " << std::string(asString ? asString : "something else")  << "(" << typeOfOutput << ")" << std::endl;
    }
    
    // Reads a line from the verification file and compares it with outputs.
    bool verifyOutputs(lua_State * L, const std::vector<PMMLExporter::ModelOutput> & verificationColumns, std::ifstream & verificationData, double epsilon, size_t line, size_t nOutputs)
    {
        std::string lineBuffer;
        if (!std::getline(verificationData, lineBuffer))
        {
            std::cerr << "Verification data ended too early" << std::endl;
            return false;
        }
        
        stripNewlines(lineBuffer);
        
        int outTableIndex = lua_gettop(L) - nOutputs;
        if (outTableIndex < 0)
        {
            std::cerr << "Not enough outputs" << std::endl;
            return false;
        }
        
        const char * token = lineBuffer.c_str();
        for (auto column = verificationColumns.begin(); column != verificationColumns.end(); ++column)
        {
            const char * nextToken = strchr(token, ',');
            if (column->field)
            {
                outTableIndex++;
                if (token == nextToken || token[0] == '\0')
                {
                    if (!lua_isnil(L, outTableIndex))
                    {
                        const char * asString = lua_tostring(L, outTableIndex);
                        complain(L, asString, "nil", nullptr, column->variableOrAttribute, line);
                        return false;
                    }
                }
                else if (column->field->field.dataType == PMMLDocument::TYPE_NUMBER)
                {
                    if (!lua_isnumber(L, outTableIndex))
                    {
                        const char * asString = lua_tostring(L, outTableIndex);
                        complain(L, asString, "number", nullptr, column->variableOrAttribute, line);
                        return false;
                    }
                    double targetVal = std::strtod(token, nullptr);
                    double actualVal = lua_tonumber(L, outTableIndex) * column->factor + column->coefficient;
                    if (fabs(targetVal - actualVal) > epsilon)
                    {
                        char asString[20];
                        snprintf(asString, sizeof(asString), "%f", actualVal);
                        complain(L, asString, token, nextToken, column->variableOrAttribute, line);
                        return false;
                    }
                }
                else if (column->field->field.dataType == PMMLDocument::TYPE_BOOL)
                {
                    if (!lua_isboolean(L, outTableIndex))
                    {
                        const char * asString = lua_tostring(L, outTableIndex);
                        complain(L, asString, "boolean", nullptr, column->variableOrAttribute, line);
                        return false;
                    }
                    
                    bool target = (nextToken ? strncasecmp(token, "true", nextToken-token) : strcasecmp(token, "true")) != 0;
                    bool actual = lua_toboolean(L, outTableIndex);
                    if (target != actual)
                    {
                        const char * asString = lua_tostring(L, outTableIndex);
                        complain(L, asString, token, nextToken, column->variableOrAttribute, line);
                        return false;
                    }
                }
                else
                {
                    if (!lua_isstring(L, outTableIndex))
                    {
                        const char * asString = lua_tostring(L, outTableIndex);
                        complain(L, asString, "string", nullptr, column->variableOrAttribute, line);
                        return false;
                    }
                    
                    std::string target;
                    if (nextToken != nullptr)
                    {
                        target.assign(token, nextToken - token);
                    }
                    else
                    {
                        target.assign(token);
                    }
                    
                    const char * actual = lua_tostring(L, outTableIndex);
                    if (target != std::string(actual))
                    {
                        const char * asString = lua_tostring(L, outTableIndex);
                        complain(L, asString, token, nextToken, column->variableOrAttribute, line);
                        return false;
                    }
                }
            }
            if (nextToken == nullptr)
            {
                break;
            }
            token = nextToken + 1;
        }
        return true;
    }
    
    // Runs the model for a single line of input
    bool executeThisLine(lua_State * L, const std::string & lineBuffer, const std::vector<PMMLExporter::ModelOutput> & inputColumns, bool insensitive, size_t hasOverflow, size_t nOutputs)
    {
        lua_getglobal(L, "func");
        const char * token = lineBuffer.c_str();
        lua_checkstack(L, std::min(size_t(200), inputColumns.size() + 2));
        size_t cols = 0;
        if (hasOverflow)
        {
            cols++;
            lua_createtable(L, hasOverflow, 0);
        }
        size_t overflowPos = lua_gettop(L);
        for (const auto & column : inputColumns)
        {
            bool quoted = false;
            if (*token == '"')
            {
                token++;
                quoted = true;
            }
            const char * nextToken = strchr(token, quoted ? '"' : ',');
            if (const auto field = column.field)
            {
                if (token == nextToken || token[0] == '\0')
                {
                    lua_pushnil(L);
                }
                else if (field->field.dataType == PMMLDocument::TYPE_NUMBER)
                {
                    pushNumber(L, token, nextToken);
                }
                else if (field->field.dataType == PMMLDocument::TYPE_BOOL)
                {
                    pushBool(L, token, nextToken);
                }
                else
                {
                    pushString(L, token, nextToken, insensitive);
                }
                
                if (field->overflowAssignment)
                {
                    lua_rawseti(L, overflowPos, field->overflowAssignment);
                }
                else
                {
                    cols++;
                }
            }
            if (nextToken == nullptr)
            {
                break;
            }
            if (quoted)
            {
                nextToken++;
            }
            token = nextToken + 1;
        }
        
        return (lua_pcall(L, cols, nOutputs, 0) == 0);
    }
    
    // When something didn't work (verification failed, or exception thrown), we try to give a hint why.
    // Like executeThisLine, but with tracing. Will print out annotated source code when it is done.
    bool debugThisLine(lua_State * L, const std::string & lineBuffer, const std::vector<PMMLExporter::ModelOutput> & inputColumns, const std::string & sourceCode, bool insensitive, bool hasOverflow, size_t nOutputs)
    {
        static std::vector<bool> linesExecuted;
        linesExecuted.clear();
        // Do not pre-allocate since we haven't counted the number of lines.
        lua_sethook(L,
                    [](lua_State *, lua_Debug *ar)
                    {
                        int line = ar->currentline;
                        if (int(linesExecuted.size()) <= line)
                        {
                            linesExecuted.resize(line + 1);
                        }
                        linesExecuted[line] = true;
                    }, LUA_MASKLINE, 0);
        
        executeThisLine(L, lineBuffer, inputColumns, insensitive, hasOverflow, nOutputs);
        
        lua_sethook(L, nullptr, 0, 0);
        
        std::stringstream lineReader(sourceCode);
        std::string sourceLineBuffer;
        size_t lineNumber = 1;
        while (std::getline(lineReader, sourceLineBuffer))
        {
            bool didExecute = linesExecuted.size() > lineNumber && linesExecuted[lineNumber];
            std::cout << (didExecute ? "*" : " ") << sourceLineBuffer << std::endl;
            lineNumber++;
        }
        return true;
    }
}

bool openVerificationFile(std::ifstream & verificationData, std::vector<PMMLExporter::ModelOutput> & verificationColumns, const char * verificationCSV, bool insensitive)
{
    verificationData.open( verificationCSV );
    if (verificationData.fail())
    {
        std::cerr << "Cannot open file: " << verificationCSV << "for reading" << std::endl;
        return false;
    }

    std::vector<std::string> columnNames;
    if (!readColumnNames(columnNames, verificationData, insensitive))
    {
        return false;
    }
    
    // Arrange the outputs into the same order as the verification columns
    for (size_t i = 0; i < columnNames.size(); ++i)
    {
        // This is the position where we want to put
        auto iter = i < verificationColumns.size() ? verificationColumns.begin() + i : verificationColumns.end();
        const std::string & name = columnNames[i];
        auto found = std::find_if(verificationColumns.begin(), verificationColumns.end(),
                                  [&](const PMMLExporter::ModelOutput & output)
                                  {
                                      return output.variableOrAttribute == name;
                                  });
        
        if (found == verificationColumns.end())
        {
            // Put an invalid column in here to ignore this column.
            verificationColumns.emplace(iter, name, name);
        }
        else if (found > iter)
        {
            // Something specified in outputs.
            std::swap(*found, *iter);
        }
        else if (found < iter)
        {
            std::cerr << "Column: " << name << " is specified more than once in verification file" << std::endl;
            // Put an invalid column in here to ignore this column.
            verificationColumns.emplace(iter, name, name);
        }
    }
    
    if (columnNames.size() < verificationColumns.size())
    {
        auto lastNeeded = verificationColumns.begin() + columnNames.size();
        for (auto iter = lastNeeded; iter != verificationColumns.end(); ++iter)
        {
            std::cerr << "Output: " << iter->variableOrAttribute << " is not specified in verification file" << std::endl;
        }

        // Cut down on the unused columns
        verificationColumns.erase(lastNeeded, verificationColumns.end());
    }
        
    return true;
}

bool PMMLExporter::doTestRun(const char * sourceFile, const std::vector<ModelOutput> & customOutputs, const char * inputCSV, const char * verificationCSV, double verificationEpsilon, bool lowercase, std::ostream & output)
{
    std::string sourceCode;
    std::vector<ModelOutput> outputs = customOutputs;

    std::ifstream inputData;
    inputData.open( inputCSV );
    if (inputData.fail())
    {
        std::cerr << "Cannot open file: " << inputCSV << "for reading" << std::endl;
        return false;
    }

    std::vector<PMMLExporter::ModelOutput> inputColumns;
    {
        std::vector<std::string> inputColumnNames;
        if (not readColumnNames(inputColumnNames, inputData, lowercase))
        {
            return false;
        }
        
        inputColumns.reserve(inputColumnNames.size());
        for (const auto & name : inputColumnNames)
        {
            inputColumns.emplace_back(name, name);
        }
    }
    
    std::vector<PMMLExporter::ModelOutput> verificationColumns;
    std::ifstream verificationData;
    if (verificationCSV)
    {
        // Re-arranges outputs to match the columns to verify
        if (!openVerificationFile(verificationData, outputs, verificationCSV, lowercase))
        {
            return false;
        }
    }

    size_t hasOverflow = 0;
    lua_State * L = buildEnv(sourceFile, sourceCode, lowercase, inputColumns, outputs, hasOverflow);
    if (L == nullptr)
    {
        return false;
    }

    if (verificationCSV == nullptr)
    {
        printColumnHeaders(output, outputs);
    }
    
    size_t nOutputs = 0;
    for (const auto & output : outputs)
    {
        if (output.field)
        {
            nOutputs++;
        }
    }
    timespec startTime;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &startTime);
    size_t lineNumber = 1; // First line is the header.
    std::string lineBuffer;
    bool ok = true;
    while (std::getline(inputData, lineBuffer))
    {
        lineNumber++;
        stripNewlines(lineBuffer);
        
        if (!executeThisLine(L, lineBuffer, inputColumns, lowercase, hasOverflow, nOutputs))
        {
            std::cerr << lua_tostring( L , -1 ) << " at input line: " << lineNumber << std::endl;
            debugThisLine(L, lineBuffer, inputColumns, sourceCode, lowercase, hasOverflow, nOutputs);
            ok = false;
            break;
        }
        
        if (verificationCSV)
        {
            if (!verifyOutputs(L, outputs, verificationData, verificationEpsilon, lineNumber, nOutputs))
            {
                debugThisLine(L, lineBuffer, inputColumns, sourceCode, lowercase, hasOverflow, nOutputs);
                printColumnHeaders(output, outputs);
                printOutputs(output, L, outputs, nOutputs);
                ok = false;
                break;
            }
        }
        else
        {
            printOutputs(output, L, outputs, nOutputs);
        }
        lua_pop(L, nOutputs);
    }

    lua_close(L);
    
    int count = lineNumber - 1;
    timespec endTime;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &endTime);
    long nanoseconds = (endTime.tv_sec - startTime.tv_sec) * 1000000000 + (endTime.tv_nsec - startTime.tv_nsec);
    fprintf(stderr, "%i runs in %li ns, %lins each run\n", count, nanoseconds, count == 0 ? 0 : (nanoseconds / count));
    return ok;
}
