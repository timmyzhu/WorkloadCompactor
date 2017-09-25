TARGET = DNC-LibraryTest
OBJS += DNC-LibraryTest.o
OBJS += ../json/jsoncpp.o
OBJS += ../Estimator/Estimator.o
OBJS += ../Estimator/NetworkEstimator.o
OBJS += ../Estimator/StorageSSDEstimator.o
OBJS += ../TraceCommon/TraceReader.o
OBJS += ../TraceCommon/ProcessedTrace.o
OBJS += ../DNC-Library/NC.o
OBJS += ../DNC-Library/DNC.o
OBJS += ../DNC-Library/WorkloadCompactor.o
OBJS += ../DNC-Library/SolverGLPK.o
OBJS += TraceReaderTest.o
OBJS += NetworkEstimatorTest.o
OBJS += StorageSSDEstimatorTest.o
OBJS += ProcessedTraceTest.o
OBJS += serializeJSONTest.o
OBJS += SolverGLPKTest.o
OBJS += NCTest.o
OBJS += DNCTest.o
OBJS += WorkloadCompactorTest.o
LIBS += -lm
ifeq ($(OS),Windows_NT)
	LIBS += -lglpk_cof
else
	LIBS += -lglpk_elf
endif

include ../common/Makefile.template
