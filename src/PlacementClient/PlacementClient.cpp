// PlacementClient.cpp - initiates placement of workloads onto servers for experimentation purposes.
// Communicates with PlacementController to place workloads (a.k.a. clients).
//
// Command line parameters:
// -t topoFilename (required) - topology file that specifies the workloads and system configuration; see README for file format
// -o outputFilename (required) - output file to store the results of the workload placement
// -s serverAddr (required) - the address of the PlacementController server
// -e eventFilename (optional) - a file for experimentation purposes to add and remove instances of a workload from the system; see below for format; if not specified, by default each workload in the topology file will be added to the system.
//
// Events file format: CSV file with 2 columns. 
// The first column corresponds to the index of the workload in the topology file.
// The second column is either addClient or delClient to indicate whether to add or remove the workload from the system.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include "../prot/PlacementController_clnt.hpp"
#include <json/json.h>
#include "../common/time.hpp"
#include "../common/common.hpp"

using namespace std;

struct EventInfo {
    unsigned int clientInfoIndex;
    bool addClient;
};

Json::Value rootConfig;
char* outputFilename = NULL;

// SIGTERM/SIGINT signal for cleanup
void term_signal(int signum)
{
    writeJson(outputFilename, rootConfig);
    exit(0);
}

int main(int argc, char** argv)
{
    int opt = 0;
    char* topoFilename = NULL;
    char* eventFilename = NULL;
    string serverAddr = "";
    do {
        opt = getopt(argc, argv, "t:o:s:e:");
        switch (opt) {
            case 't':
                topoFilename = optarg;
                break;

            case 'o':
                outputFilename = optarg;
                break;

            case 's':
                serverAddr.assign(optarg);
                break;

            case 'e':
                eventFilename = optarg;
                break;

            case -1:
                break;

            default:
                break;
        }
    } while (opt != -1);

    if ((topoFilename == NULL) || (outputFilename == NULL) || (serverAddr == "")) {
        cout << "Usage: " << argv[0] << " -t topoFilename -o outputFilename -s serverAddr [-e eventFilename]" << endl;
        return -1;
    }

    // Read config
    if (!readJson(topoFilename, rootConfig)) {
        return -1;
    }
    // Setup signal handler
    struct sigaction action;
    action.sa_handler = term_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    // Connect to PlacementController server
    PlacementController_clnt clnt(serverAddr);
    // Add clientVMs
    Json::Value& clientVMs = rootConfig["clientVMs"];
    for (unsigned int clientVMIndex = 0; clientVMIndex < clientVMs.size(); clientVMIndex++) {
        Json::Value& clientVM = clientVMs[clientVMIndex];
        clnt.addClientVM(clientVM["clientHost"].asString(), clientVM["clientVM"].asString());
    }
    // Add serverVMs
    Json::Value& serverVMs = rootConfig["serverVMs"];
    for (unsigned int serverVMIndex = 0; serverVMIndex < serverVMs.size(); serverVMIndex++) {
        Json::Value& serverVM = serverVMs[serverVMIndex];
        clnt.addServerVM(serverVM["serverHost"].asString(), serverVM["serverVM"].asString());
    }
    // Process events file, or by default add one of every client in order in topology file
    Json::Value& clientInfos = rootConfig["clients"];
    vector<EventInfo> events;
    if (eventFilename) {
        ifstream file(eventFilename);
        if (file.is_open()) {
            string line;
            EventInfo event;
            char isAdd[32];
            while (getline(file, line)) {
                // Parse line
                if (sscanf(line.c_str(), "%d,%[^,]", &event.clientInfoIndex, isAdd) == 2) {
                    event.addClient = (strcmp(isAdd, "addClient") == 0);
                    events.push_back(event);
                }
            }
        }
    } else {
        EventInfo event;
        event.addClient = true;
        for (event.clientInfoIndex = 0; event.clientInfoIndex < clientInfos.size(); event.clientInfoIndex++) {
            events.push_back(event);
        }
    }
    // Add/remove clients according to events
    string addrPrefix = rootConfig["addrPrefix"].asString();
    bool enforce = rootConfig.isMember("enforce") && rootConfig["enforce"].asBool();
    for (unsigned int eventIndex = 0; eventIndex < events.size(); eventIndex++) {
        const EventInfo& event = events[eventIndex];
        Json::Value& clientInfo = clientInfos[event.clientInfoIndex];
        if (event.addClient) {
            bool admitted = clnt.addClient(clientInfo, addrPrefix, enforce);
            if (admitted) {
                cout << "Placed " << clientInfo["name"].asString() << " (" << clientInfo["clientHost"].asString() << ", " << clientInfo["clientVM"].asString() << ") -> (" << clientInfo["serverHost"].asString() << ", " << clientInfo["serverVM"].asString() << ")" << endl;
            } else {
                cout << "Rejected " << clientInfo["name"].asString() << endl;
            }
        } else {
            clnt.delClient(clientInfo["name"].asString());
        }
    }
    // Write config
    if (!writeJson(outputFilename, rootConfig)) {
        return -1;
    }
    return 0;
}
