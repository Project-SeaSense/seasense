# SeaSense Deployment TODO (Boot van meneer Kurk)

Doel: one-shot deployment die blijft draaien zonder fysieke toegang.
Laatste review: 2026-02-23

## Status legenda
- [x] klaar
- [~] deels klaar / nog valideren
- [ ] open

---

## P0 — Moet vóór uitvaren

### Code fixes (blokkeren soak test)

- [ ] **SD write timeout ontbreekt**
  - SD.open(), file.println(), file.flush() hebben geen timeout
  - Trage/falende SD kan hoofdloop blokkeren tot 30s watchdog-panic
  - Fix: millis()-timeout wrapper of SD writes naar aparte FreeRTOS task

- [ ] **Safe mode staat UIT in code**
  - `systemHealth.begin()` wordt aangeroepen met threshold=255 (disabled)
  - Moet BOOT_LOOP_THRESHOLD (5) zijn voor productie
  - Eenregelige fix in SeaSenseLogger.ino:387

- [~] **Graceful degradation sequence ontbreekt**
  - Nu: enige recovery = 30s watchdog-panic → reboot
  - Moet: sensor timeout → I2C reset → WiFi reconnect → subsystem disable → dan pas reboot
  - I2C reset bestaat (9-clock SCL recovery na 8 failures)
  - WiFi reconnect bestaat (60s interval)
  - SD subsystem reset/disable ontbreekt
  - Geen geordende escalatie

### Bestaande items (bijgewerkt)

- [x] **Watchdog + self-recovery pad**
  - [x] Task watchdog op hoofdloop (30s, panic-on-timeout)
  - [x] Boot loop detectie met NVS-persistentie (auto-clear na 120s stabiel)
  - [~] Bij vastloper: subsystem reset (I2C ja, SD/WiFi gedeeltelijk) → pas dan reboot

- [~] **Harde timeouts op externe operaties**
  - [x] Sensor reads timeout (app-level, met watchdog feed elke 500ms)
  - [ ] SD write timeout — **ONTBREEKT** (zie hierboven)
  - [x] API upload timeout + niet-blokkerend gedrag (retry backoff 1/2/5/10/30 min)
  - [~] I2C Wire.requestFrom() heeft geen timeout — kan blokkeren bij hangende sensor

- [~] **Storage failover (code bestaat, runtime test nodig)**
  - [~] SD eruit → SPIFFS vangt op (dual-write: SD+SPIFFS tegelijk, al geïmplementeerd)
  - [~] SD terugplaatsen → auto-remount elke 30s (code pad bestaat, niet getest)

- [~] **Offline buffering + recovery (code bestaat, validatie nodig)**
  - [~] SPIFFS circulaire buffer: 1000 records ≈ 62 uur bij 16 samples/uur
  - [~] Upload recovery: persistent record-count tracking, skip al-geüploade records
  - [ ] Valideren: 12h zonder netwerk → reconnect → geen dataverlies

### Hardware tests (na code fixes)

- [ ] **Power resilience bewezen**
  - [ ] Cold boot x20 (PASS)
  - [ ] Power dip/reset x20 (PASS)

- [ ] **24h soak test**
  - [ ] Geen freeze
  - [ ] Geen reset storm
  - [ ] Data blijft binnenkomen
  - [ ] Heap stabiel (geen memory leak)

---

## P1 — Sterk aanbevolen

- [x] **Health telemetry aanwezig**
  - [x] Uptime, reboot reason, queue depth, free heap, storage status, GPS status
  - [x] Meelift op elke API upload + beschikbaar via /api/status

- [x] **Persistente fouttellers (NVS)**
  - [x] SD failures, API failures, sensor read failures, WiFi failures

- [x] **Config sanity checks**
  - [x] Sample interval grenzen, sensor enable flags, API config validatie

- [x] **Flash-marge**
  - [x] Huidig: 47% van 3MB (huge_app) — ruim onder 90% doel

- [ ] **Coredump partition activeren**
  - Partitions.csv heeft al een coredump partitie (64KB)
  - ESP-IDF kan crash dumps automatisch opslaan — nog niet geconfigureerd
  - Lost het "panic backtrace decode" probleem op

---

## P2 — Nice to have

- [ ] **OTA firmware updates via Web UI**
  - Partitie-layout is al OTA-ready (ota_0 + ota_1 slots)
  - [ ] Upload endpoint met Update.h handler
  - [ ] HTML upload form in web UI
  - [ ] Rollback-beveiliging (esp_ota_mark_app_valid())
  - [ ] Firmware size bewaken (< 1.5MB)

- [~] **Remote safe mode**
  - [x] API endpoint bestaat: POST /api/system/clear-safe-mode
  - [ ] Safe mode moet eerst ENABLED worden (zie P0)

- [ ] **Dagelijkse health summary**
  - Nu: health data meelift op elke upload (interval-based)
  - Wens: 1x per dag samenvatting met min/max/gem waarden

---

## Open issues (prioriteit)

1. **SD write timeout toevoegen** — hoofdloop kan hangen op SD operaties
2. **Safe mode enablen** — threshold 255→5 in SeaSenseLogger.ino
3. **Soak test uitvoeren** — pas na bovenstaande fixes
4. **Coredump activeren** — voor crash diagnostiek op afstand
5. ~~Exception/panic reboot-loop isoleren~~ → opgelost door coredump + safe mode
6. ~~Upload pad valideren~~ → [x] force upload, history, last_error werken

## Release administratie

- [ ] Firmware commit/hash vastgelegd
- [ ] Config snapshot (zonder secrets) opgeslagen
- [ ] Secrets geprovisioned
- [ ] Go/No-Go expliciet besloten
