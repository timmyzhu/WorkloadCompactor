TARGET = NFSEnforcer
OBJS += ../prot/nfs3_prot_xdr.o
OBJS += ../prot/storage_prot_xdr.o
OBJS += ../json/jsoncpp.o
OBJS += NFSEnforcer.o
OBJS += custom_svc_run.o
OBJS += scheduler.o
OBJS += ../Estimator/Estimator.o
OBJS += ../Estimator/StorageSSDEstimator.o
OBJS += ../Estimator/NetworkEstimator.o
LIBS += -lrt
LIBS += -lpthread

include ../common/Makefile.template
include ../prot/Makefile.template
