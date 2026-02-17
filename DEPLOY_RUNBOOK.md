# SeaSense Deploy Runbook (Dom afvinken)

Doel: zonder twijfel bepalen of deze firmware veilig op de boot van meneer Kurk kan.

**Regel 1:** Als een MUST test faalt => **STOP**. Eerst fixen, dan opnieuw testen.

---

## 0) Voorbereiding (1x)

- [ ] Laptop + USB kabel klaar
- [ ] Device voeding stabiel (zoals op boot, liefst vergelijkbaar)
- [ ] SD kaart geplaatst
- [ ] Sensoren aangesloten (minimaal temp + EC + GPS)
- [ ] Repo up-to-date (`git pull`)

Notities:
- Datum: __________
- Tester: __________
- Device ID: __________

---

## 1) Release freeze (MUST)

- [ ] Kies 1 commit als release kandidaat
- [ ] Noteer commit hash: `____________________`
- [ ] Noteer build target: `s3` of `s3-octal` => `____________`
- [ ] Noteer N2K status: `FEATURE_NMEA2000=0/1` => `____________`
- [ ] Geen nieuwe codewijzigingen meer tijdens deze testronde

---

## 2) Build + Flash (MUST)

Gebruik exact 1 van deze:

### Optie A — ESP32-S3 standaard (met huge_app)
```bash
cd SeaSenseLogger
./scripts/build.sh s3
```

### Optie B — ESP32-S3 octal
```bash
cd SeaSenseLogger
./scripts/build.sh s3-octal
```

Als N2K aan moet:
```bash
ENABLE_N2K=1 ./scripts/build.sh s3-octal
```

Aftekenen:
- [ ] Build geslaagd
- [ ] Flash/upload geslaagd
- [ ] Device boot zonder crash
- [ ] Web UI bereikbaar
- [ ] Sensor logging start zichtbaar

Boot log opmerking: ____________________________________________

---

## 3) Smoke test (MUST, 10 minuten)

- [ ] Uptime loopt op
- [ ] Geen reboot-loop
- [ ] SD mounted
- [ ] SPIFFS mounted
- [ ] GPS status logisch (fix of searching)
- [ ] Minstens 1 meetcyclus afgerond
- [ ] Minstens 1 record opgeslagen

Resultaat: PASS / FAIL
Opmerking: ____________________________________________

---

## 4) Cold boot test x20 (MUST)

**Procedure:**
1. Volledig uit
2. 5 sec wachten
3. Aan
4. Wachten tot “Ready” / normale meetloop

Aftekenen:
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

Criteria PASS:
- [ ] Alle 20 boots komen terug zonder handmatige recovery

---

## 5) Power dip/reset test x20 (MUST)

**Procedure:** korte onderbreking simuleren en herstel.

Aftekenen:
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

Criteria PASS:
- [ ] Geen corruptie
- [ ] Unit herstelt zelfstandig

---

## 6) SD failover test (MUST)

**Procedure:** tijdens runtime SD tijdelijk verwijderen en terugplaatsen.

Aftekenen:
- [ ] SD verwijderd tijdens actieve meetloop
- [ ] Device blijft draaien (geen freeze)
- [ ] Fallback gedrag zichtbaar (SPIFFS / foutmelding maar doorgaan)
- [ ] SD teruggeplaatst
- [ ] Logging herstelt

Resultaat: PASS / FAIL
Opmerking: ____________________________________________

---

## 7) Offline buffering + reconnect (MUST)

**Procedure:** netwerk uit, 12 uur laten loggen, netwerk terug.

Aftekenen:
- [ ] 12 uur offline gelogd
- [ ] Geen crash/freeze in offline periode
- [ ] Na reconnect upload komt op gang
- [ ] Geen duidelijk dataverlies

Resultaat: PASS / FAIL
Opmerking: ____________________________________________

---

## 8) GPS source test (MUST)

- [ ] Start zonder GPS fix (of zonder GPS data)
- [ ] Device blijft normaal meten/loggen
- [ ] Daarna GPS fix aanwezig
- [ ] Timestamps/positie worden correct gevuld

Resultaat: PASS / FAIL
Opmerking: ____________________________________________

---

## 9) 24h soak test (MUST)

**Procedure:** 24 uur continue run met normale configuratie.

Aftekenen:
- [ ] 24h gehaald
- [ ] Geen freeze
- [ ] Geen reset storm
- [ ] Data blijft binnenkomen
- [ ] API/upload gedrag blijft stabiel

Resultaat: PASS / FAIL
Opmerking: ____________________________________________

---

## 10) API failure/backoff (SHOULD)

- [ ] API tijdelijk onbereikbaar gemaakt
- [ ] Device blijft meten/loggen (non-blocking)
- [ ] Retries/backoff zichtbaar
- [ ] Herstel na API terug online

Resultaat: PASS / FAIL

---

## 11) Sensor fault injectie (SHOULD)

- [ ] 1 sensor tijdelijk los of fout simuleren
- [ ] Device degradeert netjes
- [ ] Geen totale crash
- [ ] Herstel na reconnect

Resultaat: PASS / FAIL

---

## 12) Release administratie (MUST)

- [ ] Commit hash vastgelegd
- [ ] Build target + flags vastgelegd
- [ ] Config snapshot opgeslagen (zonder secrets)
- [ ] Secrets geprovisioned op device
- [ ] Testlog opgeslagen

Commit hash: ____________________
Build target: ____________________
Flags (N2K etc): ____________________

---

## 13) GO / NO-GO besluit (MUST)

- [ ] Alle MUST tests PASS

Besluit:
- [ ] **GO** (deploy op boot)
- [ ] **NO-GO** (eerst fixen)

Reden / notities:
____________________________________________________________
____________________________________________________________

Naam + datum:
____________________________________________________________
