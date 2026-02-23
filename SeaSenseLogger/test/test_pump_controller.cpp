/**
 * PumpController state machine tests
 *
 * Tests the IDLE → FLUSHING → MEASURING → IDLE cycle,
 * safety cutoff, error recovery, pause/resume, and relay control.
 */

#define private public  // Access private members for state inspection

#include "../src/pump/PumpController.h"

// Provide linker symbol for EZOSensor static member (declared in header)
time_t EZOSensor::_systemEpoch = 0;

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
// Helper: create a PumpController with test-friendly config
// ============================================================================

static PumpController makePump() {
    PumpController pc(nullptr, nullptr);
    PumpConfig cfg;
    cfg.flushDurationMs = 5000;     // 5s flush
    cfg.measureDurationMs = 2000;   // 2s measure
    cfg.cycleIntervalMs = 60000;    // 60s cycle
    cfg.maxPumpOnTimeMs = 10000;    // 10s safety cutoff
    cfg.relayPin = 25;
    cfg.enabled = true;
    pc.setConfig(cfg);
    _mock_millis = 0;
    pc.begin();
    return pc;
}

// ============================================================================
// Tests
// ============================================================================

void test_initial_state_idle() {
    auto pc = makePump();
    ASSERT(pc.getState() == PumpState::IDLE, "should start IDLE");
    ASSERT(!pc.isRelayOn(), "relay should be off");
    PASS();
}

void test_idle_to_flushing() {
    auto pc = makePump();
    // Not enough time elapsed — should stay IDLE
    _mock_millis = 59999;
    pc.update();
    ASSERT(pc.getState() == PumpState::IDLE, "should still be IDLE before interval");

    // Cycle interval elapsed — should transition to FLUSHING
    _mock_millis = 60000;
    pc.update();
    ASSERT(pc.getState() == PumpState::FLUSHING, "should be FLUSHING after interval");
    ASSERT(pc.isRelayOn(), "relay should be ON during flush");
    PASS();
}

void test_flushing_to_measuring() {
    auto pc = makePump();
    _mock_millis = 60000;
    pc.update();  // IDLE → FLUSHING
    ASSERT(pc.getState() == PumpState::FLUSHING, "should be FLUSHING");

    // Not enough flush time
    _mock_millis = 64999;
    pc.update();
    ASSERT(pc.getState() == PumpState::FLUSHING, "still FLUSHING before duration");

    // Flush complete
    _mock_millis = 65000;
    pc.update();
    ASSERT(pc.getState() == PumpState::MEASURING, "should be MEASURING after flush");
    ASSERT(pc.isRelayOn(), "relay should stay ON during measure");
    PASS();
}

void test_measuring_to_idle() {
    auto pc = makePump();
    _mock_millis = 60000;
    pc.update();  // → FLUSHING
    _mock_millis = 65000;
    pc.update();  // → MEASURING

    // Not enough measure time
    _mock_millis = 66999;
    pc.update();
    ASSERT(pc.getState() == PumpState::MEASURING, "still MEASURING");

    // Measure complete
    _mock_millis = 67000;
    pc.update();
    ASSERT(pc.getState() == PumpState::IDLE, "should return to IDLE");
    ASSERT(!pc.isRelayOn(), "relay should be OFF in IDLE");
    PASS();
}

void test_full_cycle() {
    auto pc = makePump();

    // Cycle 1
    _mock_millis = 60000;
    pc.update();  // → FLUSHING
    ASSERT(pc.getState() == PumpState::FLUSHING, "cycle 1: FLUSHING");

    _mock_millis = 65000;
    pc.update();  // → MEASURING
    ASSERT(pc.getState() == PumpState::MEASURING, "cycle 1: MEASURING");

    _mock_millis = 67000;
    pc.update();  // → IDLE (_lastCycleTime = 67000)
    ASSERT(pc.getState() == PumpState::IDLE, "cycle 1: IDLE");

    // Cycle 2: starts at 67000 + 60000 = 127000
    _mock_millis = 127000;
    pc.update();  // → FLUSHING
    ASSERT(pc.getState() == PumpState::FLUSHING, "cycle 2: FLUSHING");

    _mock_millis = 132000;
    pc.update();  // → MEASURING
    ASSERT(pc.getState() == PumpState::MEASURING, "cycle 2: MEASURING");

    _mock_millis = 134000;
    pc.update();  // → IDLE
    ASSERT(pc.getState() == PumpState::IDLE, "cycle 2: IDLE");
    PASS();
}

void test_should_read_sensors() {
    auto pc = makePump();
    ASSERT(!pc.shouldReadSensors(), "IDLE: should not read");

    _mock_millis = 60000;
    pc.update();  // → FLUSHING
    ASSERT(!pc.shouldReadSensors(), "FLUSHING: should not read");

    _mock_millis = 65000;
    pc.update();  // → MEASURING
    ASSERT(pc.shouldReadSensors(), "MEASURING: should read");

    pc.notifyMeasurementComplete();
    ASSERT(!pc.shouldReadSensors(), "after notify: should not read again");
    PASS();
}

void test_safety_cutoff() {
    auto pc = makePump();

    // Set flush duration longer than safety cutoff to trigger it
    PumpConfig cfg = pc.getConfig();
    cfg.flushDurationMs = 15000;  // 15s > 10s maxPumpOnTimeMs
    pc.setConfig(cfg);

    _mock_millis = 60000;
    pc.update();  // → FLUSHING
    ASSERT(pc.getState() == PumpState::FLUSHING, "should be FLUSHING");

    // Exceed safety cutoff (10s after pump start)
    _mock_millis = 70001;
    pc.update();
    ASSERT(pc.getState() == PumpState::ERROR, "should be ERROR after safety cutoff");
    ASSERT(!pc.isRelayOn(), "relay should be OFF after error");
    ASSERT(pc.getLastError().length() > 0, "should have error message");
    PASS();
}

void test_error_recovery() {
    auto pc = makePump();

    // Force into error state via safety cutoff
    PumpConfig cfg = pc.getConfig();
    cfg.flushDurationMs = 15000;
    pc.setConfig(cfg);

    _mock_millis = 60000;
    pc.update();  // → FLUSHING
    _mock_millis = 70001;
    pc.update();  // → ERROR
    ASSERT(pc.getState() == PumpState::ERROR, "should be ERROR");

    // Not enough time for recovery
    _mock_millis = 70001 + 59999;
    pc.update();
    ASSERT(pc.getState() == PumpState::ERROR, "should still be ERROR");

    // Recovery after cycleIntervalMs
    _mock_millis = 70001 + 60000;
    pc.update();
    ASSERT(pc.getState() == PumpState::IDLE, "should recover to IDLE");
    ASSERT(pc.getLastError() == "", "error should be cleared");
    PASS();
}

void test_pause_resume() {
    auto pc = makePump();

    _mock_millis = 60000;
    pc.update();  // → FLUSHING
    ASSERT(pc.isRelayOn(), "relay ON before pause");

    pc.pause();
    ASSERT(pc.getState() == PumpState::PAUSED, "should be PAUSED");
    ASSERT(!pc.isRelayOn(), "relay OFF when paused");

    // update() should be no-op when paused
    _mock_millis = 120000;
    pc.update();
    ASSERT(pc.getState() == PumpState::PAUSED, "should stay PAUSED");

    pc.resume();
    ASSERT(pc.getState() == PumpState::IDLE, "should resume to IDLE");
    PASS();
}

void test_set_enabled_false() {
    auto pc = makePump();

    _mock_millis = 60000;
    pc.update();  // → FLUSHING
    ASSERT(pc.isRelayOn(), "relay ON");

    pc.setEnabled(false);
    ASSERT(pc.getState() == PumpState::PAUSED, "disabled → PAUSED");
    ASSERT(!pc.isRelayOn(), "relay OFF when disabled");
    ASSERT(!pc.isEnabled(), "isEnabled should be false");

    pc.setEnabled(true);
    ASSERT(pc.getState() == PumpState::IDLE, "re-enabled → IDLE");
    ASSERT(pc.isEnabled(), "isEnabled should be true");
    PASS();
}

void test_start_pump_manual() {
    auto pc = makePump();
    ASSERT(pc.getState() == PumpState::IDLE, "starts IDLE");

    pc.startPump();
    ASSERT(pc.getState() == PumpState::FLUSHING, "manual start → FLUSHING");
    ASSERT(pc.isRelayOn(), "relay ON after manual start");
    PASS();
}

void test_start_pump_only_from_idle() {
    auto pc = makePump();

    _mock_millis = 60000;
    pc.update();  // → FLUSHING
    PumpState before = pc.getState();

    pc.startPump();  // Should be ignored
    ASSERT(pc.getState() == before, "startPump ignored when not IDLE");
    PASS();
}

void test_stop_pump_emergency() {
    auto pc = makePump();

    _mock_millis = 60000;
    pc.update();  // → FLUSHING
    ASSERT(pc.isRelayOn(), "relay ON");

    pc.stopPump();
    ASSERT(pc.getState() == PumpState::IDLE, "emergency stop → IDLE");
    ASSERT(!pc.isRelayOn(), "relay OFF after stop");
    PASS();
}

void test_relay_off_in_idle() {
    auto pc = makePump();
    ASSERT(!pc.isRelayOn(), "relay OFF in IDLE");

    // Run a full cycle and verify relay off at end
    _mock_millis = 60000;
    pc.update();  // FLUSHING
    _mock_millis = 65000;
    pc.update();  // MEASURING
    _mock_millis = 67000;
    pc.update();  // IDLE
    ASSERT(!pc.isRelayOn(), "relay OFF after cycle completes");
    PASS();
}

void test_disabled_pump_no_updates() {
    auto pc = makePump();
    pc.setEnabled(false);

    _mock_millis = 120000;
    pc.update();
    ASSERT(pc.getState() == PumpState::PAUSED, "disabled pump stays PAUSED");
    ASSERT(!pc.isRelayOn(), "relay stays OFF");
    PASS();
}

void test_phase_remaining() {
    auto pc = makePump();
    ASSERT(pc.getPhaseRemainingMs() == 0, "IDLE: phase remaining = 0");

    _mock_millis = 60000;
    pc.update();  // → FLUSHING (5000ms duration)
    _mock_millis = 62000;  // 2s into flush
    ASSERT(pc.getPhaseRemainingMs() == 3000, "FLUSHING: 3s remaining");

    _mock_millis = 65000;
    pc.update();  // → MEASURING (2000ms duration)
    _mock_millis = 65500;  // 0.5s into measure
    ASSERT(pc.getPhaseRemainingMs() == 1500, "MEASURING: 1.5s remaining");
    PASS();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n=== PumpController State Machine ===\n");

    test_initial_state_idle();
    test_idle_to_flushing();
    test_flushing_to_measuring();
    test_measuring_to_idle();
    test_full_cycle();
    test_should_read_sensors();
    test_safety_cutoff();
    test_error_recovery();
    test_pause_resume();
    test_set_enabled_false();
    test_start_pump_manual();
    test_start_pump_only_from_idle();
    test_stop_pump_emergency();
    test_relay_off_in_idle();
    test_disabled_pump_no_updates();
    test_phase_remaining();

    printf("\n  %d passed, %d failed\n\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
