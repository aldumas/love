# Makefile for the mruby-port standalone harness.
#
#   make            build the harness (testing/mruby/love_mrb_harness)
#   make run        build, then run it against sample.rb
#   make mruby      build libmruby.a via the LÖVE build config
#   make clean      remove the harness binary
#
# Override paths if your checkout differs:
#   make MRUBY_DIR=/path/to/mruby

# --- Paths ---------------------------------------------------------------
# Directory containing this Makefile (testing/mruby).
HERE      := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
LOVE_ROOT := $(abspath $(HERE)/../..)
LOVE_SRC  := $(LOVE_ROOT)/src
MRUBY_DIR ?= $(abspath $(LOVE_ROOT)/../mruby)

MRUBY_INC  := $(MRUBY_DIR)/include
MRUBY_GEN  := $(MRUBY_DIR)/build/host/include
MRUBY_LIB  := $(MRUBY_DIR)/build/host/lib
MRUBY_A    := $(MRUBY_LIB)/libmruby.a

BIN    := $(HERE)love_mrb_harness
SCRIPT ?= $(HERE)sample.rb

# --- Toolchain -----------------------------------------------------------
CXX      ?= g++
CXXSTD   ?= c++14
CXXFLAGS ?= -std=$(CXXSTD) -O2 -Wall
INCLUDES := -I$(LOVE_SRC) -I$(MRUBY_INC) -I$(MRUBY_GEN)
LDFLAGS  := -L$(MRUBY_LIB)
LDLIBS   := -lmruby -lm

# --- Sources -------------------------------------------------------------
# Harness + runtime layer + LÖVE common code needed to link.
CORE_SRCS := \
	$(HERE)harness.cpp \
	$(HERE)delay_stub.cpp \
	$(LOVE_SRC)/common/mrb_runtime.cpp \
	$(LOVE_SRC)/common/Module.cpp \
	$(LOVE_SRC)/common/Object.cpp \
	$(LOVE_SRC)/common/types.cpp \
	$(LOVE_SRC)/common/Exception.cpp

# Ported module wrappers + their native implementations.
# Append here as you port more modules (e.g. the math module).
MODULE_SRCS := \
	$(LOVE_SRC)/modules/timer/wrap_Timer_mrb.cpp \
	$(LOVE_SRC)/modules/timer/Timer.cpp \
	$(LOVE_SRC)/modules/math/wrap_Math_mrb.cpp \
	$(LOVE_SRC)/modules/math/MathModule.cpp \
	$(LOVE_SRC)/modules/math/RandomGenerator.cpp \
	$(LOVE_SRC)/modules/math/BezierCurve.cpp \
	$(LOVE_SRC)/modules/math/Transform.cpp \
	$(LOVE_SRC)/common/Matrix.cpp \
	$(LOVE_SRC)/common/Vector.cpp \
	$(LOVE_SRC)/common/StringMap.cpp \
	$(LOVE_SRC)/libraries/noise1234/noise1234.cpp \
	$(LOVE_SRC)/libraries/noise1234/simplexnoise1234.cpp

SRCS := $(CORE_SRCS) $(MODULE_SRCS)

# --- Targets -------------------------------------------------------------
.PHONY: all run mruby clean

all: $(BIN)

$(BIN): $(SRCS) $(MRUBY_A)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) $(LDFLAGS) $(LDLIBS) -o $@

# Build libmruby.a if it's missing. Requires a host Ruby on PATH.
$(MRUBY_A):
	cd $(MRUBY_DIR) && MRUBY_CONFIG=love ruby ./minirake

mruby:
	cd $(MRUBY_DIR) && MRUBY_CONFIG=love ruby ./minirake

run: $(BIN)
	$(BIN) $(SCRIPT)

clean:
	rm -f $(BIN)
