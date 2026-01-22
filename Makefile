ifeq ($(origin CXX), default) # gmake defaults this to g++ instead of c++
	CXX = c++
endif
CFLAGS ?= # -fdiagnostics-absolute-paths
CXXFLAGS ?= --std=c++17 # -fdiagnostics-absolute-paths
OPTS=-O2 -Wall
DBG_OPTS=-DVL_DEBUG -g

LIB_INCLUDES := Config.hpp $(wildcard Value*.hpp) Defs.hpp RefCount.hpp String.hpp vector_map.hpp vector_set.hpp
LIB_HEADERS  := $(LIB_INCLUDES) StringTable.hpp Path.hpp external/yaml.h
LIB_SOURCES  := Config.cpp $(wildcard Value*.cpp) StringTable.cpp Path.cpp String.cpp external/libyaml.c

LIB_DEPS    := $(LIB_HEADERS) Makefile
LIB_OBJS    := $(LIB_SOURCES:.cpp=.o)
LIB_OBJS    := $(LIB_OBJS:.c=.o)
LIB_DOBJS   := $(LIB_SOURCES:.cpp=D.o)
LIB_DOBJS   := $(LIB_DOBJS:.c=D.o)

default: config_tool

all: libconfig.a libconfigd.a config_tool config_tool_debug test_core

libconfig.a: $(LIB_OBJS)
	$(AR) rcs $@ $^

libconfigd.a: $(LIB_DOBJS)
	$(AR) rcs $@ $^

config_tool: $(LIB_DEPS) libconfig.a tool/ConfigTool.cpp tool/ArgSpec.h
	$(CXX) $(CXXFLAGS) $(OPTS) -o $@ -I. tool/ConfigTool.cpp -L. -lconfig

config_tool_debug: $(LIB_DEPS) libconfigd.a tool/ConfigTool.cpp tool/ArgSpec.h
	$(CXX) $(CXXFLAGS) $(DBG_OPTS) -o $@ -I. tool/ConfigTool.cpp -L. -lconfigd

test_core: $(wildcard Value.*) $(wildcard ValueJson.*) String.hpp tool/TestCore.cpp
	$(CXX) $(CXXFLAGS) $(OPTS) -o $@ -I. -DHL_NO_STRING_TABLE Value.cpp ValueJson.cpp tool/TestCore.cpp

# Rules

%.o: %.cpp $(LIB_DEPS)
	$(CXX) $(CXXFLAGS) $(OPTS) -c $< -o $@

%D.o: %.cpp $(LIB_DEPS)
	$(CXX) $(CXXFLAGS) $(DBG_OPTS) -c $< -o $@

%.o: %.cpp $(LIB_DEPS)
	$(CXX) $(CXXFLAGS) $(OPTS) -c $< -o $@

%D.o: %.cpp $(LIB_DEPS)
	$(CXX) $(CXXFLAGS) $(DBG_OPTS) -c $< -o $@

%.o: %.c $(LIB_DEPS)
	$(CC) $(CFLAGS) $(OPTS) -c $< -o $@

%D.o: %.c $(LIB_DEPS)
	$(CC) $(CFLAGS) $(DBG_OPTS) -c $< -o $@

# Install
PREFIX ?= /usr/local

LIB_DIR     := $(PREFIX)/lib
INCLUDE_DIR := $(PREFIX)/include/Config
BIN_DIR     := $(PREFIX)/bin

install: libconfig.a libconfigd.a config_tool
	mkdir -p $(LIB_DIR)
	cp libconfig.a libconfigd.a $(LIB_DIR)
	mkdir -p $(INCLUDE_DIR)
	cp $(LIB_INCLUDES) $(INCLUDE_DIR)
	mkdir -p $(BIN_DIR)
	cp config_tool $(BIN_DIR)

test_install:
	$(CXX) $(CXXFLAGS) $(OPTS) -o $@ -I$(INCLUDE_DIR) -L$(LIB_DIR) tool/ConfigTool.cpp -lconfig

clean:
	$(RM) -rf config_tool* test_core *.o *.a *.dSYM */*.o */*.dSYM
