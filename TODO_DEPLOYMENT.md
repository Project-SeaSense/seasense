# SeaSense Deployment TODO (Boot van meneer Kurk)

Doel: one-shot deployment die blijft draaien zonder fysieke toegang.

## P0 — Moet vóór uitvaren

- [ ] **Watchdog + self-recovery pad**
  - [ ] Task watchdog op hoofdloop
  - [ ] Bij vastloper: subsystem reset (I2C/WiFi/SD) → pas dan reboot

- [ ] **Harde timeouts op externe operaties**
  - [ ] Sensor reads timeout
  - [ ] SD write timeout/foutafhandeling
  - [ ] API upload timeout + niet-blokkerend gedrag

- [ ] **Storage failover bewezen**
  - [ ] Runtime test: SD eruit → blijft meten/loggen (SPIFFS fallback)
  - [ ] SD terugplaatsen → herstel zonder crash

- [ ] **Power resilience bewezen**
  - [ ] Cold boot x20 (PASS)
  - [ ] Power dip/reset x20 (PASS)

- [ ] **Offline buffering + recovery**
  - [ ] 12h zonder netwerk bufferen
  - [ ] Na reconnect gecontroleerd uploaden zonder dataverlies

- [ ] **24h soak test**
  - [ ] Geen freeze
  - [ ] Geen reset storm
  - [ ] Data blijft binnenkomen

## P1 — Sterk aanbevolen

- [ ] **Health telemetry toevoegen**
  - [ ] Uptime
  - [ ] Reboot reason
  - [ ] Queue depth
  - [ ] Free heap
  - [ ] Storage status
  - [ ] GPS status

- [ ] **Persistente fouttellers**
  - [ ] SD failures
  - [ ] API failures
  - [ ] Sensor read failures per sensor

- [ ] **Config sanity checks**
  - [ ] sample interval grenzen
  - [ ] sensor enable flags validatie
  - [ ] API config validatie

- [ ] **Flash-marge verbeteren op standaard S3**
  - [ ] Doel: < 90% flash usage

## P2 — Nice to have

- [ ] OTA dual partition + rollback
- [ ] Remote safe mode
- [ ] Dagelijkse health summary

## Test/Go-No-Go afvinken

- [ ] Cold boot x20 PASS
- [ ] Power dip/reset x20 PASS
- [ ] SD failover PASS
- [ ] 12h offline buffer/recovery PASS
- [ ] GPS absent/present overgang PASS
- [ ] 24h soak PASS
- [ ] API failure/backoff PASS
- [ ] Sensor fault injectie PASS

## Release administratie

- [ ] Firmware commit/hash vastgelegd
- [ ] Config snapshot (zonder secrets) opgeslagen
- [ ] Secrets geprovisioned
- [ ] Go/No-Go expliciet besloten
