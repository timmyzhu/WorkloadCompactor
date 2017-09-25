WorkloadCompactor README
========================

This document contains the documentation for the WorkloadCompactor networked storage QoS system.

To build:
---------

In the src directory, run:

`make`

Files:
------

### Topology configuration file:

Topology configuration files are used to indicate the workloads we want to place in the system along with the client VMs and storage server VMs in the system. Topology files are in the following JSON format:

* "clients": list client - list of workloads (a.k.a. clients in the code)
* "clientVMs": list clientVM - list of client VMs to run the workload
* "serverVMs": list serverVM - list of storage server VMs that will host the workload data via NFS
* "addrPrefix": string - prefix for the hostname of the clientVMs/serverVMs; assumes VM hostnames are named according to the getAddr function in src/DNC-Library/NCConfig.cpp
* "enforce": bool (optional) - if true, the NFSEnforcer and NetEnforcer enforcement modules will be configured with workload priorities and rate limits upon admission/placement in the system

Each client (a.k.a. workload) configuration in the clients list contains the following entries:

* "name": string - name of workload
* "SLO": float - workload's desired tail latency goal in seconds
* "trace": string - file path of trace file describing client behavior

Each clientVM configuration in the clientVMs list contains the following entries:

* "clientHost": string - hostname of machine that hosts the client VM
* "clientVM": string - identifier for the client VM

Each serverVM configuration in the serverVMs list contains the following entries:

* "serverHost": string - hostname of machine that hosts the server VM
* "serverVM": string - identifier for the server VM

An example topology file can be found at examples/topo-example.txt.

### Trace file:

Trace files are used to represent the burstiness and load that a workload sends to the system.
In WorkloadCompactor, trace files are in CSV format with one request per line. Each line contains 3 columns:
1. (decimal) arrival time of request in nanoseconds
2. (hex) number of bytes in request
3. (string) "DiskRead" or "DiskWrite"

### Arrival curve file:

Arrival curve files are automatically generated in the arrivalCurves directory and are a condensed representation of the behavior of a workload.
Arrival curves are an alternate form of r-b curves, which denote the tradeoff between the rate (r) and burst (b) parameter for a workload.
Note that the rate limit (r,b) parameters serve both as a workload characterization and an upper bound on the traffic a workload can send.
See the WorkloadCompactor paper for details.

Arrival curve files are generated as a result of analyzing a trace file, and the arrival curve files are simply used as a cache so that trace files do not need to be repeatedly analyzed.

### Profile file:

Since SSD storage behaves differently for read vs write and for different request sizes, we capture this behavior by building a device performance profile.
Specifically, we measure the read and write bandwidth at a range of request sizes from 512b to 256kb using the BandwidthTableGen tool:

`./src/BandwidthTableGen/BandwidthTableGen -s sizeMB -t target [-f configFilename] [-c count] [-n numThreads] [-r numReadThreads] [-w numWriteThreads]`

Command line parameters:
* -s sizeMB (required) - size of target file to read/write from
* -t target (required) - target filename to read/write from
* -f configFilename (optional) - filename to output results; results will be merged into the json config file
* -c count (optional) - number of operations to perform for each bandwidth test; defaults to 10000
* -n numThreads (optional) - number of threads to use; defaults to 32
* -r numReadThreads (optional) - number of threads to use for read bandwidth tests; defaults to numThreads
* -w numWriteThreads (optional) - number of threads to use for write bandwidth tests; defaults to numThreads

An example config file to use as input/output can be found at src/BandwidthTableGen/config.txt.
An example output config file can be found at examples/profileSSD.txt.
In addition to the bandwidth profile, both these config files contain parameters for NFSEnforcer:

* "type": string - set to "storageSSD" to indicate estimator type
* "bandwidthTable": list of read/write bandwidth for a given request size - must be present to run NFSEnforcer; can be set to anything when profiling
* "readMPL": int (optional) - max number of concurrent reads at storage device
* "writeMPL": int (optional) - max number of concurrent writes at storage device
* "maxOutstandingReadBytes": int (optional) - max total size of concurrent reads in bytes at storage device
* "maxOutstandingWriteBytes": int (optional) - max total size of concurrent writes in bytes at storage device


To run WorkloadCompactor:
-------------------------

**1. Launch enforcement modules**

*Note: This step can be skipped if WorkloadCompactor is not being deployed on a cluster and is only being used to see the placement decisions.*

On each machine's host OS, run:

`./src/NetEnforcer/NetEnforcer [-d dev] [-b maxBandwidth (in bytes per sec)] [-n numPriorities]`

Command line parameters:
* -d dev (optional) - the network device (default eth0)
* -b maxBandwidth (optional) - the machine's network bandwidth in bytes per sec (default 125000000 = 1Gbps)
* -n numPriorities (optional) - the maximum number of priorities (default 7; max 8).

On each NFS server, start the NFS daemon (e.g., `service nfs-kernel-server start`) and afterwards run:

`./src/NFSEnforcer/NFSEnforcer -c configFile`

Command line parameters:
* -c configFile (required) - config file that specifies some global NFSEnforcer parameters such as the storage profile; see profile file description above

**2. Start the WorkloadCompactor admission controller server**

Run:

`./src/AdmissionController/AdmissionController`

Multiple instances (on separate VMs) can be used with the placement controller for improved placement speed.


**3. Start the WorkloadCompactor placement controller server**

Run:

`./src/PlacementController/PlacementController -a AdmissionControllerAddr [-a AdmissionControllerAddr ...] [-f]`

Command line parameters:
* -a AdmissionControllerAddr (required) - the address of the AdmissionController server that determines if a workload can fit on a server; this command line option can be used multiple times to use multiple AdmissionController servers for performing the placement computation in parallel
* -f (optional) - enables the fast-first-fit computation optimization, which tells the AdmissionController server to return early if a placement is unlikely to fit


**4. Place workloads in the system**

Run:

`./src/PlacementClient/PlacementClient -t topoFilename -o outputFilename -s serverAddr [-e eventFilename]`

Command line parameters:
* -t topoFilename (required) - topology file that specifies the workloads and system configuration
* -o outputFilename (required) - output file to store the results of the workload placement
* -s serverAddr (required) - the address of the PlacementController server
* -e eventFilename (optional) - a file for experimentation purposes to add and remove instances of a workload from the system; see src/PlacementClient/PlacementClient.cpp for details

Some example output files are located at examples/output-example*.


**5. Run workload**

This assumes step 1 has been performed and enforcement is enabled in the topology file so that priorities and rate limits are enforced.
The result of the placement indicates which client VM should mount to which NFS server.
Once a mount has been established, the workload can be run, and it will be prioritized and rate limited accordingly.

Src directories:
----------------

### Core components

* DNC-Library - core code for WorkloadCompactor's rate limit parameter optimization and code for calculating tail latency with Deterministic Network Calculus (DNC)
* AdmissionController - WorkloadCompactor's admission controller server
* PlacementController - WorkloadCompactor's placement controller server
* PlacementClient - client for interacting with PlacementController
* NFSEnforcer - storage QoS enforcement module; intercepts NFS RPCs and prioritizes and rate limits them
* NetEnforcer - network QoS enforcement module; configures Linux Traffic Control (TC) to perform prioritization and rate limiting of network traffic

### Cross-component code

* prot - RPC protocol definitions
* common - common code
* TraceCommon - common trace processing code
* Estimator - estimates the work to perform a request; see src/Estimator/Estimator.hpp for details

### Utilities

* BandwidthTableGen - tool for building SSD storage profiles

### Test code

* DNC-LibraryTest - test code for DNC-Library

### Library headers

* json - JSON manipulation library headers
* glpk - GNU Linear Programming Kit library headers
