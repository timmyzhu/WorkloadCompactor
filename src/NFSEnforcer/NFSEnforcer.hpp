// NFSEnforcer.hpp - NFSEnforcer globals definitions.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#ifndef _NFSENFORCER_HPP
#define _NFSENFORCER_HPP

#include <pthread.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include "scheduler.hpp"

using namespace std;

extern pid_t pid;
// Scheduler
extern Scheduler* sched;
extern uint64_t startTime;
extern int maxPendingJobsPerClient;
// xprt cache
struct xprt_cache_t {
    SVCXPRT* xprt; // xprt handle
    pthread_mutex_t mutex;
    pthread_cond_t CV;
    pthread_cond_t recvJobCV;
    const struct SVCXPRT::xp_ops* xp_ops; // original xp_ops
    struct SVCXPRT::xp_ops xp_ops_modified; // modified xp_ops with our interposition
    bool ignore; // ignore fd in poll
    volatile bool* threadDestroy; // destroy signal to RPC thread
};
extern pthread_mutex_t xprt_mutex; // used in addition to xprt_cache->mutex to protect ignore flag; must not lock xprt_cache->mutex while holding xprt_mutex
extern xprt_cache_t* xprt_cache;

// Custom svc_run function.
void custom_svc_run();

// RPC dispatch functions.
void proxy_dispatch(struct svc_req* rqstp, register SVCXPRT* transp);
void proxy_dispatch_main(struct svc_req* rqstp, register SVCXPRT* transp);

#endif // _NFSENFORCER_HPP
