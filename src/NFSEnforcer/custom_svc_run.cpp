// custom_svc_run.cpp - custom svc_run function to add threading support.
// Based on glibc-2.19 with minor modifications to add threading and integrate with NFSEnforcer.cpp.
//

#include <iostream>
#include <cassert>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <sys/poll.h>
#include <errno.h>
#include "NFSEnforcer.hpp"

// From glibc-2.19 with minor modifications to compile and run in a thread
#define RQCRED_SIZE 400/* this size is excessive */
void* custom_svc_getreq_common_thread(void* ptr)
{
    volatile bool destroy = false;
    struct rpc_msg msg;
    int fd = (long)ptr;
    char cred_area[2 * MAX_AUTH_BYTES + RQCRED_SIZE];
    msg.rm_call.cb_cred.oa_base = cred_area;
    msg.rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);

    /* now receive msgs from xprtprt (support batch calls) */
    xprt_cache_t& xprt_cache_data = xprt_cache[fd];
    pthread_mutex_lock(&xprt_cache_data.mutex);
    register SVCXPRT* xprt = xprt_cache_data.xprt;
    if ((xprt != NULL) && (xprt_cache_data.threadDestroy == NULL)) {
        xprt_cache_data.threadDestroy = &destroy;
        while (!destroy) {
            do
            {
                if (SVC_RECV (xprt, &msg))
                {
                    /* now find the exported program and call it */
                    struct svc_req r;
                    enum auth_stat why;

                    r.rq_clntcred = &(cred_area[2 * MAX_AUTH_BYTES]);
                    r.rq_xprt = xprt;
                    r.rq_prog = msg.rm_call.cb_prog;
                    r.rq_vers = msg.rm_call.cb_vers;
                    r.rq_proc = msg.rm_call.cb_proc;
                    r.rq_cred = msg.rm_call.cb_cred;

                    /* first authenticate the message */
                    /* Check for null flavor and bypass these calls if possible */

                    if (msg.rm_call.cb_cred.oa_flavor == AUTH_NULL)
                    {
                        r.rq_xprt->xp_verf.oa_flavor = _null_auth.oa_flavor;
                        r.rq_xprt->xp_verf.oa_length = 0;
                    }
                    else if ((why = _authenticate (&r, &msg)) != AUTH_OK)
                    {
                        svcerr_auth (xprt, why);
                        continue;
                    }

                    if (r.rq_prog == NFS_PROGRAM) {
                        assert(r.rq_vers == NFS_V3);
                        proxy_dispatch(&r, xprt);
                    } else {
                        svcerr_noprog(xprt);
                    }
                }
                if (sched->GetNumPendingJobs(svc_getcaller(xprt)->sin_addr.s_addr) >= maxPendingJobsPerClient) {
                    pthread_cond_wait(&xprt_cache_data.recvJobCV, &xprt_cache_data.mutex);
                }
            } while (SVC_STAT(xprt) == XPRT_MOREREQS);

            // Wake main poll so it'll take this socket into account
            pthread_mutex_lock(&xprt_mutex);
            xprt_cache_data.ignore = false;
            pthread_mutex_unlock(&xprt_mutex);
            kill(pid, SIGUSR1);

            // Wait on cond var
            pthread_cond_wait(&xprt_cache_data.CV, &xprt_cache_data.mutex);
        }
    }
    pthread_mutex_unlock(&xprt_cache_data.mutex);
    return NULL;
}

// From glibc-2.19 with minor modifications to compile and add threading
void custom_svc_getreq_poll (struct pollfd *pfdp, int pollretval, int max_pollfd)
{
    if (pollretval == 0)
        return;

    register int fds_found;
    for (int i = fds_found = 0; i < max_pollfd; ++i)
    {
        register struct pollfd *p = &pfdp[i];

        if (p->fd != -1 && p->revents)
        {
            /* fd has input waiting */
            // Check if we've cached the xprt so that we can run our custom svc_getreq_common in a thread
            xprt_cache_t& xprt_cache_data = xprt_cache[p->fd];
            pthread_mutex_lock(&xprt_cache_data.mutex);
            SVCXPRT* xprt = xprt_cache_data.xprt;
            if (xprt != NULL) {
                if (p->revents & POLLNVAL) {
                    xprt_unregister(xprt);
                    pthread_mutex_unlock(&xprt_cache_data.mutex);
                } else if (SVC_STAT(xprt) == XPRT_DIED) {
                    SVC_DESTROY(xprt);
                    pthread_mutex_unlock(&xprt_cache_data.mutex);
                } else {
                    pthread_mutex_lock(&xprt_mutex);
                    assert(p->fd == xprt->xp_sock);
                    // Ignore this fd in poll until thread completes
                    assert(xprt_cache[p->fd].ignore == false);
                    xprt_cache[p->fd].ignore = true;
                    pthread_mutex_unlock(&xprt_mutex);
                    if (xprt_cache_data.threadDestroy != NULL) {
                        pthread_cond_signal(&xprt_cache_data.CV);
                    } else {
                        pthread_t thread;
                        pthread_attr_t attr;
                        pthread_attr_init(&attr);
                        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                        int rc = pthread_create(&thread,
                                                &attr,
                                                custom_svc_getreq_common_thread,
                                                (void*)(long)(p->fd));
                        if (rc) {
                            cerr << "Error creating thread: " << rc << " errno: " << errno << endl;
                            exit(-1);
                        }
                    }
                    pthread_mutex_unlock(&xprt_cache_data.mutex);
                }
            } else {
                svc_getreq_common(p->fd);
                pthread_mutex_unlock(&xprt_cache_data.mutex);
            }

            if (++fds_found >= pollretval)
                break;
        }
    }
}

// From glibc-2.19 with minor modifications to compile and add threading
void custom_svc_run (void)
{
    int i;
    struct pollfd *my_pollfd = NULL;
    int last_max_pollfd = 0;

    for (;;)
    {
        int max_pollfd = svc_max_pollfd;
        if (max_pollfd == 0 && svc_pollfd == NULL)
            break;

        if (last_max_pollfd != max_pollfd)
        {
            struct pollfd *new_pollfd
                = (struct pollfd*)realloc (my_pollfd, sizeof (struct pollfd) * max_pollfd);

            if (new_pollfd == NULL)
            {
                perror ("svc_run: - out of memory");
                break;
            }

            my_pollfd = new_pollfd;
            last_max_pollfd = max_pollfd;
        }

        pthread_mutex_lock(&xprt_mutex);
        for (i = 0; i < max_pollfd; ++i)
        {
            if (xprt_cache[svc_pollfd[i].fd].ignore) {
                my_pollfd[i].fd = -1;
            } else {
                my_pollfd[i].fd = svc_pollfd[i].fd;
            }
            my_pollfd[i].events = svc_pollfd[i].events;
            my_pollfd[i].revents = 0;
        }
        pthread_mutex_unlock(&xprt_mutex);

        switch (i = poll (my_pollfd, max_pollfd, 1))
        {
            case -1:
                if (errno == EINTR)
                    continue;
                perror ("svc_run: - poll failed");
                break;
            case 0:
                continue;
            default:
                custom_svc_getreq_poll (my_pollfd, i, max_pollfd);
                continue;
        }
        break;
    }

    free (my_pollfd);
}
