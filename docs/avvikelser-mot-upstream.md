# Avvikelser mot upstream

Allt som GnuCash SE lägger till i eller ändrar i förhållande till `upstream/future`. Listan är till för
synkningarna: varje rad är en plats där en merge av `upstream/future` kan ge konflikter eller tyst ändrat
beteende.

## Principer

1. **Egna filer före ändringar i upstreams filer.** Ny funktionalitet ligger i egna kataloger (`docs/`,
   `util/se/`, och för kod en egen modul, se designskisserna). I upstreams filer görs bara små, märkta krokar.
2. **Varje krok i en upstreamfil står i tabellen nedan**, med varför den behövs och vad som händer om upstream
   ändrar koden runt den. En krok utan rad här är ett fel.
3. **Egna KVP-nycklar under ett eget prefix** (förslag: `se/`, se designskissen för lås och numrering), så
   att de aldrig krockar med upstreams nycklar och så att vanlig GnuCash bara ser okända slots den lämnar
   orörda. Upstreams befintliga nycklar återanvänds bara när det är uttryckligen beslutat och antecknat här.
4. **Ingen feature-flagga i boken** (`gnc-features`): den skulle få vanlig GnuCash att vägra öppna boken även
   för granskning (beslut 2026-11-26).
5. **Envägs**: ändringar från upstream tas in via synken. Forken skickar inget tillbaka och har ingen kontakt
   med upstreamprojektet.
6. **Upstreams stil** i upstreamfiler (kodstil, commitrubriker `[Modul] Kort rubrik`), så att synkningarna
   blir enkla.

## Filer och kataloger som bara finns i forken

| Sökväg | Innehåll | Synkrisk |
|---|---|---|
| `docs/` | forkens dokumentation (upstream har `doc/`, som lämnas orörd) | ingen |
| `libgnucash/se/` | biblioteket `gnc-se`: låsdatum, verifikationsserier, numrering vid låsning, vakten, kontrollsummekedja och kontroll, med tester i `test/` | ingen (använder motorns publika funktioner samt `qofinstance-p.h` och `kvp-frame.hpp` för KVP) |
| `util/se/` | byggcontainer för `future` (`future-env`, `containers/`). Ligger utanför `util_DIST` i `util/CMakeLists.txt` och kommer därför inte med i källtarbollen | ingen |

## Ändringar i upstreams filer

| Fil | Ändring | Varför | Synkrisk | Sedan |
|---|---|---|---|---|
| `libgnucash/CMakeLists.txt` | `add_subdirectory (se)` och `${se_DIST}` | Bygger `libgnucash/se/` | Liten: två rader i en fil som sällan ändras | 2026-11-26 |
| `po/POTFILES.in` | raden `libgnucash/se/gnc-se.cpp` | Filen skrivs om av bygget när en källfil med översättbara texter tillkommer | Liten: genereras om vid nästa bygge efter en synk | 2026-11-26 |
| `libgnucash/engine/Transaction.h`, `Transaction.cpp` | `GncTransCommitGuard`, `gnc_trans_set_commit_guard()` och ett anrop först i `xaccTransCommitEdit()`: säger vakten nej rullas redigeringen tillbaka med `xaccTransRollbackEdit()` | Enda sättet att stoppa ändring och radering av låsta transaktioner oavsett vem som anropar motorn. Utan registrerad vakt ändras ingenting | Medel: `xaccTransCommitEdit()` är central. Blocket ligger först i funktionen och rör ingen befintlig rad | 2026-11-26 |
| `libgnucash/engine/Transaction.cpp` | `xaccTransSetDatePostedGDate()` öppnar och stänger en redigeringsomgång runt hela funktionen | Funktionen skrev datumet till KVP (`date-posted`) *före* `xaccTransBeginEdit()`. Kopian som en återställning utgår från hade då redan det nya datumet, så ett nekat datumbyte låg kvar i KVP. Det är ett fel även utan SE | Liten: två rader. Kan försvinna om upstream rättar samma sak | 2026-11-26 |
| `libgnucash/engine/qofsession.h`, `qofsession.cpp` | `qof_session_load_in_progress()` och en räknare runt de två anropen av `m_backend->load()` | En backend som läser in en bok skapar transaktionerna genom samma `Begin/CommitEdit` som en redigering. Vakten måste kunna skilja inläsning från redigering | Liten: räknaren rör inget annat | 2026-11-26 |

## Ändrat beteende utan kodändring

Sådant som SE gör annorlunda genom att använda upstreams mekanismer på ett nytt sätt (t.ex. att sätta en
befintlig KVP-nyckel).

| Mekanism | Hur SE använder den | Status |
|---|---|---|
| KVP `trans-read-only` på transaktioner | Sätts när SE låser en transaktion, så att den ärvda koden (register, radering, makulering) hanterar låsta poster. Beslut 2026-11-26, design 01 avsnitt 4–5 | byggd (`gnc_se_lock_through()`) |
| Transaktionens `num` | Skrivs över med verifikationsnumret (`A 12`) vid låsning; den gamla texten sparas i `se/former-num` | byggd |
