# SeaSense Native Unit Tests
# Run: make test

CXX      = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -DNATIVE_TEST -g

# Include paths: mocks FIRST (shadow real Arduino/ESP headers), then project
SRCDIR   = ..
MOCKDIR  = mocks
AJSON    = $(HOME)/Documents/Arduino/libraries/ArduinoJson/src

INCLUDES = -I$(MOCKDIR) -I$(SRCDIR) -I$(AJSON)

# Output directory
BUILDDIR = build

# Test binaries
TESTS = $(BUILDDIR)/test_config_clamp \
        $(BUILDDIR)/test_system_health \
        $(BUILDDIR)/test_csv_roundtrip \
        $(BUILDDIR)/test_millis_to_utc \
        $(BUILDDIR)/test_millis_rollover \
        $(BUILDDIR)/test_upload_timing \
        $(BUILDDIR)/test_metadata_batching \
        $(BUILDDIR)/test_gps_nan_guard

.PHONY: all test clean

all: $(TESTS)

test: $(TESTS)
	@echo ""
	@failed=0; \
	for t in $(TESTS); do \
		$$t || failed=1; \
	done; \
	echo ""; \
	if [ $$failed -eq 0 ]; then \
		echo "\033[32m=== All test suites passed ===\033[0m"; \
	else \
		echo "\033[31m=== Some tests failed ===\033[0m"; \
		exit 1; \
	fi

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# ConfigManager clamp tests
# Only compile clampConfig + setters (skip SPIFFS file I/O by not compiling loadFromFile/saveToFile)
$(BUILDDIR)/test_config_clamp: test_config_clamp.cpp $(SRCDIR)/src/config/ConfigManager.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# SystemHealth tests
$(BUILDDIR)/test_system_health: test_system_health.cpp $(SRCDIR)/src/system/SystemHealth.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# CSV round-trip tests (SPIFFSStorage parseCSVLine/recordToCSV)
$(BUILDDIR)/test_csv_roundtrip: test_csv_roundtrip.cpp $(SRCDIR)/src/storage/SPIFFSStorage.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# millisToUTC tests (standalone — logic extracted to avoid APIUploader dependency chain)
$(BUILDDIR)/test_millis_to_utc: test_millis_to_utc.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $<

# millis() rollover safety tests (standalone — pure unsigned arithmetic)
$(BUILDDIR)/test_millis_rollover: test_millis_rollover.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $<

# APIUploader timing tests (standalone — extracted timing state machine)
$(BUILDDIR)/test_upload_timing: test_upload_timing.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $<

# SPIFFSStorage metadata batching tests
$(BUILDDIR)/test_metadata_batching: test_metadata_batching.cpp $(SRCDIR)/src/storage/SPIFFSStorage.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# GPS NaN guard tests (standalone — extracted filtering predicate)
$(BUILDDIR)/test_gps_nan_guard: test_gps_nan_guard.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $<

clean:
	rm -rf $(BUILDDIR)
