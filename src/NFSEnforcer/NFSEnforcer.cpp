// NFSEnforcer.cpp - Storage traffic enforcement.
// NFSEnforcer acts as a shim layer for NFSv3. It intercepts NFS RPCs and queues them in per-workload queues.
// It then performs schedules the RPCs while taking into account workload priorities and rate limits.
// Workloads are configured via the storage enforcer RPC interface (see prot/storage_prot.x).
// NFSEnforcer is run in the same VM that is running the NFS server.
//
// Command line parameters:
// -c configFile (required) - config file that specifies some global NFSEnforcer parameters such as the storage profile; see profile file description in README
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <sys/poll.h>
#include <errno.h>
#include <stdarg.h>
#include <json/json.h>
#include "../prot/nfs3_prot.h"
#include "../prot/storage_prot.h"
#include "../common/time.hpp"
#include "../common/common.hpp"
#include "scheduler.hpp"
#include "NFSEnforcer.hpp"

using namespace std;

pid_t pid;
// Scheduler
Scheduler* sched;
uint64_t startTime;
int maxPendingJobsPerClient = 8;
// xprt cache
pthread_mutex_t xprt_mutex = PTHREAD_MUTEX_INITIALIZER; // used in addition to xprt_cache->mutex to protect ignore flag; must not lock xprt_cache->mutex while holding xprt_mutex
xprt_cache_t* xprt_cache;

// Default timeout can be changed using clnt_control()
static struct timeval TIMEOUT = { 25, 0 };

/*
 * kept in xprt->xp_p1
 */
struct tcp_conn {
    enum xprt_stat strm_stat;
    u_long x_id;
    XDR xdrs;
    char verf_body[MAX_AUTH_BYTES];
};

/*
 * kept in xprt->xp_p2
 */
struct svcudp_data {
    u_int su_iosz;		/* byte size of send.recv buffer */
    u_long su_xid;		/* transaction id */
    XDR su_xdrs;		/* XDR handle */
    char su_verfbody[MAX_AUTH_BYTES];	/* verifier body */
    char *su_cache;		/* cached data, NULL if no cache */
};

// Assumes xprt mutex is held
u_long custom_xp_get_xid(SVCXPRT* xprt)
{
    if (xprt->xp_p2 == NULL) {
        // Assume TCP
        struct tcp_conn* cd = (struct tcp_conn*) (xprt->xp_p1);
        return cd->x_id;
    } else {
        // Assume UDP
        struct svcudp_data* su = (struct svcudp_data*) (xprt->xp_p2);
        return su->su_xid;
    }
}

// Assumes xprt mutex is held
void custom_xp_set_xid(SVCXPRT* xprt, u_long xid)
{
    if (xprt->xp_p2 == NULL) {
        // Assume TCP
        struct tcp_conn* cd = (struct tcp_conn*) (xprt->xp_p1);
        cd->x_id = xid;
    } else {
        // Assume UDP
        struct svcudp_data* su = (struct svcudp_data*) (xprt->xp_p2);
        su->su_xid = xid;
    }
}

void print_debug(unsigned long s_addr, string fmt, ...)
{
    char str[INET_ADDRSTRLEN];
    struct in_addr addr;
    addr.s_addr = s_addr;
    inet_ntop(AF_INET, &addr, str, INET_ADDRSTRLEN);
    cout << str << " [" << ConvertTimeToSeconds(GetTime() - startTime) << "] ";
    char buf[1024];
    va_list arglist;
    va_start(arglist, fmt);
    vsnprintf(buf, sizeof(buf), fmt.c_str(), arglist);
    va_end(arglist);
    cout << buf << endl;
}

// Assumes xprt mutex is held
bool InitJob(Job* pJob, rpcproc_t rq_proc, register SVCXPRT* transp)
{
    bool success = true;
    xdrproc_t _xdr_argument;
    xdrproc_t _xdr_result;
    switch (rq_proc) {
        case NULLPROC:
            svc_sendreply(transp, (xdrproc_t)xdr_void, (caddr_t)NULL);
            success = false;
            break;

        case NFSPROC3_GETATTR:
            _xdr_argument = (xdrproc_t)xdr_nfs_fh3;
            _xdr_result = (xdrproc_t)xdr_getattr3res;
            break;

        case NFSPROC3_SETATTR:
            _xdr_argument = (xdrproc_t)xdr_setattr3args;
            _xdr_result = (xdrproc_t)xdr_wccstat3;
            break;

        case NFSPROC3_LOOKUP:
            _xdr_argument = (xdrproc_t)xdr_diropargs3;
            _xdr_result = (xdrproc_t)xdr_lookup3res;
            break;

        case NFSPROC3_ACCESS:
            _xdr_argument = (xdrproc_t)xdr_access3args;
            _xdr_result = (xdrproc_t)xdr_access3res;
            break;

        case NFSPROC3_READLINK:
            _xdr_argument = (xdrproc_t)xdr_nfs_fh3;
            _xdr_result = (xdrproc_t)xdr_readlink3res;
            break;

        case NFSPROC3_READ:
            _xdr_argument = (xdrproc_t)xdr_read3args;
            _xdr_result = (xdrproc_t)xdr_read3res;
            break;

        case NFSPROC3_WRITE:
            _xdr_argument = (xdrproc_t)xdr_write3args;
            _xdr_result = (xdrproc_t)xdr_write3res;
            break;

        case NFSPROC3_CREATE:
            _xdr_argument = (xdrproc_t)xdr_create3args;
            _xdr_result = (xdrproc_t)xdr_diropres3;
            break;

        case NFSPROC3_MKDIR:
            _xdr_argument = (xdrproc_t)xdr_mkdir3args;
            _xdr_result = (xdrproc_t)xdr_diropres3;
            break;

        case NFSPROC3_SYMLINK:
            _xdr_argument = (xdrproc_t)xdr_symlink3args;
            _xdr_result = (xdrproc_t)xdr_diropres3;
            break;

        case NFSPROC3_MKNOD:
            _xdr_argument = (xdrproc_t)xdr_mknod3args;
            _xdr_result = (xdrproc_t)xdr_diropres3;
            break;

        case NFSPROC3_REMOVE:
            _xdr_argument = (xdrproc_t)xdr_diropargs3;
            _xdr_result = (xdrproc_t)xdr_wccstat3;
            break;

        case NFSPROC3_RMDIR:
            _xdr_argument = (xdrproc_t)xdr_diropargs3;
            _xdr_result = (xdrproc_t)xdr_wccstat3;
            break;

        case NFSPROC3_RENAME:
            _xdr_argument = (xdrproc_t)xdr_rename3args;
            _xdr_result = (xdrproc_t)xdr_rename3res;
            break;

        case NFSPROC3_LINK:
            _xdr_argument = (xdrproc_t)xdr_link3args;
            _xdr_result = (xdrproc_t)xdr_link3res;
            break;

        case NFSPROC3_READDIR:
            _xdr_argument = (xdrproc_t)xdr_readdir3args;
            _xdr_result = (xdrproc_t)xdr_readdir3res;
            break;

        case NFSPROC3_READDIRPLUS:
            _xdr_argument = (xdrproc_t)xdr_readdirplus3args;
            _xdr_result = (xdrproc_t)xdr_readdirplus3res;
            break;

        case NFSPROC3_FSSTAT:
            _xdr_argument = (xdrproc_t)xdr_nfs_fh3;
            _xdr_result = (xdrproc_t)xdr_fsstat3res;
            break;

        case NFSPROC3_FSINFO:
            _xdr_argument = (xdrproc_t)xdr_nfs_fh3;
            _xdr_result = (xdrproc_t)xdr_fsinfo3res;
            break;

        case NFSPROC3_PATHCONF:
            _xdr_argument = (xdrproc_t)xdr_nfs_fh3;
            _xdr_result = (xdrproc_t)xdr_pathconf3res;
            break;

        case NFSPROC3_COMMIT:
            _xdr_argument = (xdrproc_t)xdr_commit3args;
            _xdr_result = (xdrproc_t)xdr_commit3res;
            break;

        default:
            svcerr_noproc(transp);
            success = false;
            break;
    }
    if (success) {
        // Fill job parameters
        memset((char*)pJob->Argument(), 0, sizeof(pJob->argument));
        memset((char*)pJob->Result(), 0, sizeof(pJob->result));
        pJob->xdr_argument = _xdr_argument;
        pJob->xdr_result = _xdr_result;
        pJob->rq_proc = rq_proc;
        pJob->xprt = transp;
        pJob->fd = transp->xp_sock;
        pJob->xid = custom_xp_get_xid(transp);
        pJob->s_addr = svc_getcaller(transp)->sin_addr.s_addr;
        // Get arguments
        if (svc_getargs(transp, pJob->XdrArgument(), pJob->Argument())) {
            if (pJob->IsReadRequest()) {
                read3args* args = (read3args*)pJob->Argument();
                // the request size in terms of bytes : args->count()
                pJob->requestSize = args->count;
                pJob->offset = args->offset;
                pJob->file = args->file;
                pJob->immediate = false;
            } else if (pJob->IsWriteRequest()) {
                write3args* args = (write3args*)pJob->Argument();
                pJob->requestSize = args->count;
                pJob->offset = args->offset;
                pJob->file = args->file;
                pJob->immediate = false;
            } else {
                pJob->immediate = true;
            }
        } else {
            svcerr_decode(transp);
            success = false;
        }
    }
    return success;
}

void RunJob(Job* pJob)
{
    // Forward to NFS
    enum clnt_stat rpcStatus = clnt_call(pJob->RPCClient(), pJob->Proc(),
                                         pJob->XdrArgument(), pJob->Argument(),
                                         pJob->XdrResult(), pJob->Result(),
                                         TIMEOUT);
    xprt_cache_t& xprt_cache_data = xprt_cache[pJob->Fd()];
    pthread_mutex_lock(&xprt_cache_data.mutex);
    // Wake up thread reading requests
    if (sched->GetNumPendingJobs(pJob->Addr()) < maxPendingJobsPerClient) {
        pthread_cond_signal(&xprt_cache_data.recvJobCV);
    }
    // Only reply if xprt is matching
    register SVCXPRT* transp = pJob->Xprt();
    if (xprt_cache_data.xprt == transp) {
        custom_xp_set_xid(transp, pJob->Xid());
        if (rpcStatus == RPC_SUCCESS) {
            // Reply to client
            if (!svc_sendreply(transp, pJob->XdrResult(), pJob->Result())) {
                svcerr_systemerr(transp);
            }
        } else {
            clnt_perror(pJob->RPCClient(), "Failed RPC");
            svcerr_systemerr(transp);
            pthread_mutex_unlock(&xprt_cache_data.mutex);
            // Indicate that job has finished forwarding to NFS
            sched->CompleteJob(pJob);
            return;
        }
        // Free arguments
        if (!svc_freeargs(transp, pJob->XdrArgument(), pJob->Argument())) {
            cerr << "Unable to free arguments" << endl;
        }
    }
    pthread_mutex_unlock(&xprt_cache_data.mutex);

    // Indicate that job has finished forwarding to NFS
    sched->CompleteJob(pJob);

    // Free results
    if (!clnt_freeres(pJob->RPCClient(), pJob->XdrResult(), pJob->Result())) {
        cerr << "Unable to free results" << endl;
    }

    // Return RPC client to scheduler
    sched->ReturnClient(pJob);
}

bool_t custom_xp_recv (SVCXPRT* xprt, struct rpc_msg* msg)
{
    bool_t result = FALSE;
    xprt_cache_t& xprt_cache_data = xprt_cache[xprt->xp_sock];
    pthread_mutex_lock(&xprt_cache_data.mutex);
    if (xprt_cache_data.xprt == xprt) {
        result = xprt_cache_data.xp_ops->xp_recv(xprt, msg);
    }
    pthread_mutex_unlock(&xprt_cache_data.mutex);
    return result;
}

enum xprt_stat custom_xp_stat (SVCXPRT* xprt)
{
    enum xprt_stat result = XPRT_DIED;
    xprt_cache_t& xprt_cache_data = xprt_cache[xprt->xp_sock];
    pthread_mutex_lock(&xprt_cache_data.mutex);
    if (xprt_cache_data.xprt == xprt) {
        result = xprt_cache_data.xp_ops->xp_stat(xprt);
    }
    pthread_mutex_unlock(&xprt_cache_data.mutex);
    return result;
}

bool_t custom_xp_getargs (SVCXPRT* xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{
    bool_t result = FALSE;
    xprt_cache_t& xprt_cache_data = xprt_cache[xprt->xp_sock];
    pthread_mutex_lock(&xprt_cache_data.mutex);
    if (xprt_cache_data.xprt == xprt) {
        result = xprt_cache_data.xp_ops->xp_getargs(xprt, xdr_args, args_ptr);
    }
    pthread_mutex_unlock(&xprt_cache_data.mutex);
    return result;
}

bool_t custom_xp_reply (SVCXPRT* xprt, struct rpc_msg* msg)
{
    bool_t result = FALSE;
    xprt_cache_t& xprt_cache_data = xprt_cache[xprt->xp_sock];
    pthread_mutex_lock(&xprt_cache_data.mutex);
    if (xprt_cache_data.xprt == xprt) {
        result = xprt_cache_data.xp_ops->xp_reply(xprt, msg);
    }
    pthread_mutex_unlock(&xprt_cache_data.mutex);
    return result;
}

bool_t custom_xp_freeargs (SVCXPRT* xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{
    bool_t result = FALSE;
    xprt_cache_t& xprt_cache_data = xprt_cache[xprt->xp_sock];
    pthread_mutex_lock(&xprt_cache_data.mutex);
    if (xprt_cache_data.xprt == xprt) {
        result = xprt_cache_data.xp_ops->xp_freeargs(xprt, xdr_args, args_ptr);
    }
    pthread_mutex_unlock(&xprt_cache_data.mutex);
    return result;
}

void custom_xp_destroy(SVCXPRT* xprt)
{
    xprt_cache_t& xprt_cache_data = xprt_cache[xprt->xp_sock];
    pthread_mutex_lock(&xprt_cache_data.mutex);
    pthread_mutex_lock(&xprt_mutex);
    if (xprt_cache_data.xprt == xprt) {
        assert(xprt_cache_data.xp_ops != NULL);
        if (xprt_cache_data.threadDestroy != NULL) {
            // Signal RPC thread to terminate
            *xprt_cache_data.threadDestroy = true;
            pthread_cond_signal(&xprt_cache_data.CV);
        }
        // Destroy xprt
        xprt->xp_ops = xprt_cache_data.xp_ops;
        xprt_cache_data.xprt = NULL;
        xprt_cache_data.xp_ops = NULL;
        xprt_cache_data.threadDestroy = NULL;
        xprt_cache_data.ignore = false;
        pthread_mutex_unlock(&xprt_mutex);
        SVC_DESTROY(xprt);
    } else {
        pthread_mutex_unlock(&xprt_mutex);
    }
    pthread_mutex_unlock(&xprt_cache_data.mutex);
}

// Assumes xprt mutex is held
void proxy_dispatch(struct svc_req* rqstp, register SVCXPRT* transp)
{
    Job* pJob = new Job();
    if (InitJob(pJob, rqstp->rq_proc, transp)) {
        sched->SubmitJob(pJob);
    }
}

// Assumes xprt mutex is held
void proxy_dispatch_main(struct svc_req* rqstp, register SVCXPRT* transp)
{
    // Cache xprt pointer
    xprt_cache_t& xprt_cache_data = xprt_cache[transp->xp_sock];
    if (xprt_cache_data.xprt == NULL) {
        xprt_cache_data.xprt = transp;
        xprt_cache_data.xp_ops = transp->xp_ops;
        xprt_cache_data.xp_ops_modified = *(transp->xp_ops);
        xprt_cache_data.xp_ops_modified.xp_recv = custom_xp_recv;
        xprt_cache_data.xp_ops_modified.xp_stat = custom_xp_stat;
        xprt_cache_data.xp_ops_modified.xp_getargs = custom_xp_getargs;
        xprt_cache_data.xp_ops_modified.xp_reply = custom_xp_reply;
        xprt_cache_data.xp_ops_modified.xp_freeargs = custom_xp_freeargs;
        xprt_cache_data.xp_ops_modified.xp_destroy = custom_xp_destroy;
        transp->xp_ops = &(xprt_cache_data.xp_ops_modified);
    }

    proxy_dispatch(rqstp, transp);
}

void* worker_thread(void* ptr)
{
    while (true) {
        Job* pJob = sched->GetNextJob();
        // Run job
        RunJob(pJob);
        // Delete job
        delete pJob;
    }
    return NULL;
}

// Dummy signal handler to wake up poll
void dummy_signal(int signum)
{
}

// SIGTERM/SIGINT signal for cleanup
void term_signal(int signum)
{
    // Unregister NFS RPC handlers before quitting
    pmap_unset(NFS_PROGRAM, NFS_V3);
    // Unregister storage RPC handlers
    pmap_unset(STORAGE_ENFORCER_PROGRAM, STORAGE_ENFORCER_V1);
    exit(0);
}

void* storage_enforcer_update_svc(StorageUpdateArgs* argp, struct svc_req* rqstp)
{
    static char* result;
    for (unsigned int i = 0; i < argp->StorageUpdateArgs_len; i++) {
        StorageClient* client = &argp->StorageUpdateArgs_val[i];
        assert(client->rateLimitRates.rateLimitRates_len == client->rateLimitBursts.rateLimitBursts_len);
        sched->UpdateClient(client->s_addr,
                            client->priority,
                            client->rateLimitRates.rateLimitRates_len,
                            client->rateLimitRates.rateLimitRates_val,
                            client->rateLimitBursts.rateLimitBursts_val);
    }
    return (void*)&result;
}

StorageGetOccupancyRes* storage_enforcer_get_occupancy_svc(StorageGetOccupancyArgs* argp, struct svc_req* rqstp)
{
    static StorageGetOccupancyRes result;
    result.occupancy = sched->GetOccupancy(argp->s_addr);
    return &result;
}

void storage_enforcer_program(struct svc_req* rqstp, register SVCXPRT* transp)
{
    union {
        StorageUpdateArgs storage_enforcer_update_arg;
        StorageGetOccupancyArgs storage_get_occupancy_arg;
    } argument;
    char* result;
    xdrproc_t _xdr_argument, _xdr_result;
    char* (*local)(char*, struct svc_req*);

    switch (rqstp->rq_proc) {
        case STORAGE_ENFORCER_NULL:
            svc_sendreply(transp, (xdrproc_t)xdr_void, (caddr_t)NULL);
            return;

        case STORAGE_ENFORCER_UPDATE:
            _xdr_argument = (xdrproc_t)xdr_StorageUpdateArgs;
            _xdr_result = (xdrproc_t)xdr_void;
            local = (char* (*)(char*, struct svc_req*))storage_enforcer_update_svc;
            break;

        case STORAGE_ENFORCER_GET_OCCUPANCY:
            _xdr_argument = (xdrproc_t)xdr_StorageGetOccupancyArgs;
            _xdr_result = (xdrproc_t)xdr_StorageGetOccupancyRes;
            local = (char* (*)(char*, struct svc_req*))storage_enforcer_get_occupancy_svc;
            break;

        default:
            svcerr_noproc(transp);
            return;
    }
    memset((char*)&argument, 0, sizeof(argument));
    if (!svc_getargs(transp, (xdrproc_t)_xdr_argument, (caddr_t)&argument)) {
        svcerr_decode(transp);
        return;
    }
    result = (*local)((char*)&argument, rqstp);
    if (result != NULL && !svc_sendreply(transp, (xdrproc_t)_xdr_result, result)) {
        svcerr_systemerr(transp);
    }
    if (!svc_freeargs(transp, (xdrproc_t)_xdr_argument, (caddr_t)&argument)) {
        cerr << "Unable to free arguments" << endl;
    }
}

int main(int argc, char** argv)
{
    int opt = 0;
    char* configFile = NULL;
    do {
        opt = getopt(argc, argv, "c:");
        switch (opt) {
            case 'c':
                configFile = optarg;
                break;

            case -1:
                break;

            default:
                break;
        }
    } while (opt != -1);

    if (configFile == NULL) {
        cerr << "Usage: " << argv[0] << " -c configFile" << endl;
        // Unregister NFS RPC handlers before quitting
        pmap_unset(NFS_PROGRAM, NFS_V3);
        // Unregister storage RPC handlers
        pmap_unset(STORAGE_ENFORCER_PROGRAM, STORAGE_ENFORCER_V1);
        exit(1);
    }

    // Get pid
    pid = getpid();
    // Read config file
    Json::Value root;
    if (!readJson(configFile, root)) {
        return -1;
    }
    // Read configuration
    int NFS_read_MPL = root.isMember("readMPL") ? root["readMPL"].asInt() : root["MPL"].asInt();
    int NFS_write_MPL = root.isMember("writeMPL") ? root["writeMPL"].asInt() : root["MPL"].asInt();
    int maxOutstandingReadBytes;
    int maxOutstandingWriteBytes;
    startTime = GetTime();
    if (root.isMember("maxOutstandingReadBytes")) {
        maxOutstandingReadBytes = root["maxOutstandingReadBytes"].asInt();
    } else {
        maxOutstandingReadBytes = 1024 * 1024 * 1024; // large number - unconstrained
    }
    if (root.isMember("maxOutstandingWriteBytes")) {
        maxOutstandingWriteBytes = root["maxOutstandingWriteBytes"].asInt();
    } else {
        maxOutstandingWriteBytes = 1024 * 1024 * 1024; // large number - unconstrained
    }

    // Setup signal handler
    struct sigaction action;
    action.sa_handler = dummy_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGUSR1, &action, NULL);
    action.sa_handler = term_signal;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    // Ignore SIGPIPE
    action.sa_handler = SIG_IGN;
    action.sa_flags = SA_RESTART;
    sigaction(SIGPIPE, &action, NULL);

    // Create xprt cache
    int maxFds = getdtablesize();
    xprt_cache = new xprt_cache_t[maxFds];
    if (xprt_cache == NULL) {
        cerr << "Failed to create xprt cache" << endl;
        exit(1);
    }
    for (int i = 0; i < maxFds; i++) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&xprt_cache[i].mutex, &attr);
        pthread_cond_init(&xprt_cache[i].CV, NULL);
        pthread_cond_init(&xprt_cache[i].recvJobCV, NULL);
        xprt_cache[i].xprt = NULL;
        xprt_cache[i].xp_ops = NULL;
        xprt_cache[i].ignore = false;
        xprt_cache[i].threadDestroy = NULL;
    }

    // Create NFS RPC clients
    vector<CLIENT*> RPCClients;
    int numClients = NFS_read_MPL + NFS_write_MPL + 7; // 7 for backup and non-read/write requests
    for (int i = 0; i < numClients; i++) {
        // Connect to NFS server
        CLIENT* cl; // NFS RPC handle
        if ((cl = clnt_create("127.0.0.1", NFS_PROGRAM, NFS_V3, "tcp")) == NULL) {
            clnt_pcreateerror("127.0.0.1");
            exit(2);
        }
        // Use NFS enforcer's user as authentication
        cl->cl_auth = authunix_create_default();
        RPCClients.push_back(cl);
    }

    // Create storage estimator
    Estimator* pEst = Estimator::create(root);

    // Create scheduler
    sched = new Scheduler(RPCClients, maxOutstandingReadBytes, maxOutstandingWriteBytes, NFS_read_MPL, NFS_write_MPL, pEst);
    if (sched == NULL) {
        cerr << "Failed to create scheduler" << endl;
        exit(1);
    }

    // Create worker threads
    for (int i = 0; i < numClients; i++) {
        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        int rc = pthread_create(&thread,
                                &attr,
                                worker_thread,
                                (void*)NULL);
        if (rc) {
            cerr << "Error creating thread: " << rc << " errno: " << errno << endl;
            exit(-1);
        }
    }

    // Unregister NFS RPC handlers
    pmap_unset(NFS_PROGRAM, NFS_V3);

    // Replace udp RPC handlers
    register SVCXPRT *transp;
    transp = svcudp_create(RPC_ANYSOCK);
    if (transp == NULL) {
        cerr << "Failed to create udp service" << endl;
        exit(1);
    }
    if (!svc_register(transp, NFS_PROGRAM, NFS_V3, proxy_dispatch_main, IPPROTO_UDP)) {
        cerr << "Failed to register udp NFSEnforcer" << endl;
        exit(1);
    }
  
    // Replace tcp RPC handlers
    transp = svctcp_create(RPC_ANYSOCK, 0, 0);
    if (transp == NULL) {
        cerr << "Failed to create tcp service" << endl;
        exit(1);
    }
    if (!svc_register(transp, NFS_PROGRAM, NFS_V3, proxy_dispatch_main, IPPROTO_TCP)) {
        cerr << "Failed to register tcp NFSEnforcer" << endl;
        exit(1);
    }

    // Unregister storage RPC handlers
    pmap_unset(STORAGE_ENFORCER_PROGRAM, STORAGE_ENFORCER_V1);

    // Replace tcp RPC handlers
    transp = svctcp_create(RPC_ANYSOCK, 0, 0);
    if (transp == NULL) {
        cerr << "Failed to create tcp service" << endl;
        exit(1);
    }
    if (!svc_register(transp, STORAGE_ENFORCER_PROGRAM, STORAGE_ENFORCER_V1, storage_enforcer_program, IPPROTO_TCP)) {
        cerr << "Failed to register tcp storage enforcer" << endl;
        exit(1);
    }

    // Run proxy
    custom_svc_run();
    cerr << "custom_svc_run returned" << endl;

    delete sched;
    delete pEst;
    delete[] xprt_cache;

    return 1;
}
