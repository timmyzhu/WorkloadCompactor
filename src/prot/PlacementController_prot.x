/* PlacementController_prot.x - PlacementController RPC interface.
 * Interface for communicating with PlacementController and performing placement/admission control.
 */

#ifdef IGNORE_WARNINGS
%#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

typedef string str<>;

enum PlacementStatus {
    PLACEMENT_SUCCESS,
    PLACEMENT_ERR_MISSING_ARGUMENT,
    PLACEMENT_ERR_INVALID_ARGUMENT,
    PLACEMENT_ERR_CLIENT_VM_ALREADY_EXISTS,
    PLACEMENT_ERR_SERVER_VM_ALREADY_EXISTS,
    PLACEMENT_ERR_CLIENT_VM_NONEXISTENT,
    PLACEMENT_ERR_SERVER_VM_NONEXISTENT,
    PLACEMENT_ERR_SERVER_VM_IN_USE
};

/* Arguments for AddClients RPC */
struct PlacementAddClientsArgs {
    /* string encoded JSON of list of clients (see DNC-Library/NC.hpp) */
    str clientInfos;
    str addrPrefix;
    bool enforce;
};

/* Results for AddClients RPC */
struct PlacementAddClientsRes {
    PlacementStatus status;
    bool admitted;
    str clientHosts<>;
    str clientVMs<>;
    str serverHosts<>;
    str serverVMs<>;
};

/* Arguments for DelClients RPC */
struct PlacementDelClientsArgs {
    /* list of names of clients to delete */
    str names<>;
};

/* Results for DelClients RPC */
struct PlacementDelClientsRes {
    PlacementStatus status;
};

/* Arguments for AddClientVM RPC */
struct PlacementAddClientVMArgs {
    str clientHost;
    str clientVM;
};

/* Results for AddClientVM RPC */
struct PlacementAddClientVMRes {
    PlacementStatus status;
};

/* Arguments for DelClientVM RPC */
struct PlacementDelClientVMArgs {
    str clientHost;
    str clientVM;
};

/* Results for DelClientVM RPC */
struct PlacementDelClientVMRes {
    PlacementStatus status;
};

/* Arguments for AddServerVM RPC */
struct PlacementAddServerVMArgs {
    str serverHost;
    str serverVM;
};

/* Results for AddServerVM RPC */
struct PlacementAddServerVMRes {
    PlacementStatus status;
};

/* Arguments for DelServerVM RPC */
struct PlacementDelServerVMArgs {
    str serverHost;
    str serverVM;
};

/* Results for DelServerVM RPC */
struct PlacementDelServerVMRes {
    PlacementStatus status;
};

/* PlacementController RPC interface */
program PLACEMENT_CONTROLLER_PROGRAM {
    version PLACEMENT_CONTROLLER_V1 {
        void
        PLACEMENT_CONTROLLER_NULL(void) = 0;

        /* Determine placement/admission control for a set of clients */
        PlacementAddClientsRes
        PLACEMENT_CONTROLLER_ADD_CLIENTS(PlacementAddClientsArgs) = 1;

        /* Delete a set of clients */
        PlacementDelClientsRes
        PLACEMENT_CONTROLLER_DEL_CLIENTS(PlacementDelClientsArgs) = 2;

        /* Add a client VM */
        PlacementAddClientVMRes
        PLACEMENT_CONTROLLER_ADD_CLIENT_VM(PlacementAddClientVMArgs) = 3;

        /* Delete a client VM */
        PlacementDelClientVMRes
        PLACEMENT_CONTROLLER_DEL_CLIENT_VM(PlacementDelClientVMArgs) = 4;

        /* Add a server VM */
        PlacementAddServerVMRes
        PLACEMENT_CONTROLLER_ADD_SERVER_VM(PlacementAddServerVMArgs) = 5;

        /* Delete a server VM */
        PlacementDelServerVMRes
        PLACEMENT_CONTROLLER_DEL_SERVER_VM(PlacementDelServerVMArgs) = 6;
    } = 1;
} = 8004;
