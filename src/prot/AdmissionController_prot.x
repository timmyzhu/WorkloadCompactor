/* AdmissionController_prot.x - AdmissionController RPC interface.
 * Interface for communicating with AdmissionController and performing admission control.
 */

#ifdef IGNORE_WARNINGS
%#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

enum AdmissionStatus {
    ADMISSION_SUCCESS,
    ADMISSION_ERR_MISSING_ARGUMENT,
    ADMISSION_ERR_INVALID_ARGUMENT,
    ADMISSION_ERR_FLOW_NAME_IN_USE,
    ADMISSION_ERR_CLIENT_NAME_IN_USE,
    ADMISSION_ERR_QUEUE_NAME_IN_USE,
    ADMISSION_ERR_FLOW_NAME_NONEXISTENT,
    ADMISSION_ERR_CLIENT_NAME_NONEXISTENT,
    ADMISSION_ERR_QUEUE_NAME_NONEXISTENT,
    ADMISSION_ERR_QUEUE_HAS_ACTIVE_FLOWS
};

/* Arguments for AddClients RPC */
struct AdmissionAddClientsArgs {
    /* string encoded JSON of list of clients (see DNC-Library/NC.hpp) */
    string clientInfos<>;
    /* return quickly if client is unlikely to fit */
    bool fastFirstFit;
};

/* Results for AddClients RPC */
struct AdmissionAddClientsRes {
    AdmissionStatus status;
    bool admitted;
};

/* Arguments for DelClient RPC */
struct AdmissionDelClientArgs {
    /* name of client to delete */
    string name<>;
};

/* Results for DelClient RPC */
struct AdmissionDelClientRes {
    AdmissionStatus status;
};

/* Arguments for AddQueue RPC */
struct AdmissionAddQueueArgs {
    /* string encoded JSON of a queue (see DNC-Library/NC.hpp) */
    string queueInfo<>;
};

/* Results for AddQueue RPC */
struct AdmissionAddQueueRes {
    AdmissionStatus status;
};

/* Arguments for DelQueue RPC */
struct AdmissionDelQueueArgs {
    /* name of queue to delete */
    string name<>;
};

/* Results for DelQueue RPC */
struct AdmissionDelQueueRes {
    AdmissionStatus status;
};

/* AdmissionController RPC interface */
program ADMISSION_CONTROLLER_PROGRAM {
    version ADMISSION_CONTROLLER_V1 {
        void
        ADMISSION_CONTROLLER_NULL(void) = 0;

        /* Determine admission control for a set of clients */
        AdmissionAddClientsRes
        ADMISSION_CONTROLLER_ADD_CLIENTS(AdmissionAddClientsArgs) = 1;

        /* Delete a client */
        AdmissionDelClientRes
        ADMISSION_CONTROLLER_DEL_CLIENT(AdmissionDelClientArgs) = 2;

        /* Add a queue */
        AdmissionAddQueueRes
        ADMISSION_CONTROLLER_ADD_QUEUE(AdmissionAddQueueArgs) = 3;

        /* Delete a queue */
        AdmissionDelQueueRes
        ADMISSION_CONTROLLER_DEL_QUEUE(AdmissionDelQueueArgs) = 4;
    } = 1;
} = 8003;
