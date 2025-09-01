# === ml-model-stresser Makefile ===
# Builds TCP/UDP C servers and provides handy run/clean/format targets.
# Works on macOS/Linux with clang or gcc.

# ---- Configurable knobs (override at CLI, e.g. `make tcp SERVER_PORT=7000`) ----
CC              ?= cc
STD             ?= c11
CWARN           ?= -Wall -Wextra -Wpedantic
OPT             ?= -O2
DEBUG           ?= -g
COMMON_CFLAGS   ?= -std=$(STD) $(CWARN) $(OPT)
# compile flags for UDP build that needs pthreads
UDP_THREAD_FLAG ?= -pthread

# These map to the #defines used in the C sources:
SERVER_PORT       ?= 6160           # both TCP/UDP servers use this
WORKER_BASE_PORT  ?= 9001           # both TCP/UDP use this
WORKER_COUNT      ?= 3              # used by TCP server
THREAD_POOL_SIZE  ?= 3              # used by UDP server
WORKER_SCRIPT     ?= src/worker/worker.py

# Propagate config as preprocessor defines:
CPPDEFS  = -DSERVER_PORT=$(SERVER_PORT) -DWORKER_BASE_PORT=$(WORKER_BASE_PORT) \
           -DWORKER_COUNT=$(WORKER_COUNT) -DTHREAD_POOL_SIZE=$(THREAD_POOL_SIZE) \
		   -DWORKER_SCRIPT=\"$(WORKER_SCRIPT)\"

# Linker libs (UDP needs pthread)
LDLIBS_TCP   ?=
LDLIBS_UDP   ?= -pthread

# ---- Paths / files ----
SRC_DIR     := src
SRV_DIR     := $(SRC_DIR)/server
BUILD_DIR   := build
BIN_DIR     := bin

TCP_SRCS    := $(SRV_DIR)/server_tcp.c $(SRV_DIR)/queue.c
UDP_SRCS    := $(SRV_DIR)/server_udp.c $(SRV_DIR)/queue.c

TCP_OBJS    := $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(TCP_SRCS)))
UDP_OBJS    := $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(UDP_SRCS)))

TCP_BIN     := $(BIN_DIR)/server_tcp
UDP_BIN     := $(BIN_DIR)/server_udp

# Map object names back to their source locations
vpath %.c $(SRV_DIR)

# ---- Default ----
.PHONY: help
help:
	@echo "Targets:"
	@echo "  tcp            Build the TCP server -> $(TCP_BIN)"
	@echo "  udp            Build the UDP server -> $(UDP_BIN)"
	@echo "  run-tcp        Build & run TCP server"
	@echo "  run-udp        Build & run UDP server"
	@echo "  client-tcp     Send a test line to the TCP server via netcat"
	@echo "  client-udp     Send a test line to the UDP server via netcat"
	@echo "  format         Run clang-format on C sources (if available)"
	@echo "  clean          Remove objects and binaries"
	@echo "  distclean      Also remove build dirs and Python venv caches"
	@echo ""
	@echo "Config (overridable): SERVER_PORT=$(SERVER_PORT) WORKER_BASE_PORT=$(WORKER_BASE_PORT) WORKER_COUNT=$(WORKER_COUNT) THREAD_POOL_SIZE=$(THREAD_POOL_SIZE)"

# Ensure directories exist
$(BUILD_DIR) $(BIN_DIR):
	@mkdir -p $@

# Pattern rule for objects (common)
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(CPPDEFS) -c $< -o $@

# UDP objects (need -pthread for compile on some platforms)
$(BUILD_DIR)/server_udp.o: server_udp.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(UDP_THREAD_FLAG) $(CPPDEFS) -c $< -o $@

# ---- Link rules ----
.PHONY: tcp udp
tcp: $(TCP_BIN)
udp: $(UDP_BIN)

$(TCP_BIN): $(TCP_OBJS) | $(BIN_DIR)
	$(CC) $(COMMON_CFLAGS) $(DEBUG) $(TCP_OBJS) $(LDLIBS_TCP) -o $@

$(UDP_BIN): $(UDP_OBJS) | $(BIN_DIR)
	$(CC) $(COMMON_CFLAGS) $(DEBUG) $(UDP_OBJS) $(LDLIBS_UDP) -o $@

# ---- Run helpers ----
.PHONY: run-tcp run-udp
run-tcp: $(TCP_BIN)
	@echo "Starting TCP server on port $(SERVER_PORT) ..."
	@echo "Note: the server will spawn Python workers (see src/worker_predictor.py or src/worker.py)."
	@$(TCP_BIN)

run-udp: $(UDP_BIN)
	@echo "Starting UDP server on port $(SERVER_PORT) ..."
	@echo "Note: the server will spawn Python workers (see src/worker_predictor.py or src/worker.py)."
	@$(UDP_BIN)

# Simple local clients (require netcat)
.PHONY: client-tcp client-udp
client-tcp:
	@echo "hello from nc" | nc 127.0.0.1 $(SERVER_PORT)

client-udp:
	@echo "hello from nc" | nc -u 127.0.0.1 $(SERVER_PORT)

# ---- Dev niceties ----
.PHONY: format
format:
	@if command -v clang-format >/dev/null 2>&1; then \
	  clang-format -i $(SRV_DIR)/*.c $(SRV_DIR)/*.h ; \
	  echo "Formatted C sources."; \
	else \
	  echo "clang-format not found; skipping."; \
	fi

.PHONY: clean distclean
clean:
	@$(RM) -r $(BUILD_DIR)/*.o 2>/dev/null || true
	@$(RM) -r $(TCP_BIN) $(UDP_BIN) 2>/dev/null || true
	@echo "Cleaned objects and binaries."

distclean: clean
	@$(RM) -r $(BUILD_DIR) $(BIN_DIR) 2>/dev/null || true
	@find . -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
	@echo "Fully cleaned."


/* =========================== cmocka unit tests from tests/unit/*.c =========================== */

CMOCKA_CFLAGS := $(shell pkg-config --cflags cmocka 2>/dev/null)
CMOCKA_LIBS   := $(shell pkg-config --libs   cmocka 2>/dev/null)
CMOCKA_LIBS   := $(if $(CMOCKA_LIBS),$(CMOCKA_LIBS),-lcmocka)

TEST_CFLAGS   := -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-deprecated-declarations
TEST_INCLUDES := -Isrc

TEST_OBJS     := src/server/queue.o

TEST_SRCS := $(wildcard tests/unit/*_cmocka.c)
TEST_BINS := $(TEST_SRCS:.c=)

.PHONY: unit-tests
unit-tests: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "Running $$t"; \
		./$$t; \
		rm -f $$t; \
		rm -rf $$t.dSYM; \
	done

# compile the server object(s) if not already
src/server/%.o: src/server/%.c src/server/%.h
	$(CC) -DUNIT_TEST -c -g -O2 -Wall -Wextra -Isrc -o $@ $<

# pattern rule: build each test binary from its .c plus the needed objs
tests/unit/%: tests/unit/%.c $(TEST_OBJS)
	$(CC) -DUNIT_TEST $(TEST_CFLAGS) $(TEST_INCLUDES) $(CMOCKA_CFLAGS) -o $@ $^ $(CMOCKA_LIBS)

.INTERMEDIATE: $(TEST_BINS)
.DELETE_ON_ERROR: