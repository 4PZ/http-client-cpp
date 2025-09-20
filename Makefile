CXX := g++-15
CXXFLAGS := -O3 -mcpu=native -flto -pthread -DNDEBUG -funroll-loops -ffast-math -Iinclude
LDFLAGS := -lcurl -flto

PERF_TARGET := build/performance_test

all: $(PERF_TARGET)

$(PERF_TARGET): examples/performance_test.cpp src/core/async_client.cpp src/utils/utils.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)