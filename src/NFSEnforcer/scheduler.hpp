// scheduler.hpp - Class definitions for the storage scheduler.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef _SCHEDULER_HPP
#define _SCHEDULER_HPP

#include <list>
#include <map>
#include <vector>
#include <stdint.h>
#include <rpc/rpc.h>
#include <json/json.h>
#include "../Estimator/Estimator.hpp"
#include "../prot/nfs3_prot.h"

using namespace std;

// Represents a NFS request.
class Job
{
public:
    // NFS parameters
    union {
        nfs_fh3 nfsproc3_getattr_3_arg;
        setattr3args nfsproc3_setattr_3_arg;
        diropargs3 nfsproc3_lookup_3_arg;
        access3args nfsproc3_access_3_arg;
        nfs_fh3 nfsproc3_readlink_3_arg;
        read3args nfsproc3_read_3_arg;
        write3args nfsproc3_write_3_arg;
        create3args nfsproc3_create_3_arg;
        mkdir3args nfsproc3_mkdir_3_arg;
        symlink3args nfsproc3_symlink_3_arg;
        mknod3args nfsproc3_mknod_3_arg;
        diropargs3 nfsproc3_remove_3_arg;
        diropargs3 nfsproc3_rmdir_3_arg;
        rename3args nfsproc3_rename_3_arg;
        link3args nfsproc3_link_3_arg;
        readdir3args nfsproc3_readdir_3_arg;
        readdirplus3args nfsproc3_readdirplus_3_arg;
        nfs_fh3 nfsproc3_fsstat_3_arg;
        nfs_fh3 nfsproc3_fsinfo_3_arg;
        nfs_fh3 nfsproc3_pathconf_3_arg;
        commit3args nfsproc3_commit_3_arg;
    } argument;
    union {
        getattr3res nfsproc3_getattr_3_res;
        wccstat3 nfsproc3_setattr_3_res;
        lookup3res nfsproc3_lookup_3_res;
        access3res nfsproc3_access_3_res;
        readlink3res nfsproc3_readlink_3_res;
        read3res nfsproc3_read_3_res;
        write3res nfsproc3_write_3_res;
        diropres3 nfsproc3_create_3_res;
        diropres3 nfsproc3_mkdir_3_res;
        diropres3 nfsproc3_symlink_3_res;
        diropres3 nfsproc3_mknod_3_res;
        wccstat3 nfsproc3_remove_3_res;
        wccstat3 nfsproc3_rmdir_3_res;
        rename3res nfsproc3_rename_3_res;
        link3res nfsproc3_link_3_res;
        readdir3res nfsproc3_readdir_3_res;
        readdirplus3res nfsproc3_readdirplus_3_res;
        fsstat3res nfsproc3_fsstat_3_res;
        fsinfo3res nfsproc3_fsinfo_3_res;
        pathconf3res nfsproc3_pathconf_3_res;
        commit3res nfsproc3_commit_3_res;
    } result;
    xdrproc_t xdr_argument;
    xdrproc_t xdr_result;
    rpcproc_t rq_proc;
    SVCXPRT* xprt;
    int fd;
    u_long xid;
    unsigned long s_addr;
    bool immediate;
    // Read/write specific info
    int requestSize;
    uint64_t offset;
    nfs_fh3 file;
    // Scheduler parameters
    uint64_t arrivalTime;
    double jobSize;
    bool rateLimitObeyed;
    unsigned int priority;
    uint64_t seqNumRead;
    uint64_t seqNumWrite;
    uint64_t seqNumReadBytes;
    uint64_t seqNumWriteBytes;
    CLIENT* cl;

    inline rpcproc_t Proc() { return rq_proc; }
    inline caddr_t Argument() { return (caddr_t)&argument; }
    inline caddr_t Result() { return (caddr_t)&result; }
    inline xdrproc_t XdrArgument() { return xdr_argument; }
    inline xdrproc_t XdrResult() { return xdr_result; }
    inline SVCXPRT* Xprt() { return xprt; }
    inline int Fd() { return fd; }
    inline u_long Xid() { return xid; }
    inline unsigned long Addr() { return s_addr; }
    inline bool Immediate() { return immediate; }
    inline bool IsReadRequest() { return (Proc() == NFSPROC3_READ); }
    inline bool IsWriteRequest() { return (Proc() == NFSPROC3_WRITE); }
    inline int RequestSize() { return requestSize; }
    inline uint64_t Offset() { return offset; }
    inline nfs_fh3 File() { return file; }
    inline uint64_t ArrivalTime() { return arrivalTime; }
    inline double JobSize() { return jobSize; }
    inline CLIENT* RPCClient() { return cl; }
};

// A workload's (a.k.a. client) parameters.
typedef struct {
    list<Job*> pendingJobs;
    unsigned int priority;
    int rateLimitLength;
    double* rateLimitRates;
    double* rateLimitBursts;
    double* rateLimitTokens;
    uint64_t rateLimitUpdateTime;
    bool rateLimitObeyed;
    uint64_t occupancy;
    uint64_t lastOccupancyTime;
    uint64_t getOccupancyTime;
} Client;

// Scheduler for NFS requests that queues each workload separately and prioritizes and rate limits workloads.
class Scheduler
{
private:
    // Protects scheduler state
    pthread_mutex_t _schedulerMutex;
    // Available jobs condition variable
    pthread_cond_t _availableJobsCV;
    // RPC client pool
    vector<CLIENT*> _RPCAvailableClients;
    // Track outstanding jobs
    list<Job*> _outstandingPriorityList;
    uint64_t _seqNumRead;
    uint64_t _seqNumWrite;
    uint64_t _seqNumReadBytes;
    uint64_t _seqNumWriteBytes;
    int _outstandingReadBytes;
    int _maxOutstandingReadBytes;
    int _outstandingWriteBytes;
    int _maxOutstandingWriteBytes;
    int _outstandingJobs;
    int _maxOutstandingJobs;
    int _outstandingReadJobs;
    int _maxOutstandingReadJobs;
    int _outstandingWriteJobs;
    int _maxOutstandingWriteJobs;
    // Total number of pending jobs
    int _pendingJobCount;
    // Array of clients
    map<unsigned long, Client> _clients;
    // Storage estimator
    Estimator* _pEst;
    // Keep Alive
    pthread_t _keepAliveThread;
    bool _keepAlive;

    // Returns job size estimate.
    double EstimateJobSize(Client& c, Job* job);
    // Compares two clients to see which should be scheduled.
    // Assumes UpdateTokens has been called to update rateLimitObeyed flags.
    int CompareClient(const Client& c1, const Client& c2);
    // Get a client, possibly creating a new client.
    Client& GetClient(unsigned long s_addr);
    // Update token buckets in order to check rate limits.
    void UpdateTokens(Client& c, uint64_t now);
    // Add a job to the scheduler queue.
    void AddJob(Job* pJob);
    // Remove a job from the scheduler queue to submit it to storage.
    Job* RemoveJob(Client& c);
    // Find the best client to schedule next.
    Client& FindBestClient();
    // Try to schedule next job.
    Job* ScheduleJob();
    // Remove job from outstanding priority list.
    void RemoveOutstandingPriority(Job* pJob);

public:
    Scheduler(vector<CLIENT*> RPCClients, int maxOutstandingReadBytes, int maxOutstandingWriteBytes, int maxReadJobs, int maxWriteJobs, Estimator* pEst);
    ~Scheduler();
    // Update client parameters.
    void UpdateClient(unsigned long s_addr, unsigned int priority, int rateLimitLength, double* rateLimitRates, double* rateLimitBursts);
    // Return queue occupancy for a client since last call for the client.
    double GetOccupancy(unsigned long s_addr);
    // Return number of pending jobs for a client.
    int GetNumPendingJobs(unsigned long s_addr);
    // Submit job to scheduler queue.
    void SubmitJob(Job* pJob);
    // Get the next job to send to storage.
    Job* GetNextJob();
    // Indicate job is completed.
    void CompleteJob(Job* pJob);
    // Return NFS RPC client resources once a job completes.
    void ReturnClient(Job* pJob);
    // Keep NFS RPC clients alive via periodic NULL requests.
    bool KeepAlive();
};

#endif // _SCHEDULER_HPP
