// BandwidthTableGen.cpp - tool for building storage profiles for WorkloadCompactor.
// Calculates read and write bandwidth as a function of request size ranging from 512b to 256kb.
// Bandwidth tests will perform random I/O to a target file of a given size, and is meant for profiling SSDs.
//
// Command line parameters:
// -s sizeMB (required) - size of target file to read/write from
// -t target (required) - target filename to read/write from
// -f configFilename (optional) - filename to output results; results will be merged into the json config file
// -c count (optional) - number of operations to perform for each bandwidth test; defaults to 10000
// -n numThreads (optional) - number of threads to use; defaults to 32
// -r numReadThreads (optional) - number of threads to use for read bandwidth tests; defaults to numThreads
// -w numWriteThreads (optional) - number of threads to use for write bandwidth tests; defaults to numThreads
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <iostream>
#include <cassert>
#include <random>
#include "../common/time.hpp"
#include "../common/common.hpp"
#include <json/json.h>

using namespace std;

typedef enum {
    OP_READ,
    OP_WRITE,
    OP_MAX
} disk_op_t;

typedef struct {
    disk_op_t diskOp;
    string filename;
    vector<uint64_t> offset;
    int* count;
    uint64_t requestSize;
} bandwidth_test_t;

// Allocate 512 byte aligned buffer
char* allocBuf(size_t len)
{
    char* origBuf = (char*)operator new(len + 512); // + 512 for alignment
    char* buf = (char*)((((uintptr_t)origBuf) + 512) & ~511ull);
    assert((uintptr_t)(((char**)buf)-1) >= (uintptr_t)origBuf);
    *(((char**)buf)-1) = origBuf; // Store origBuf in space before buf
    return buf;
}

// Free allocBuf buffer
void freeBuf(char* buf)
{
    char* origBuf = *(((char**)buf)-1); // Get origBuf in space before buf
    operator delete(origBuf);
}

void getRandomData(char *buf, int size)
{
    int randomSource = open("/dev/urandom", O_RDONLY);
    int bytesRead = 0;
    assert(size > 0);
    while (bytesRead < size) {
       ssize_t numb = read(randomSource, buf + bytesRead, size - bytesRead);
       if (numb < 0) {
           cerr << "Failed to read from /dev/urandom, errno: " <<errno <<endl;
           exit(-1);
       }
       bytesRead += numb;
    }
    close(randomSource);
}
// Performs read/write requests of size requestSize at offsets specified in offset vector
void* workerThread(void *arg)
{
    bandwidth_test_t* args = (bandwidth_test_t*)arg;
    char* buf = allocBuf(args->requestSize); // O_DIRECT requires 512 byte aligned buffer for local filesystems (but not for NFS)

    if (args->diskOp == OP_WRITE) {
        getRandomData(buf, args->requestSize);
    }

    // Open file
    int fd = open(args->filename.c_str(), O_RDWR | O_DIRECT);
    if (fd == -1) {
        cerr << "Failed open errno: " << errno << endl;
        exit(-1);
    }

    uint64_t index;
    uint64_t totalCount = static_cast<uint64_t>(args->offset.size());
    while ((index = __sync_fetch_and_add(args->count, 1)) < totalCount) {
        uint64_t offset = args->offset[index];
        uint64_t numb = 0;
        if (args->diskOp == OP_READ) {
            numb = pread(fd, buf, args->requestSize, offset);
        } else if (args->diskOp == OP_WRITE) {
            numb = pwrite(fd, buf, args->requestSize, offset);
        }
        if (numb != args->requestSize) {
            cerr << "Failed to pread/pwrite " << numb << " errno: " << errno << endl;
            exit(-1);
        }
    }
    // Close file
    close(fd);

    freeBuf(buf);
    return NULL;
}

int main(int argc, char** argv)
{
    // Process command line options
    int opt = 0;
    int count = 10000;
    int numThreads = 32;
    int numReadThreads = 0;
    int numWriteThreads = 0;
    int sizeMB = 0;
    string target = "";
    string configFilename;
    mt19937_64 generator;
    random_device rd;
    generator.seed(rd());
    do {
        opt = getopt(argc, argv, "s:t:f:c:n:r:w:");
        switch (opt) {
            case 's':
                sizeMB = atoi(optarg);
                break;

            case 't':
                target.assign(optarg);
                break;

            case 'f':
                configFilename.assign(optarg);
                break;

            case 'c':
                count = atoi(optarg);
                break;

            case 'n':
                numThreads = atoi(optarg);
                break;

            case 'r':
                numReadThreads = atoi(optarg);
                break;

            case 'w':
                numWriteThreads = atoi(optarg);
                break;

            case -1:
                break;

            default:
                cerr << "Usage: " << argv[0] << " -s sizeMB -t target [-f configFilename] [-c count] [-n numThreads] [-r numReadThreads] [-w numWriteThreads]" << endl;
                exit(-1);
                break;
        }
    } while (opt != -1);
    if (numReadThreads <= 0) {
        numReadThreads = numThreads;
    }
    if (numWriteThreads <= 0) {
        numWriteThreads = numThreads;
    }

    // Check arguments
    if ((sizeMB < 1) || (target == "") || (count < 1) || (numReadThreads < 1) || (numWriteThreads < 1)) {
        cerr << "Usage: " << argv[0] << " -s sizeMB -t target [-f configFilename] [-c count] [-n numThreads] [-r numReadThreads] [-w numWriteThreads]" << endl;
        exit(-1);
    }

    // Open base config file
    Json::Value root;
    if (!configFilename.empty()) {
        // Read config file
        if (!readJson(configFilename, root)) {
            return -1;
        }
    }

    // Profile bandwidth
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_t* threadArray = new pthread_t[max(numReadThreads, numWriteThreads)];
    if (threadArray == NULL) {
        cerr << "Failed to create thread array" << endl;
        exit(1);
    }

    vector<unsigned int> requestSizes;
    //for (unsigned int requestSize = 512; requestSize < 65536; requestSize += 512) {
    //    requestSizes.push_back(requestSize);
    //}
    requestSizes.push_back(64 * 1024);
    requestSizes.push_back(96 * 1024);
    //requestSizes.push_back(128 * 1024);
    //requestSizes.push_back(192 * 1024);
    //requestSizes.push_back(256 * 1024);
    vector<double> bwReadTable;
    vector<double> bwWriteTable;

    for (unsigned int j = 0; j < requestSizes.size(); j++) { // for each request size
        for (int diskOp = OP_READ; diskOp < OP_MAX; diskOp++) { // for each operation type
            int index = 0;
            bandwidth_test_t args;
            args.diskOp = (disk_op_t)diskOp;
            args.filename = target;
            args.count = &index;
            args.requestSize = requestSizes[j];
            int maxBlock = ((((uint64_t)sizeMB) * 1024ull * 1024ull) / args.requestSize) - 1;
            uniform_int_distribution<uint64_t> distribution(0, maxBlock);
            for (int i = 0; i < count; i++) {
                args.offset.push_back(args.requestSize * distribution(generator));
            }

            uint64_t startTime = GetTime();
            // Create worker threads
            numThreads = (diskOp == OP_READ) ? numReadThreads : numWriteThreads;
            for (int i = 0; i < numThreads; i++) {
                int rc = pthread_create(&threadArray[i],
                                        &attr,
                                        workerThread,
                                        (void*)&args);
                if (rc) {
                    cerr << "Error creating thread: " << rc << " errno: " << errno << endl;
                    exit(-1);
                }
            }

            // Join all threads
            for (int i = 0; i < numThreads; i++) {
                int rc = pthread_join(threadArray[i], NULL);
                if (rc) {
                    cerr << "Error joining thread: " << rc << " errno: " << errno << endl;
                    exit(-1);
                }
            }

            // Record bandwidth
            uint64_t endTime = GetTime();
            double duration = ConvertTimeToSeconds(endTime - startTime);
            double bw = ((double)args.requestSize * (double)count) / duration;
            if (diskOp == OP_READ) {
                bwReadTable.push_back(bw);
                cout << "Read " << args.requestSize << ": " << (bw / 1024.0 / 1024.0) << " MB/s" << endl;
            } else if (diskOp == OP_WRITE) {
                bwWriteTable.push_back(bw);
                cout << "Write " << args.requestSize << ": " << (bw / 1024.0 / 1024.0) << " MB/s" << endl;
            }
            RelativeSleepUninterruptible(ConvertSecondsToTime(10));
        }
    }

    // Fill in bandwidth table
    Json::Value& bwTable = root["bandwidthTable"];
    bwTable.clear();
    for (unsigned int j = 0; j < requestSizes.size(); j++) {
        Json::Value& bwTableEntry = bwTable[j];
        bwTableEntry["requestSize"] = requestSizes[j]; // in B
        bwTableEntry["readBandwidth"] = bwReadTable[j]; // bw in B/s
        bwTableEntry["writeBandwidth"] = bwWriteTable[j];  // bw in B/s
    }

    // Output results
    if (!configFilename.empty()) {
        writeJson(configFilename, root);
    } else {
        // Print results
        cout << root;
    }

    // Cleanup
    pthread_attr_destroy(&attr);
    delete[] threadArray;
    return 0;
}
