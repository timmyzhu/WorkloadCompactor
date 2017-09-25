/* storage_prot.x - NFSEnforcer RPC interface.
 * Interface for sending storage configuration parameters to NFSEnforcer.
 */

#ifdef IGNORE_WARNINGS
%#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

/* Storage QoS configuration parameters for a client */
struct StorageClient {
    unsigned long s_addr;
    unsigned int priority;
    double rateLimitRates<>;
    double rateLimitBursts<>;
};

typedef StorageClient StorageUpdateArgs<>;

struct StorageGetOccupancyArgs {
    unsigned long s_addr;
};

struct StorageGetOccupancyRes {
    double occupancy;
};

program STORAGE_ENFORCER_PROGRAM {
    version STORAGE_ENFORCER_V1 {
        void
        STORAGE_ENFORCER_NULL(void) = 0;

        /* Update storage QoS parameters for a set of clients */
        void
        STORAGE_ENFORCER_UPDATE(StorageUpdateArgs) = 1;

        /* Get occupancy statistics */
        StorageGetOccupancyRes
        STORAGE_ENFORCER_GET_OCCUPANCY(StorageGetOccupancyArgs) = 2;
    } = 1;
} = 8002;
