/**
 * OTAManager state machine and version parsing tests
 *
 * Tests: state transitions, size validation, progress calculation,
 * version tag parsing, and version comparison logic.
 */

#define private public  // Access private members for state inspection

#include "../src/ota/OTAManager.h"

#undef private

#include <cstdio>
#include <cstdlib>

// ============================================================================
// Test harness (same pattern as other tests)
// ============================================================================

static int g_passed = 0, g_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("\033[31m  [FAIL] \033[0m%s: %s (line %d)\n", __func__, msg, __LINE__); \
        g_failed++; return; \
    } \
} while(0)

#define PASS() do { \
    printf("\033[32m  [PASS] \033[0m%s\n", __func__); \
    g_passed++; \
} while(0)

// ============================================================================
// Tests
// ============================================================================

void test_initial_state_idle() {
    OTAManager ota;
    ASSERT(ota.getState() == OTAManager::State::IDLE, "should start IDLE");
    ASSERT(ota.getProgress() == 0, "progress should be 0");
    ASSERT(ota.getErrorMessage().isEmpty(), "no error message");
    PASS();
}

void test_begin_sets_receiving() {
    OTAManager ota;
    bool ok = ota.begin(100000);
    ASSERT(ok, "begin() should succeed");
    ASSERT(ota.getState() == OTAManager::State::RECEIVING, "state should be RECEIVING");
    ASSERT(ota.getProgress() == 0, "progress should still be 0");
    PASS();
}

void test_begin_rejects_oversized() {
    OTAManager ota;
    size_t maxSize = ota.getMaxFirmwareSize();
    bool ok = ota.begin(maxSize + 1);
    ASSERT(!ok, "begin() should reject oversized firmware");
    ASSERT(ota.getState() == OTAManager::State::ERROR, "state should be ERROR");
    ASSERT(ota.getErrorMessage().indexOf("too large") >= 0, "error message should mention 'too large'");
    PASS();
}

void test_progress_calculation() {
    OTAManager ota;
    ota.begin(1000);

    uint8_t data[250];
    memset(data, 0, sizeof(data));

    ota.writeChunk(data, 250);
    ASSERT(ota.getProgress() == 25, "should be 25% after 250/1000 bytes");

    ota.writeChunk(data, 250);
    ASSERT(ota.getProgress() == 50, "should be 50% after 500/1000 bytes");

    ota.writeChunk(data, 250);
    ASSERT(ota.getProgress() == 75, "should be 75% after 750/1000 bytes");

    ota.writeChunk(data, 250);
    ASSERT(ota.getProgress() == 100, "should be 100% after 1000/1000 bytes");
    PASS();
}

void test_end_sets_success() {
    OTAManager ota;
    ota.begin(100);
    uint8_t data[100];
    memset(data, 0, sizeof(data));
    ota.writeChunk(data, 100);
    bool ok = ota.end();
    ASSERT(ok, "end() should succeed");
    ASSERT(ota.getState() == OTAManager::State::SUCCESS, "state should be SUCCESS");
    ASSERT(ota.getProgress() == 100, "progress should be 100");
    PASS();
}

void test_end_fails_if_not_receiving() {
    OTAManager ota;
    bool ok = ota.end();
    ASSERT(!ok, "end() should fail when not in RECEIVING state");
    ASSERT(ota.getState() == OTAManager::State::IDLE, "state should stay IDLE");
    PASS();
}

void test_abort_resets_to_idle() {
    OTAManager ota;
    ota.begin(1000);
    ASSERT(ota.getState() == OTAManager::State::RECEIVING, "should be RECEIVING after begin");
    ota.abort();
    ASSERT(ota.getState() == OTAManager::State::IDLE, "should be IDLE after abort");
    ASSERT(ota.getProgress() == 0, "progress should be 0 after abort");
    ASSERT(ota.getErrorMessage().isEmpty(), "error should be cleared after abort");
    PASS();
}

void test_parse_version_from_tag() {
    String result = OTAManager::parseVersionFromTag("fw-abc1234");
    ASSERT(result == "abc1234", "should strip fw- prefix");
    PASS();
}

void test_parse_version_edge_cases() {
    // Empty tag
    String r1 = OTAManager::parseVersionFromTag("");
    ASSERT(r1 == "", "empty tag should return empty string");

    // No fw- prefix
    String r2 = OTAManager::parseVersionFromTag("v1.0.0");
    ASSERT(r2 == "v1.0.0", "non-prefixed tag should be returned as-is");

    // Just "fw-" with no version
    String r3 = OTAManager::parseVersionFromTag("fw-");
    ASSERT(r3 == "", "fw- only should return empty string");

    // Tag with fw- in middle (not prefix)
    String r4 = OTAManager::parseVersionFromTag("release-fw-123");
    ASSERT(r4 == "release-fw-123", "fw- not as prefix should return as-is");

    PASS();
}

void test_version_comparison() {
    // Same version = no update
    OTAManager ota;
    // We can't test checkForUpdate without HTTP, but we test the version parsing
    // logic that drives the comparison
    String current = "abc1234";
    String tag = "fw-abc1234";
    String remote = OTAManager::parseVersionFromTag(tag);
    ASSERT(remote == current, "same version should match");

    // Different version = update available
    String tag2 = "fw-def5678";
    String remote2 = OTAManager::parseVersionFromTag(tag2);
    ASSERT(remote2 != current, "different version should not match");

    PASS();
}

void test_max_firmware_size() {
    OTAManager ota;
    size_t maxSize = ota.getMaxFirmwareSize();
    // In native test mode, should return 0x1E0000 = 1966080 (1.875 MB)
    ASSERT(maxSize == 1966080, "should return partition size (1.875 MB)");
    PASS();
}

void test_write_chunk_fails_if_not_receiving() {
    OTAManager ota;
    uint8_t data[10] = {};
    bool ok = ota.writeChunk(data, 10);
    ASSERT(!ok, "writeChunk should fail when not RECEIVING");
    PASS();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n=== OTAManager Tests ===\n\n");

    test_initial_state_idle();
    test_begin_sets_receiving();
    test_begin_rejects_oversized();
    test_progress_calculation();
    test_end_sets_success();
    test_end_fails_if_not_receiving();
    test_abort_resets_to_idle();
    test_parse_version_from_tag();
    test_parse_version_edge_cases();
    test_version_comparison();
    test_max_firmware_size();
    test_write_chunk_fails_if_not_receiving();

    printf("\n  Results: %d passed, %d failed\n\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
