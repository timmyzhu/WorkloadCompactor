DIRS += AdmissionController
DIRS += PlacementController
DIRS += PlacementClient
DIRS += NetEnforcer
DIRS += NFSEnforcer
DIRS += BandwidthTableGen
DIRS += DNC-LibraryTest
# the sets of directories to do various things in
BUILDDIRS = $(DIRS:%=build-%)
CLEANDIRS = $(DIRS:%=clean-%)

all: $(BUILDDIRS)
$(DIRS): $(BUILDDIRS)
$(BUILDDIRS):
	@$(MAKE) --no-print-directory -C $(@:build-%=%)

clean: $(CLEANDIRS)
$(CLEANDIRS): 
	@$(MAKE) --no-print-directory -C $(@:clean-%=%) clean

.PHONY: subdirs $(DIRS)
.PHONY: subdirs $(BUILDDIRS)
.PHONY: subdirs $(CLEANDIRS)
.PHONY: all clean
