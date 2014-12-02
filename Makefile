SRC = src
INCLUDE = include
BENCH = benchmarks
PROF = prof

CFLAGS = -O0 -Wall
LDFLAGS = -lm -lrt -lpthread -lssmem
VER_FLAGS = -D_GNU_SOURCE

MEASUREMENTS = 0

ifeq ($(VERSION),DEBUG) 
CFLAGS = -O0 -ggdb -Wall -g -fno-inline
VER_FLAGS += -DDEBUG
endif

UNAME := $(shell uname -n)

ifeq ($(UNAME), lpd48core)
PLATFORM = OPTERON
CC = gcc-4.8
PLATFORM_NUMA=1
endif

ifeq ($(UNAME), lpdxeon2680)
PLATFORM = XEON2
CC = gcc
PLATFORM_NUMA=1
endif

ifeq ($(UNAME), trigonak-laptop)
PLATFORM = COREi7
CC = gcc
endif

ifeq ($(UNAME), diassrv8)
PLATFORM = XEON
CC = gcc
PLATFORM_NUMA=1
endif

ifeq ($(UNAME), maglite)
PLATFORM = NIAGARA
CC = /opt/csw/bin/gcc
CFLAGS += -m64 -mcpu=v9 -mtune=v9
endif

ifeq ($(UNAME), parsasrv1.epfl.ch)
PLATFORM = TILERA
CC = tile-gcc
LDFLAGS += -ltmc
endif

ifeq ($(UNAME), smal1.sics.se)
PLATFORM = TILERA
CC = tile-gcc
LDFLAGS += -ltmc
endif

ifeq ($(UNAME), ol-collab1)
PLATFORM = T44
CC = /usr/sfw/bin/gcc
CFLAGS += -m64
endif

ifeq ($(PLATFORM), )
PLATFORM = DEFAULT
CC = gcc
endif

ifeq ($(PLATFORM_NUMA),1) #give PLATFORM_NUMA=1 for NUMA
LDFLAGS += -lnuma
endif 

VER_FLAGS += -D$(PLATFORM)

all: libssmem.a ssmem_test

default: ssmem_test

ssmem.o: $(SRC)/ssmem.c 
	$(CC) $(VER_FLAGS) -c $(SRC)/ssmem.c $(CFLAGS) -I./$(INCLUDE)

ifeq ($(MEASUREMENTS),1)
VER_FLAGS += -DDO_TIMINGS
MEASUREMENTS_FILES += measurements.o
endif

libssmem.a: ssmem.o $(INCLUDE)/ssmem.h $(MEASUREMENTS_FILES)
	@echo Archive name = libssmem.a
	ar -r libssmem.a ssmem.o $(MEASUREMENTS_FILES)
	rm -f *.o	

ssmem_test.o: $(SRC)/ssmem_test.c libssmem.a
	$(CC) $(VER_FLAGS) -c $(SRC)/ssmem_test.c $(CFLAGS) -I./$(INCLUDE)

ssmem_test: libssmem.a ssmem_test.o
	$(CC) $(VER_FLAGS) -o ssmem_test ssmem_test.o $(CFLAGS) $(LDFLAGS) -I./$(INCLUDE) -L./ 


clean:
	rm -f *.o *.a ssmem_test
