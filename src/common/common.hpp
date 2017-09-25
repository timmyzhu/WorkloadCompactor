// common.hpp - Misc helper functions.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef _COMMON_HPP
#define _COMMON_HPP

#include <string>
#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <json/json.h>

using namespace std;

// Tests if a string s starts with the string beginning.
inline bool startsWith(const string& s, const string& beginning)
{
    if (s.length() < beginning.length()) {
        return false;
    }
    return (s.compare(0, beginning.length(), beginning) == 0);
}

// Read json file
inline bool readJson(const char* filename, Json::Value& json)
{
    // Open file
    ifstream inputFile(filename);
    if (!inputFile.good()) {
        cerr << "Failed to read json file " << filename << endl;
        return false;
    }
    // Parse file
    Json::Reader reader;
    if (!reader.parse(inputFile, json)) {
        cerr << "Failed to parse json file " << filename << endl;
        return false;
    }
    return true;
}
inline bool readJson(string filename, Json::Value& json)
{
    return readJson(filename.c_str(), json);
}

// Write json file
inline bool writeJson(const char* filename, const Json::Value& json)
{
    // Open file
    ofstream outputFile(filename);
    if (!outputFile.good()) {
        cerr << "Failed to open output file " << filename << endl;
        return false;
    }
    // Write json to file
    Json::StyledStreamWriter writer;
    writer.write(outputFile, json);
    return true;
}
inline bool writeJson(string filename, const Json::Value& json)
{
    return writeJson(filename.c_str(), json);
}

// Convert json to string
inline string jsonToString(const Json::Value& json)
{
    Json::StyledWriter writer;
    return writer.write(json);
}

// Convert string to json
inline bool stringToJson(string str, Json::Value& json)
{
    Json::Reader reader;
    return reader.parse(str, json);
}
inline bool stringToJson(const char* c_str, Json::Value& json)
{
    string str(c_str);
    return stringToJson(str, json);
}

// Convert a string internet address to an IP address
inline unsigned long addrInfo(string addr)
{
    unsigned long s_addr = 0;
    struct addrinfo* result;
    int err = getaddrinfo(addr.c_str(), NULL, NULL, &result);
    if ((err != 0) || (result == NULL)) {
        cerr << "Error in getaddrinfo: " << err << endl;
    } else {
        for (struct addrinfo* res = result; res != NULL; res = res->ai_next) {
            if (result->ai_addr->sa_family == AF_INET) {
                s_addr = ((struct sockaddr_in*)result->ai_addr)->sin_addr.s_addr;
                break;
            }
        }
        freeaddrinfo(result);
    }
    return s_addr;
}

#endif // _COMMON_HPP
