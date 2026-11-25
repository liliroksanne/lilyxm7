CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2

all: lilyxm7_obd_tester test_obd_protocol

lilyxm7_obd_tester: lilyxm7_obd_tester.cpp ../lilyxm7/obd_protocol.h ../lilyxm7/data_types.h
	$(CXX) $(CXXFLAGS) -o $@ lilyxm7_obd_tester.cpp

test_obd_protocol: ../tests/test_obd_protocol.cpp ../lilyxm7/obd_protocol.h ../lilyxm7/data_types.h
	$(CXX) $(CXXFLAGS) -o $@ ../tests/test_obd_protocol.cpp

test: test_obd_protocol
	./test_obd_protocol

clean:
	rm -f lilyxm7_obd_tester test_obd_protocol

.PHONY: all test clean
