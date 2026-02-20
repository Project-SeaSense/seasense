# SeaSense Deployment TODO (Boot van meneer Kurk)

Doel: one-shot deployment die blijft draaien zonder fysieke toegang.

## Status legenda
- [x] klaar
- [~] deels klaar / nog valideren
- [ ] open

## P0 — Moet vóór uitvaren

- [x] **Watchdog + self-recovery pad**
  - [x] Task watchdog op hoofdloop
  - [~] Bij vastloper: subsystem reset (I2C/WiFi/SD) → pas dan reboot

- [~] **Harde timeouts op externe operaties**
  - [x] Sensor reads timeout
  - [~] SD write timeout/foutafhandeling
  - [x] API upload timeout + niet-blokkerend gedrag

- [ ] **Storage failover bewezen (runtime test)**
  - [ ] SD eruit → blijft meten/loggen (SPIFFS fallback)
  - [ ] SD terugplaatsen → herstel zonder crash

- [ ] **Power resilience bewezen**
  - [ ] Cold boot x20 (PASS)
  - [ ] Power dip/reset x20 (PASS)

- [ ] **Offline buffering + recovery bewezen**
  - [ ] 12h zonder netwerk bufferen
  - [ ] Na reconnect gecontroleerd uploaden zonder dataverlies

- [ ] **24h soak test**
  - [ ] Geen freeze
  - [ ] Geen reset storm
  - [ ] Data blijft binnenkomen

## P1 — Sterk aanbevolen

- [x] **Health telemetry aanwezig**
  - [x] Uptime
  - [x] Reboot reason
  - [x] Queue depth / pending records
  - [x] Free heap
  - [x] Storage status
  - [x] GPS status

- [x] **Persistente fouttellers**
  - [x] SD failures
  - [x] API failures
  - [x] Sensor read failures

- [x] **Config sanity checks**
  - [x] sample interval grenzen
  - [x] sensor enable flags validatie
  - [x] API config validatie

- [ ] **Flash-marge verbeteren op standaard S3**
  - [ ] Doel: < 90% flash usage

## P2 — Nice to have

- [ ] **OTA firmware updates via Web UI**
  - [ ] Partitie switchen: `no_ota` → `min_spiffs` (eenmalig via USB)
  - [ ] Upload endpoint toevoegen aan WebServer (`Update.h` handler)
  - [ ] HTML upload form in web UI
  - [ ] Rollback-beveiliging (`esp_ota_mark_app_valid()`)
  - [ ] Firmware size bewaken (moet < ~1.5MB blijven)
  - [ ] Optioneel later: HTTP Pull OTA voor remote updates zonder WiFi-bereik
- [ ] Remote safe mode
- [ ] Dagelijkse health summary

## Open issues nu (prioriteit)

1. Exception/panic reboot-loop oorzaak isoleren (serial panic backtrace + decode)
2. Upload pad valideren op echte target (force upload + history + last_error)
3. Runtime failover test (SD/network) uitvoeren met logging

## Release administratie

- [ ] Firmware commit/hash vastgelegd
- [ ] Config snapshot (zonder secrets) opgeslagen
- [ ] Secrets geprovisioned
- [ ] Go/No-Go expliciet besloten
