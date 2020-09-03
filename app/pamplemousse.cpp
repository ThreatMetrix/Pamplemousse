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
//  Created by Caleb Moore on 2/9/20.
//

#include "modeloutput.hpp"
#include "basicexport.hpp"
#include "luaconverter/luaoutputter.hpp"
#include "testrun.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <stdlib.h>
#include <getopt.h>

static void printUsage(const char * programName)
{
    std::cout << "OVERVIEW:\tconverts PMML document to Lua" << std::endl;
    std::cout << std::endl;
    std::cout << "USAGE:\t\t" << programName << " (--test/--convert) [options] <input>" << std::endl;
    std::cout << std::endl;
    std::cout << "OPTIONS:\t--test\t\t\t\t\t\tCheck model output given a CSV input" << std::endl;
    std::cout << "\t\t\t--convert\t\t\t\t\tConvert model to LUA" << std::endl;
    std::cout << "\t\t-h\t--help\t\t\t\t\t\tDisplay this message." << std::endl;
    std::cout << "\t\t-i\t--insenstive\t\t\t\tConvert all strings to lower case\t\t\t\t\t(--test/--convert, optional)" << std::endl;
    std::cout << "\t\t-o\t--output <file>\t\t\t\tWrite to a file (defaults to stdout)\t\t\t\t(--test/--convert, optional)" << std::endl;
    std::cout << "\t\t-f\t--feature featurename\t\tDefine input\t\t\t\t\t\t\t\t\t\t(--convert, optional)" << std::endl;
    std::cout << "\t\t-p\t--prediction <name>=<name>\tOutput to a custom attribute.\t\t\t\t\t\t(--test/--convert, optional)" << std::endl;
    std::cout << "\t\t-d\t--data <file>\t\t\t\tCSV input file\t\t\t\t\t\t\t\t\t\t(--test, mandatory)" << std::endl;
    std::cout << "\t\t-v\t--verify <file>\t\t\t\tCSV input file\t\t\t\t\t\t\t\t\t\t(--test, optional)" << std::endl;
    std::cout << "\t\t-e\t--epsilon <epsilon>\t\t\tPrecision to verify output.\t\t\t\t\t\t\t(--test, optional)" << std::endl;
    std::cout << std::endl;
    std::cout << "For any output, you may reference any target/predicted or output value from the model. Furthermore, you may access any neuron's activation value through \"neuron:<id>\"" << std::endl;
    std::cout << "You may also put expression using +, -, * and / after an model output, but not before." << std::endl;
    std::cout << "E.g. \"--prediction probability=predicted_value*100+3\" is acceptable, but \"--prediction probability=100*predicted_value+3\" is not" << std::endl;
}

int main(int argc, char * const * argv)
{
    int isTest = 0;
    int isConvert = 0;
    struct option longopts[] = {
        { "test",      no_argument,     &isTest,       1 },
        { "convert",   no_argument,     &isConvert,    1 },
        { "insensitive", no_argument,    NULL,         'i' },
        { "data",      required_argument,NULL,         'd' },
        { "verify",    required_argument,NULL,         'v' },
        { "output",    required_argument,NULL,         'o' },
        { "feature",   required_argument,NULL,         'f' },
        { "prediction",required_argument,NULL,         'p' },
        { "help",      no_argument,      NULL,         'h' },
        { "epsilon",   required_argument,NULL,         'e' },
        { NULL,        0,                NULL,          0 }
    };

    const char * dataFile   = nullptr;
    const char * verifyFile = nullptr;
    const char * outputFile = nullptr;
    bool insensitive = false;
    double epsilon = 0.0001;
    std::vector<PMMLExporter::ModelOutput> inputs;
    std::vector<PMMLExporter::ModelOutput> outputs;
    char c;
    while ((c = getopt_long(argc, argv, "id:v:o:f:p:he:", longopts, NULL)) != -1)
    {
        if (c == 'i')
        {
            insensitive = true;
        }
        else if (c == 'd')
        {
            dataFile = optarg;
        }
        else if (c == 'v')
        {
            verifyFile = optarg;
        }
        else if (c == 'o')
        {
            outputFile = optarg;
        }
        else if (c == 'f')
        {
            inputs.emplace_back(optarg, optarg);
        }
        else if (c == 'p')
        {
            std::string customOutput(optarg);
            size_t separator = customOutput.find('=');
            if (separator != std::string::npos)
            {
                outputs.emplace_back(customOutput.substr(separator + 1), customOutput.substr(0, separator));
            }
            else
            {
                outputs.emplace_back(customOutput, customOutput);
            }
        }
        else if (c == 'h')
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (c == 'e')
        {
            char * endOfString;
            epsilon = strtod(optarg, &endOfString);
            if (*endOfString != '\0' && !isspace(*endOfString))
            {
                fprintf( stderr, "%s: Epsilon should be a number (found '%c')\n", argv[0], *endOfString);
                return -1;
            }
        }
        else if (c != 0)
        {
            fprintf( stderr, "%s: Unrecognised option: %s\n", argv[0], argv[optind]);
            printUsage(argv[0]);
            return -1;
        }
    }

    if (isTest + isConvert != 1)
    {
        fprintf( stderr, "%s: Requires exactly one of the following arguments: --%s, --%s\n", argv[0], longopts[0].name, longopts[1].name );
        printUsage(argv[0]);
        return -1;
    }

    if (optind >= argc)
    {
        std::cerr << argv[0] << ": No input files specified\n";
        return -1;
    }

    const char * sourceFile = argv[optind];
    std::ofstream outFileStream;
    if (outputFile)
    {
        outFileStream.open(outputFile);
        if (!outFileStream.is_open())
        {
            std::cerr << argv[0] << ": Cannot open " << outputFile << " for writing\n";
            return -1;
        }
    }

    if (isTest)
    {
        if (dataFile == nullptr)
        {
            std::cerr << argv[0] << ": No data file specified (required for test mode)\n";
            return -1;
        }
        
        if (!PMMLExporter::doTestRun(sourceFile, outputs, dataFile, verifyFile, epsilon, insensitive, outputFile ? outFileStream : std::cout))
        {
            return -1;
        }
    }
    else if (isConvert)
    {
        LuaOutputter output(outputFile ? outFileStream : std::cout, insensitive ? LuaOutputter::OPTION_LOWERCASE : 0);

        if (!PMMLExporter::createScript(sourceFile, output, inputs, outputs))
        {
            return -1;
        }
    }
    
    if (outputFile)
    {
        outFileStream.close();
    }

    return 0;
}
