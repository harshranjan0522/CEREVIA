# ---------------------------------------------------------------------------
# Plain-make build for the CEREVIA backend.
#
# CMake is the primary build (see CMakeLists.txt), but it is not installed
# everywhere; this Makefile needs nothing but a C/C++ toolchain so `./cerevia`
# can always get the server built.
# ---------------------------------------------------------------------------

CXX      ?= c++
CC       ?= cc
BUILD    ?= build
TARGET   := $(BUILD)/backend

CXXFLAGS += -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -I. -Ilibs/json -Ilibs/json/sqlite
CFLAGS   += -O2 -Ilibs/json/sqlite -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -pthread -ldl
endif
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -pthread
endif
ifneq (,$(findstring MINGW,$(UNAME_S)))
    LDFLAGS += -lws2_32
endif
ifneq (,$(findstring MSYS,$(UNAME_S)))
    LDFLAGS += -lws2_32
endif
ifeq ($(UNAME_S),Windows)
    LDFLAGS += -lws2_32
endif

SOURCES := main.cpp ApiServer.cpp Http.cpp Database.cpp MoodTracker.cpp JournalManager.cpp Encryption.cpp Paths.cpp
OBJECTS := $(addprefix $(BUILD)/,$(SOURCES:.cpp=.o))
SQLITE_OBJ := $(BUILD)/sqlite3.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS) $(SQLITE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SQLITE_OBJ): libs/json/sqlite/sqlite3.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@rm -rf $(BUILD)
