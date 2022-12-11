# CFLAGS  := -O3 -g
# LD      := g++
# LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lrt -lpthread  -lm

# ROCE_COMMON_PATH = ../common/
# INCLUDES  = -I${ROCE_COMMON_PATH}
# CFLAGS := ${CFLAGS} ${INCLUDES}
# SOURCES := $(wildcard *.c *.h ${ROCE_COMMON_PATH}*.c ${ROCE_COMMON_PATH}*.h)


# all: app 
# app: main.o  p4ml_manager.o ${ROCE_COMMON_PATH}packet.o ${ROCE_COMMON_PATH}dma_common.o ${ROCE_COMMON_PATH}window_manager.o
# 	${LD} $(CFLAGS) -o $@ $^ ${LDFLAGS}


# # Clean Target
# clean:
# 	rm *.o ../common/*.o
# 	rm app

# binary name
APP = client

# all source are stored in SRCS-y
SRCS-y := main.cc dpdk_manager.cc

# Build using pkg-config variables if possible
ifneq ($(shell pkg-config --exists libdpdk && echo 0),0)
$(error "no installation of DPDK found")
endif

all: shared
.PHONY: shared static
shared: build/$(APP)-shared
	ln -sf $(APP)-shared build/$(APP)
static: build/$(APP)-static
	ln -sf $(APP)-static build/$(APP)

PKGCONF ?= pkg-config

PC_FILE := $(shell $(PKGCONF) --path libdpdk 2>/dev/null)
CFLAGS += -g -Wall -O3 $(shell $(PKGCONF) --cflags libdpdk)
LDFLAGS_SHARED = $(shell $(PKGCONF) --libs libdpdk)
LDFLAGS_STATIC = $(shell $(PKGCONF) --static --libs libdpdk)

CFLAGS += -DALLOW_EXPERIMENTAL_API -pthread

build/$(APP)-shared: $(SRCS-y) Makefile $(PC_FILE) | build
	g++ $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_SHARED)

build/$(APP)-static: $(SRCS-y) Makefile $(PC_FILE) | build
	g++ $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_STATIC)

build:
	@mkdir -p $@

.PHONY: clean
clean:
	rm -f build/$(APP) build/$(APP)-static build/$(APP)-shared
	test -d build && rmdir -p build || true
# all:
# 	g++ -std=c++11 -g -O3 -c -o main.o main.cc
# 	g++ -std=c++11 -g -O3 -c -o p4ml_manager.o p4ml_manager.cc  -mavx
# 	g++ -std=c++11 -g -O3 -c -o ../common/HashTable.o ../common/HashTable.cc
# 	g++ -std=c++11 -g -O3 -c -o ../common/dma_common.o ../common/dma_common.cc
# 	g++ -std=c++11 -g -O3 -I../common/ -o app main.o p4ml_manager.o ../common/HashTable.o ../common/dma_common.o -lrdmacm -libverbs -lrt -lpthread  -lm 

# clean:
# 	rm *.o
# 	rm app
