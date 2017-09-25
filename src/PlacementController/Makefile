TARGET = PlacementController
OBJS += ../prot/PlacementController_prot_xdr.o
OBJS += ../prot/AdmissionController_prot_xdr.o
OBJS += ../prot/AdmissionController_prot_clnt.o
OBJS += ../prot/AdmissionController_clnt.o
OBJS += PlacementController.o
OBJS += ../json/jsoncpp.o
OBJS += ../Estimator/Estimator.o
OBJS += ../Estimator/NetworkEstimator.o
OBJS += ../Estimator/StorageSSDEstimator.o
OBJS += ../TraceCommon/TraceReader.o
OBJS += ../TraceCommon/ProcessedTrace.o
OBJS += ../DNC-Library/NC.o
OBJS += ../DNC-Library/NCConfig.o
OBJS += ../DNC-Library/DNC.o
OBJS += ../DNC-Library/WorkloadCompactor.o
OBJS += ../DNC-Library/SolverGLPK.o
LIBS += -lm
LIBS += -lpthread
ifeq ($(OS),Windows_NT)
	LIBS += -lglpk_cof
else
	LIBS += -lglpk_elf
endif

include ../common/Makefile.template
include ../prot/Makefile.template
