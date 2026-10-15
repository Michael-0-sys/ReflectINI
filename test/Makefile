# Host tests (no Arduino needed). Usage:
#   make -C test                                   unit tests (ASan + UBSan), each also with the Arduino stand-in (-DARDUINO)
#   make -C test examples                          builds and runs all examples against the Arduino stand-in
#   make -C test json REFLECTJSON=../../ReflectJSON/src
#                                                  tests + example that use ReflectJSON and ReflectINI together
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -I../src
REFLECTJSON ?=

TESTS = test_error_messages test_limits test_nullable test_char_arrays test_robustness test_strict_zerocopy \
        test_roundtrip test_numformat test_numformat_int test_fuzz
EXAMPLES = $(filter-out 15_INI_JSON_SameStruct,$(notdir $(wildcard ../examples/*)))

all: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t > $$t.log 2>&1 || { cat $$t.log; exit 1; }; tail -1 $$t.log; \
	  echo "== $$t (ARDUINO)"; ./$$t-arduino > $$t.log 2>&1 || { cat $$t.log; exit 1; }; tail -1 $$t.log; done; rm -f *.log

$(TESTS): %: %.cpp $(wildcard ../src/*.h) mock/Arduino.h
	$(CXX) $(CXXFLAGS) $< -o $@
	$(CXX) $(CXXFLAGS) -DARDUINO -Imock $< -o $@-arduino

examples:
	@for e in $(EXAMPLES); do \
	  printf '#include "Arduino.h"\n#include "../examples/%s/%s.ino"\nint main(){ setup(); loop(); }\n' $$e $$e > example_$$e.cpp; \
	  $(CXX) $(CXXFLAGS) -DARDUINO -Imock example_$$e.cpp -o example_$$e && ./example_$$e > /dev/null || exit 1; \
	  echo "ok  $$e"; rm -f example_$$e.cpp example_$$e; done

json:
	@test -n "$(REFLECTJSON)" || { echo "set REFLECTJSON=<path to ReflectJSON/src> (ReflectJSON >= 1.1.0 with ReflectCore.h)"; exit 1; }
	$(CXX) $(CXXFLAGS) -I$(REFLECTJSON) test_with_json.cpp -o test_with_json && ./test_with_json
	@e=15_INI_JSON_SameStruct; \
	printf '#include "Arduino.h"\n#include "../examples/%s/%s.ino"\nint main(){ setup(); loop(); }\n' $$e $$e > example_$$e.cpp; \
	$(CXX) $(CXXFLAGS) -DARDUINO -Imock -I$(REFLECTJSON) example_$$e.cpp -o example_$$e && ./example_$$e > /dev/null && echo "ok  $$e"; \
	rm -f example_$$e.cpp example_$$e

clean:
	rm -f $(TESTS) $(addsuffix -arduino,$(TESTS)) test_with_json example_* *.log

.PHONY: all examples json clean
