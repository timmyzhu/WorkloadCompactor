// scheduler.cpp - Code for the storage scheduler.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <iostream>
#include <limits>
#include <cassert>
#include <cstring>
#include <stdint.h>
#include <errno.h>
#include <pthread.h>
#include "scheduler.hpp"
#include <json/json.h>
#include "../common/time.hpp"
#include "../Estimator/Estimator.hpp"

using namespace std;

// Get a client, possibly creating a new client.
// Assumes mutex held
Client& Scheduler::GetClient(unsigned long s_addr)
{
    // Lookup client
    map<unsigned long, Client>::iterator it = _clients.find(s_addr);
    if (it != _clients.end()) {
        return it->second;
    }
    // If new client, create one
    Client& c = _clients[s_addr];
    c.priority = 0;
    c.rateLimitLength = 0; // no rate-limiting by default for the purposes of profiling
    c.rateLimitRates = new double[c.rateLimitLength];
    c.rateLimitBursts = new double[c.rateLimitLength];
    c.rateLimitTokens = new double[c.rateLimitLength];
    c.rateLimitUpdateTime = 0;
    c.rateLimitObeyed = false;
    c.occupancy = 0;
    uint64_t now = GetTime();
    c.lastOccupancyTime = now;
    c.getOccupancyTime = now;
    return c;
}

// Update client parameters.
void Scheduler::UpdateClient(unsigned long s_addr, unsigned int priority, int rateLimitLength, double* rateLimitRates, double* rateLimitBursts)
{
    // Request ownership of the mutex
    pthread_mutex_lock(&_schedulerMutex);
    Client& c = GetClient(s_addr);
    c.priority = priority;
    delete[] c.rateLimitRates;
    delete[] c.rateLimitBursts;
    delete[] c.rateLimitTokens;
    c.rateLimitLength = rateLimitLength;
    c.rateLimitRates = new double[c.rateLimitLength];
    c.rateLimitBursts = new double[c.rateLimitLength];
    c.rateLimitTokens = new double[c.rateLimitLength];
    for (int i = 0; i < c.rateLimitLength; i++) {
        c.rateLimitRates[i] = rateLimitRates[i];
        c.rateLimitBursts[i] = rateLimitBursts[i];
        c.rateLimitTokens[i] = rateLimitBursts[i];
    }
    c.rateLimitObeyed = false;
    // Release ownership of the mutex
    pthread_mutex_unlock(&_schedulerMutex);
}

// Return queue occupancy for a client since last call for the client.
double Scheduler::GetOccupancy(unsigned long s_addr)
{
    // Request ownership of the mutex
    pthread_mutex_lock(&_schedulerMutex);
    Client& c = GetClient(s_addr);
    uint64_t now = GetTime();
    uint64_t occupancyTime = c.occupancy;
    // Update occupancy for partial period
    if (!c.pendingJobs.empty()) {
        occupancyTime += now - c.lastOccupancyTime;
    }
    double occupancy = ((double)occupancyTime) / ((double)(now - c.getOccupancyTime));
    // Clear stats
    c.occupancy -= occupancyTime;
    c.getOccupancyTime = now;
    // Release ownership of the mutex
    pthread_mutex_unlock(&_schedulerMutex);
    return occupancy;
}

// Return number of pending jobs for a client.
int Scheduler::GetNumPendingJobs(unsigned long s_addr)
{
    // Request ownership of the mutex
    pthread_mutex_lock(&_schedulerMutex);
    Client& c = GetClient(s_addr);
    int numPending = c.pendingJobs.size();
    // Release ownership of the mutex
    pthread_mutex_unlock(&_schedulerMutex);
    return numPending;
}

// Submit job to scheduler queue.
void Scheduler::SubmitJob(Job* pJob)
{
    // Request ownership of the mutex
    pthread_mutex_lock(&_schedulerMutex);
    // Add job
    AddJob(pJob);
    // Release ownership of the mutex
    pthread_mutex_unlock(&_schedulerMutex);
}

// Get the next job to send to storage.
Job* Scheduler::GetNextJob()
{
    // Request ownership of the mutex
    pthread_mutex_lock(&_schedulerMutex);
    Job* pJob = ScheduleJob();
    while (pJob == NULL) {
      pthread_cond_wait(&_availableJobsCV, &_schedulerMutex);
      pJob = ScheduleJob();
    }
    // Release ownership of the mutex
    pthread_mutex_unlock(&_schedulerMutex);
    return pJob;
}

// Indicate job is completed.
void Scheduler::CompleteJob(Job* pJob)
{
    // Request ownership of the mutex
    pthread_mutex_lock(&_schedulerMutex);
    // Readjust maximum number of outstanding jobs for immediate jobs
    if (pJob->Immediate()) {
        _maxOutstandingJobs--;
    }
    // Track outstanding jobs and bytes
    _outstandingJobs--;
    if (pJob->IsReadRequest()) {
        _outstandingReadJobs--;
        _outstandingReadBytes -= pJob->RequestSize();
    } else if (pJob->IsWriteRequest()) {
        _outstandingWriteJobs--;
        _outstandingWriteBytes -= pJob->RequestSize();
    }
    // Track outstanding priority
    if (pJob->rateLimitObeyed) {
        RemoveOutstandingPriority(pJob);
    }
    // Broadcast new jobs may be able to run
    if (_pendingJobCount > 0) {
        pthread_cond_broadcast(&_availableJobsCV);
    }
    // Release ownership of the mutex
    pthread_mutex_unlock(&_schedulerMutex);
}

// Return NFS RPC client resources once a job completes.
void Scheduler::ReturnClient(Job* pJob)
{
    // Request ownership of the mutex
    pthread_mutex_lock(&_schedulerMutex);
    _RPCAvailableClients.push_back(pJob->RPCClient());
    // Release ownership of the mutex
    pthread_mutex_unlock(&_schedulerMutex);
}

// Returns job size estimate.
double Scheduler::EstimateJobSize(Client& c, Job* pJob)
{
    // Non-read/write requests are treated as free for now
    if (!pJob->IsReadRequest() && !pJob->IsWriteRequest()) {
        return 0;
    }
    return _pEst->estimateWork(pJob->RequestSize(), pJob->IsReadRequest());
}

// Compares two clients to see which should be scheduled.
// Assumes mutex held
// Assumes UpdateTokens has been called to update rateLimitObeyed flags
#define PREFER_1 1
#define PREFER_EQUAL 0
#define PREFER_2 -1
int Scheduler::CompareClient(const Client& c1, const Client& c2)
{
    // Check for pending job
    if (c1.pendingJobs.empty()) {
        if (c2.pendingJobs.empty()) {
            return PREFER_EQUAL;
        } else {
            return PREFER_2;
        }
    } else if (c2.pendingJobs.empty()) {
        return PREFER_1;
    }

    // Check immediate flag
    Job* pJob1 = c1.pendingJobs.front();
    Job* pJob2 = c2.pendingJobs.front();
    if (pJob1->Immediate()) {
        if (!pJob2->Immediate()) {
            return PREFER_1;
        }
    } else if (pJob2->Immediate()) {
        return PREFER_2;
    }

    // Check if within rate limit
    if (c1.rateLimitObeyed) {
        if (c2.rateLimitObeyed) {
            // Check priority
            // Priority only applies when both clients are within rate limits
            if (c1.priority < c2.priority) {
                return PREFER_1;
            } else if (c1.priority > c2.priority) {
                return PREFER_2;
            }
        } else {
            return PREFER_1;
        }
    } else if (c2.rateLimitObeyed) {
        return PREFER_2;
    }

    // When rate limits/priority have not determined which client is better,
    // then switch to using FCFS
    if (pJob1->ArrivalTime() < pJob2->ArrivalTime()) {
        return PREFER_1;
    } else if (pJob1->ArrivalTime() > pJob2->ArrivalTime()) {
        return PREFER_2;
    }
    return PREFER_EQUAL;
}

// Update token buckets in order to check rate limits
// Assumes mutex held
void Scheduler::UpdateTokens(Client& c, uint64_t now)
{
    // Only update when queue is non empty
    if (!c.pendingJobs.empty()) {
        Job* pJob = c.pendingJobs.front();
        // Only update if we're may not be within rate limits
        if (!c.rateLimitObeyed) {
            c.rateLimitObeyed = true;
            // Update token buckets with burst limits when the queue was empty
            if (c.rateLimitUpdateTime < c.lastOccupancyTime) {
                double elapsedTime = ConvertTimeToSeconds(c.lastOccupancyTime - c.rateLimitUpdateTime);
                for (int i = 0; i < c.rateLimitLength; i++) {
                    c.rateLimitTokens[i] += elapsedTime * c.rateLimitRates[i];
                    if (c.rateLimitTokens[i] > c.rateLimitBursts[i]) {
                        c.rateLimitTokens[i] = c.rateLimitBursts[i];
                    }
                }
                c.rateLimitUpdateTime = c.lastOccupancyTime;
            }
            // Update token buckets without burst limits for the time that queue is non empty
            assert(c.rateLimitUpdateTime >= c.lastOccupancyTime);
            double elapsedTime = ConvertTimeToSeconds(now - c.rateLimitUpdateTime);
            c.rateLimitUpdateTime = now;
            for (int i = 0; i < c.rateLimitLength; i++) {
                c.rateLimitTokens[i] += elapsedTime * c.rateLimitRates[i];
                // Check if not enough tokens for meeting rate limit
                if (pJob->JobSize() > c.rateLimitTokens[i]) {
                    c.rateLimitObeyed = false;
                }
            }
        }
    }
}

// Add a job to the scheduler queue.
// Assumes mutex held
void Scheduler::AddJob(Job* pJob)
{
    uint64_t now = GetTime();
    Client& c = GetClient(pJob->Addr());
    // Set arrival time
    pJob->arrivalTime = now;
    // Initialize job size
    pJob->jobSize = EstimateJobSize(c, pJob);
    // Initialize RPC client
    pJob->cl = NULL;
    // Update occupancy time
    if (c.pendingJobs.empty()) {
        c.lastOccupancyTime = now;
        // Signal new job might be available
        pthread_cond_signal(&_availableJobsCV);
    }
    // Add job to queue
    c.pendingJobs.push_back(pJob);
    _pendingJobCount++;
}

// Remove a job from the scheduler queue to submit it to storage.
// Assumes mutex held
Job* Scheduler::RemoveJob(Client& c)
{
    // Remove job from queue
    assert(!c.pendingJobs.empty());
    Job* pJob = c.pendingJobs.front();
    c.pendingJobs.pop_front();
    _pendingJobCount--;
    uint64_t now = GetTime();
    // Update occupancy
    if (c.pendingJobs.empty()) {
        c.occupancy += now - c.lastOccupancyTime;
    }
    // Update estimator history
    assert(pJob->jobSize >= 0);
    pJob->rateLimitObeyed = c.rateLimitObeyed;
    // Decrement token buckets by usage
    for (int i = 0; i < c.rateLimitLength; i++) {
        c.rateLimitTokens[i] -= pJob->JobSize();
        // Prevent best effort traffic from making tokens go negative
        if (c.rateLimitTokens[i] < 0) {
            c.rateLimitTokens[i] = 0;
        }
    }
    // Clear rate limit flag (will be rechecked next UpdateTokens)
    c.rateLimitObeyed = false;
    return pJob;
}

// Find the best client to schedule next.
// Assumes mutex held
Client& Scheduler::FindBestClient()
{
    uint64_t now = GetTime();
    // Loop through clients and find the best client
    map<unsigned long, Client>::iterator bestClientIt = _clients.begin();
    for (map<unsigned long, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        // Update token buckets
        UpdateTokens(it->second, now);
        // Compare against best client
        if (CompareClient(it->second, bestClientIt->second) > 0) {
            bestClientIt = it;
        }
    }
    return bestClientIt->second;
}

// Try to schedule next job.
// Assumes mutex held
Job* Scheduler::ScheduleJob()
{
    // Check if there are pending jobs
    if (_pendingJobCount > 0) {
        // Check if we are out of clients
        if (_RPCAvailableClients.empty()) {
            return NULL;
        }
        Client& c = FindBestClient();
        Job* pJob = c.pendingJobs.front();
        assert(_outstandingJobs <= _maxOutstandingJobs);
        // Immediate jobs get to increase the maximum number of outstanding jobs
        if (pJob->Immediate()) {
            _maxOutstandingJobs++;
        } else {
            // Check if too many outstanding jobs
            if (_outstandingJobs >= _maxOutstandingJobs) {
                return NULL;
            }
            if (pJob->IsReadRequest()) {
                // Check if too many outstanding read jobs/bytes
                if (_outstandingReadJobs >= _maxOutstandingReadJobs) {
                    return NULL;
                } else if ((_outstandingReadBytes + pJob->RequestSize()) >= _maxOutstandingReadBytes) {
                    return NULL;
                }
                // Ensure that higher priority jobs are not being starved by low priority jobs
                uint64_t oldestHigherPrioritySeqNumRead = _seqNumRead;
                uint64_t oldestHigherPrioritySeqNumReadBytes = _seqNumReadBytes;
                // Assumes list is sorted by oldest request
                for (list<Job*>::iterator it = _outstandingPriorityList.begin(); it != _outstandingPriorityList.end(); it++) {
                    if ((*it)->priority < c.priority) {
                        oldestHigherPrioritySeqNumRead = (*it)->seqNumRead;
                        oldestHigherPrioritySeqNumReadBytes = (*it)->seqNumReadBytes;
                        break;
                    }
                }
                if (_seqNumRead > (oldestHigherPrioritySeqNumRead + _maxOutstandingReadJobs)) {
                    return NULL;
                } else if ((_seqNumReadBytes + pJob->RequestSize()) >= (oldestHigherPrioritySeqNumReadBytes + _maxOutstandingReadBytes)) {
                    return NULL;
                }
            } else if (pJob->IsWriteRequest()) {
                // Check if too many outstanding write jobs/bytes
                if (_outstandingWriteJobs >= _maxOutstandingWriteJobs) {
                    return NULL;
                } else if ((_outstandingWriteBytes + pJob->RequestSize()) >= _maxOutstandingWriteBytes) {
                    return NULL;
                }
                // Ensure that higher priority jobs are not being starved by low priority jobs
                uint64_t oldestHigherPrioritySeqNumWrite = _seqNumWrite;
                uint64_t oldestHigherPrioritySeqNumWriteBytes = _seqNumWriteBytes;
                // Assumes list is sorted by oldest request
                for (list<Job*>::iterator it = _outstandingPriorityList.begin(); it != _outstandingPriorityList.end(); it++) {
                    if ((*it)->priority < c.priority) {
                        oldestHigherPrioritySeqNumWrite = (*it)->seqNumWrite;
                        oldestHigherPrioritySeqNumWriteBytes = (*it)->seqNumWriteBytes;
                        break;
                    }
                }
                if (_seqNumWrite > (oldestHigherPrioritySeqNumWrite + _maxOutstandingWriteJobs)) {
                    return NULL;
                } else if ((_seqNumWriteBytes + pJob->RequestSize()) >= (oldestHigherPrioritySeqNumWriteBytes + _maxOutstandingWriteBytes)) {
                    return NULL;
                }
            }
        }
        pJob = RemoveJob(c);
        // Track outstanding priority (only for requests that obey rate limit)
        pJob->priority = c.priority;
        pJob->seqNumRead = _seqNumRead;
        pJob->seqNumWrite = _seqNumWrite;
        pJob->seqNumReadBytes = _seqNumReadBytes;
        pJob->seqNumWriteBytes = _seqNumWriteBytes;
        if (pJob->IsReadRequest()) {
            _seqNumRead++;
            _seqNumReadBytes += pJob->RequestSize();
        } else if (pJob->IsWriteRequest()) {
            _seqNumWrite++;
            _seqNumWriteBytes += pJob->RequestSize();
        }
        if (pJob->rateLimitObeyed) {
            _outstandingPriorityList.push_back(pJob);
        }
        // Release job for execution
        pJob->cl = _RPCAvailableClients.back();
        _RPCAvailableClients.pop_back();
        _outstandingJobs++;
        if (pJob->IsReadRequest()) {
            _outstandingReadJobs++;
            _outstandingReadBytes += pJob->RequestSize();
        } else if (pJob->IsWriteRequest()) {
            _outstandingWriteJobs++;
            _outstandingWriteBytes += pJob->RequestSize();
        }
	return pJob;
    }
    return NULL;
}

// Remove job from outstanding priority list.
void Scheduler::RemoveOutstandingPriority(Job* pJob)
{
    for (list<Job*>::iterator it = _outstandingPriorityList.begin(); it != _outstandingPriorityList.end(); it++) {
        if (*it == pJob) {
            _outstandingPriorityList.erase(it);
            break;
        }
    }
}

// Keep NFS RPC clients alive via periodic NULL requests.
static struct timeval TIMEOUT = { 25, 0 };
bool Scheduler::KeepAlive()
{
    // Request ownership of the mutex
    pthread_mutex_lock(&_schedulerMutex);
    for (vector<CLIENT*>::iterator it = _RPCAvailableClients.begin(); it != _RPCAvailableClients.end();) {
        char clnt_res = 0;
        enum clnt_stat rpcStatus = clnt_call(*it, NFSPROC3_NULL,
                                             (xdrproc_t)xdr_void, (caddr_t)NULL,
                                             (xdrproc_t)xdr_void, (caddr_t)&clnt_res,
                                             TIMEOUT);
        if (rpcStatus == RPC_SUCCESS) {
            it++;
        } else {
            it = _RPCAvailableClients.erase(it);
        }
    }
    // Release ownership of the mutex
    pthread_mutex_unlock(&_schedulerMutex);
    return _keepAlive;
}
void* KeepAliveThread(void* ptr)
{
    Scheduler* sched = (Scheduler*)ptr;
    uint64_t t = GetTime();
    do {
        t += ConvertSecondsToTime(60);
        AbsoluteSleepUninterruptible(t);
    } while (sched->KeepAlive());
    return NULL;
}

Scheduler::Scheduler(vector<CLIENT*> RPCClients, int maxOutstandingReadBytes, int maxOutstandingWriteBytes, int maxReadJobs, int maxWriteJobs, Estimator* pEst)
    : _RPCAvailableClients(RPCClients),
      _seqNumRead(0),
      _seqNumWrite(0),
      _seqNumReadBytes(0),
      _seqNumWriteBytes(0),
      _outstandingReadBytes(0),
      _maxOutstandingReadBytes(maxOutstandingReadBytes),
      _outstandingWriteBytes(0),
      _maxOutstandingWriteBytes(maxOutstandingWriteBytes),
      _outstandingJobs(0),
      _maxOutstandingJobs(maxReadJobs + maxWriteJobs),
      _outstandingReadJobs(0),
      _maxOutstandingReadJobs(maxReadJobs),
      _outstandingWriteJobs(0),
      _maxOutstandingWriteJobs(maxWriteJobs),
      _pendingJobCount(0),
      _pEst(pEst),
      _keepAlive(true)
{
    pthread_mutex_init(&_schedulerMutex, NULL);
    pthread_cond_init(&_availableJobsCV, NULL);
    // Create keepalive thread
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    int rc = pthread_create(&_keepAliveThread,
                            &attr,
                            KeepAliveThread,
                            (void*)this);
    if (rc) {
        cerr << "Error creating thread: " << rc << " errno: " << errno << endl;
        exit(-1);
    }
}

Scheduler::~Scheduler()
{
    _keepAlive = false;
    int rc = pthread_join(_keepAliveThread, NULL);
    if (rc) {
        cerr << "Error joining thread: " << rc << " errno: " << errno << endl;
        exit(-1);
    }
    pthread_cond_destroy(&_availableJobsCV);
    pthread_mutex_destroy(&_schedulerMutex);
}
