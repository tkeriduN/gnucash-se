# Design 03 – behandlingshistorik

Status: **byggt 2026-11-26** på grenen `feat/history`. Bygger på design 01 och 02.

Systemdokumentationen (BFNAR 9.2) ingår **inte**: Isak strök den 2026-11-26, eftersom programmet ska byggas
om kraftigt (serverläge) och dokumentationen då skulle göras två gånger. Den står kvar som ett ouppfyllt krav
i `../fardplan.md`.

## 1. Vad lagen kräver och vad funktionen gör

| Krav | Källa (se `../svenska-krav.md`, avsnitt A3) | Funktion i SE |
|---|---|---|
| Behandlingshistorik så att varje bokföringspost kan följas | 5 kap. 11 § BFL | Varje bokförd transaktion bär verifikationsnummer, registreringstidpunkt och bokföringstidpunkt, och rättelser hänvisar åt båda håll (design 01, 02). `gnc_se_history_text()` ställer upp dem |
| Historiken ska visa de bokföringsposter som har tillförts systemet samt registreringsdatum | BFNAR 9.16 | Samma uppställning: verifikation, datum, registrerad, bokförd, beskrivning, rättelse |
| Historiken ska beskriva förändringar i systemet som påverkar bokföringsposternas behandling, och när de infördes. Exempel: nya programversioner, ändrad momssats för automatkontering, nya konton | BFNAR 9.16 med kommentar | Händelseloggen i boken (avsnitt 2) |

## 2. Händelseloggen

Loggen ligger i boken och följer med den. Varje händelse har nummer, tidpunkt, slag, signatur (där den har
efterfrågats) och en text.

| Slag | Händelse | Signatur |
|---|---|---|
| `start` | Historiken börjar: programversion och antal konton och momstabeller just då | – |
| `lock` | En period bokfördes: datum, och per serie och period vilka nummer som delades ut samt början av sista verifikationens kontrollsumma | ja |
| `correction` | En rättelseverifikation skapades: för vilken verifikation, datum, anledning | ja |
| `settings` | Räkenskapsårets första dag, senareläggning eller standardserie ändrades, eller en serie lades till | – |
| `account` | Ett konto lades till, togs bort, eller fick nytt nummer, namn, typ eller överordnat konto | – |
| `tax-table` | En momstabell lades till, togs bort eller ändrades (namn, konto, sats) | – |
| `program` | Boken öppnades skrivbart med en annan programversion än senast | – |
| `check` | Kontrollen av verifikationsserierna hittade avvikelser (eller hittar inga längre) | – |

Historiken börjar första gången något ska loggas, alltså när boken börjar föras i SE. Då sparas kontoplanen
och momstabellerna som utgångsläge utan att loggas konto för konto.

**Konton och momstabeller** jämförs mot det läge historiken senast kände till, som sparas i boken per konto
och tabell. Jämförelsen görs
- direkt när motorn meddelar en ändring (`qof_event_register_handler()`), och
- för hela boken när den öppnas, före varje låsning och när historiken visas (`gnc_se_history_sync()`).

Den andra vägen fångar ändringar som gjorts medan händelser var avstängda (t.ex. när en hel kontoplan läses
in) eller i ett annat program. Sådana ändringar får tidpunkten då de upptäcktes, inte då de gjordes.

**Signaturen** frågas varje gång i gränssnittet (dialogerna "Bokför till och med" och rättelse) och skickas
som parameter till motorn. Händelser som följer av vanlig redigering (konton, momstabeller, inställningar)
har ingen signatur.

## 3. Lagring och skydd

```
se/history/next                      nästa händelsenummer
se/history/head                      kontrollsumma för senaste händelsen
se/history/events/<n>/time|type|signature|text|hash
se/history/account/<guid>/state|text      senast kända läge per konto
se/history/tax-table/<guid>/state|text    senast kända läge per momstabell
se/history/program                   senast använda programversion
se/history/last-check                kontrollsumma för senast loggade avvikelser
```

Händelserna är kedjade som verifikationsserierna:
`h(n) = SHA-256(h(n−1) ‖ nummer ‖ tid ‖ slag ‖ signatur ‖ text)`, med `h(0)` härledd ur bokens GUID.
`gnc_se_verify()` rapporterar en händelse som saknas eller har ändrats. Varje låsning skriver in början av
seriens senaste kontrollsumma i sin händelse, så historiken och serierna binder varandra.

Som för serierna gäller att kedjan skyddar mot misstag och mot ändringar utanför SE, inte mot den som
skriver om filen och räknar om kedjan.

## 4. Presentation

Åtgärder → Bokföring → Behandlingshistorik… visar historiken som text och kan spara den som fil:
händelserna i ordning, och därefter per serie och period varje bokförd transaktion med verifikationsnummer,
bokföringsdatum, registreringstidpunkt, bokföringstidpunkt, beskrivning och rättelsehänvisning.

## 5. Kända begränsningar

- **Systemdokumentation saknas** (se överst).
- En momstabell som byggs upp i flera steg i gränssnittet kan ge flera händelser efter varandra, en per
  mellanläge.
- Händelsetexterna skrivs på det språk programmet kör med när de skapas.
- Ändringar som upptäcks i efterhand (avsnitt 2) har upptäcktens tidpunkt.
- Loggen växer med boken. En händelse är fem små värden; tusentals händelser är inget problem, men det finns
  ingen arkivering av gamla händelser.
- Historiken visas som text. Det finns ingen sökning eller filtrering.
