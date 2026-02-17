# SeaSense Deployment Runbook (Simple Checkbox Version)

Goal: decide with high confidence whether this firmware is safe to deploy on Mr. Kurk’s boat.

**Rule #1:** If any MUST test fails => **STOP**. Fix first, then restart testing.

---

## 0) Preparation (one-time)

- [ ] Laptop + USB cable ready
- [ ] Stable power supply for device (as close as possible to real boat conditions)
- [ ] SD card inserted
- [ ] Sensors connected (at least temp + EC + GPS)
- [ ] Repo up to date (`git pull`)

Notes:
- Date: __________
- Tester: __________
- Device ID: __________

---

## 1) Release freeze (MUST)

- [ ] Select one commit as the release candidate
- [ ] Record commit hash: `____________________`
- [ ] Record build target: `s3` or `s3-octal` => `____________`
- [ ] Record N2K status: `FEATURE_NMEA2000=0/1` => `____________`
- [ ] No code changes during this test round

---

## 2) Build + Flash (MUST)

Use exactly one of these:

### Option A — ESP32-S3 standard (with huge_app)
```bash
cd SeaSenseLogger
./scripts/build.sh s3
```

### Option B — ESP32-S3 octal
```bash
cd SeaSenseLogger
./scripts/build.sh s3-octal
```

If N2K must be enabled:
```bash
ENABLE_N2K=1 ./scripts/build.sh s3-octal
```

Checklist:
- [ ] Build succeeded
- [ ] Flash/upload succeeded
- [ ] Device boots without crash
- [ ] Web UI is reachable
- [ ] Sensor logging starts

Boot log note: ____________________________________________

---

## 3) Smoke test (MUST, 10 minutes)

- [ ] Uptime is increasing
- [ ] No reboot loop
- [ ] SD mounted
- [ ] SPIFFS mounted
- [ ] GPS status is reasonable (fix or searching)
- [ ] At least 1 measurement cycle completed
- [ ] At least 1 record saved

Result: PASS / FAIL
Note: ____________________________________________

---

## 4) Cold boot test x20 (MUST)

**Procedure:**
1. Full power off
2. Wait 5 seconds
3. Power on
4. Wait for “Ready” / normal measurement loop

Checklist:
- [ ] 1
- [ ] 2
- [ ] 3
- [ ] 4
- [ ] 5
- [ ] 6
- [ ] 7
- [ ] 8
- [ ] 9
- [ ] 10
- [ ] 11
- [ ] 12
- [ ] 13
- [ ] 14
- [ ] 15
- [ ] 16
- [ ] 17
- [ ] 18
- [ ] 19
- [ ] 20

PASS criteria:
- [ ] All 20 boots recover without manual intervention

---

## 5) Power dip/reset test x20 (MUST)

**Procedure:** simulate short power interruptions and recovery.

Checklist:
- [ ] 1
- [ ] 2
- [ ] 3
- [ ] 4
- [ ] 5
- [ ] 6
- [ ] 7
- [ ] 8
- [ ] 9
- [ ] 10
- [ ] 11
- [ ] 12
- [ ] 13
- [ ] 14
- [ ] 15
- [ ] 16
- [ ] 17
- [ ] 18
- [ ] 19
- [ ] 20

PASS criteria:
- [ ] No corruption
- [ ] Unit self-recovers each time

---

## 6) SD failover test (MUST)

**Procedure:** remove SD during runtime, then reinsert.

Checklist:
- [ ] SD removed during active measurement loop
- [ ] Device keeps running (no freeze)
- [ ] Fallback behavior observed (SPIFFS / graceful errors)
- [ ] SD reinserted
- [ ] Logging recovers

Result: PASS / FAIL
Note: ____________________________________________

---

## 7) Offline buffering + reconnect (MUST)

**Procedure:** disable network, let it log for 12 hours, then restore network.

Checklist:
- [ ] Logged 12h offline
- [ ] No crash/freeze during offline period
- [ ] Upload resumes after reconnect
- [ ] No obvious data loss

Result: PASS / FAIL
Note: ____________________________________________

---

## 8) GPS source test (MUST)

- [ ] Start without GPS fix (or no GPS input)
- [ ] Device keeps measuring/logging
- [ ] GPS fix appears later
- [ ] Timestamps/position fields become valid

Result: PASS / FAIL
Note: ____________________________________________

---

## 9) 24h soak test (MUST)

**Procedure:** run continuously for 24 hours under normal config.

Checklist:
- [ ] 24h completed
- [ ] No freeze
- [ ] No reset storm
- [ ] Data keeps coming in
- [ ] API/upload behavior stays stable

Result: PASS / FAIL
Note: ____________________________________________

---

## 10) API failure/backoff (SHOULD)

- [ ] API made temporarily unavailable
- [ ] Device keeps measuring/logging (non-blocking)
- [ ] Retry/backoff behavior observed
- [ ] Recovery after API is back online

Result: PASS / FAIL

---

## 11) Sensor fault injection (SHOULD)

- [ ] Temporarily disconnect or fault one sensor
- [ ] Device degrades gracefully
- [ ] No full-system crash
- [ ] Recovery after reconnect

Result: PASS / FAIL

---

## 12) Release administration (MUST)

- [ ] Commit hash recorded
- [ ] Build target + flags recorded
- [ ] Config snapshot saved (without secrets)
- [ ] Secrets provisioned on device
- [ ] Test log saved

Commit hash: ____________________
Build target: ____________________
Flags (N2K etc): ____________________

---

## 13) GO / NO-GO decision (MUST)

- [ ] All MUST tests are PASS

Decision:
- [ ] **GO** (deploy to boat)
- [ ] **NO-GO** (fix first)

Reason / notes:
____________________________________________________________
____________________________________________________________

Name + date:
____________________________________________________________
