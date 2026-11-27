# Avvikelser mot upstream

Allt som GnuCash SE lägger till i eller ändrar i förhållande till `upstream/future`. Listan är till för
synkningarna: varje rad är en plats där en merge av `upstream/future` kan ge konflikter eller tyst ändrat
beteende.

## Principer

1. **Egna filer före ändringar i upstreams filer.** Ny funktionalitet ligger i egna kataloger (`docs/`,
   `util/se/`, och för kod en egen modul, se designskisserna). I upstreams filer görs bara små, märkta krokar.
2. **Varje krok i en upstreamfil står i tabellen nedan**, med varför den behövs och vad som händer om upstream
   ändrar koden runt den. En krok utan rad här är ett fel.
3. **Egna KVP-nycklar under ett eget prefix**, så att de aldrig krockar med upstreams nycklar och så att
   vanlig GnuCash bara ser okända slots den lämnar orörda. Sedan design 06 är det platta nycklar som börjar
   med `se-` direkt på objektet (`se-number`, `se-series:A:periods:2025:next`), inga nästlade ramar: varje
   nästlad ram kostar SQL-backenden en egen fråga. Upstreams befintliga nycklar återanvänds bara när det är
   uttryckligen beslutat och antecknat här.
4. **Ingen feature-flagga i boken** (`gnc-features`): den skulle få vanlig GnuCash att vägra öppna boken även
   för granskning (beslut 2026-11-26).
5. **Envägs**: ändringar från upstream tas in via synken. Forken skickar inget tillbaka och har ingen kontakt
   med upstreamprojektet.
6. **Upstreams stil** i upstreamfiler (kodstil, commitrubriker `[Modul] Kort rubrik`), så att synkningarna
   blir enkla.
7. **`gnucash/` byggs inte och ändras inte** (beslut 2026-11-27). GTK-programmet ligger kvar i trädet, exakt
   som upstream har det, så att en merge av `upstream/future` aldrig ger konflikter där. CI-jobbet
   "gnucash/ is identical to upstream" (`.github/workflows/se-ci.yml`) jämför katalogen mot merge-basen och
   blir rött vid minsta skillnad. Behövs kod därifrån (t.ex. CSV-tolkarna) byggs den som källfiler i ett eget
   mål under `se/`, utan att flyttas.
8. **Ny kod ovanpå motorn ligger i `se/`** under AGPL-3.0-or-later (se `se/README.md`). `libgnucash/se`
   kompileras in i motorn och har kvar GnuCash licens.

## Filer och kataloger som bara finns i forken

| Sökväg | Innehåll | Synkrisk |
|---|---|---|
| `docs/` | forkens dokumentation (upstream har `doc/`, som lämnas orörd) | ingen |
| `libgnucash/se/` | låsdatum, verifikationsserier, numrering vid låsning, vakten, kontrollsummekedja, kontroll, tidsgränser, rättelseverifikation, behandlingshistorik och räkenskapsår (`gnc-se.h`, `gnc-se.hpp`, `gnc-se.cpp`), med tester i `test/`. Källorna kompileras in i `gnc-engine` | ingen (använder motorns publika funktioner samt `qofinstance-p.h` och `kvp-frame.hpp` för KVP) |
| `libgnucash/backend/sql/gnc-se-sql.cpp`, `gnc-se-sql.h` | behandlingshistoriken i egna tabeller (`se_events`, `se_watched`) när boken lagras i SQL: en objektbackend som de övriga i katalogen (design 06) | liten: följer `GncSqlObjectBackend`. Ändras det gränssnittet eller `GncSqlBackend` måste filen följa med |
| `se/` | forkens standardvärden för bygget (`cmake/SeDefaults.cmake`), licenstexten för AGPL och platsen för kommandolager, server, CLI och webbklient | ingen |
| `.github/workflows/se-ci.yml` | forkens egna CI-jobb: kontrollen att `gnucash/` är orörd, och ett fullständigt bygge med GTK på grenar som heter `chore/sync-upstream-…` | ingen |
| `util/se/` | byggcontainer för `future` (`future-env`, `containers/`). Ligger utanför `util_DIST` i `util/CMakeLists.txt` och kommer därför inte med i källtarbollen | ingen |

## Ändringar i upstreams filer

| Fil | Ändring | Varför | Synkrisk | Sedan |
|---|---|---|---|---|
| `CMakeLists.txt` | `include (…/se/cmake/SeDefaults.cmake)` före `option()`-blocket, och `add_subdirectory (se)` efter `util` | Den första raden ger `WITH_GNUCASH` standardvärdet OFF (ett alternativ som redan är definierat behåller sitt värde), så att GTK-programmet inte byggs. Den andra bygger forkens egen kod | Liten: tre rader plus kommentarer. Byter upstream namn på alternativet eller tar bort det måste `SeDefaults.cmake` följa med | 2026-11-27 |
| `.github/workflows/ci-tests.yml` | `ninja distcheck` är utbytt mot `ninja check` i Ubuntu-jobbet | `distcheck` vägrar köra utan `WITH_GNUCASH` (`common/cmake_modules/MakeDist.cmake`) | Liten: en rad. Övriga arbetsflöden (ASAN, Arch, macOS, Windows, coverage) är orörda och bygger utan GTK genom standardvärdet | 2026-11-27 |
| `libgnucash/backend/sql/CMakeLists.txt` | `gnc-se-sql.cpp` och `gnc-se-sql.h` i källistorna | Bygger filerna ovan in i `gnc-backend-sql` | Liten: två rader | 2026-11-27 |
| `libgnucash/backend/sql/gnc-sql-backend.cpp`, `gnc-sql-backend.hpp` | (1) `GncSqlSeHistoryBackend` registreras sist i `ObjectBackendRegistry`; (2) `gnc_sql_se_before_sync()` anropas först i `sync()`; (3) tre nya publika funktioner `begin_transaction()`, `commit_transaction()` och `rollback_transaction()` som bara skickar vidare till anslutningen | (1) och (2): historikens tabeller skapas, läses och skrivs, och historiken tas ur bokens KVP innan boken sparas till en ny databas. (3): en databastransaktion per kommando, `gnc_se_command_begin()` m.fl. | Liten till medel: raderna är märkta `// GnuCash SE` och rör ingen befintlig rad, men `sync()` och registret är centrala. Gör upstream om registreringen måste raden flyttas med | 2026-11-27 |
| `libgnucash/backend/sql/gnc-sql-connection.hpp`, `gnc-sql-backend.hpp`, `gnc-slots-sql.cpp`, `gnc-bill-term-sql.cpp`, `test/utest-gnc-backend-sql.cpp` | `GncSqlResultPtr` är `std::unique_ptr<GncSqlResult>` i stället för en vanlig pekare, de tre `delete result;` som fanns är borttagna, och testets attrapp för anslutningen lämnar ut ett nytt resultat per fråga i stället för en pekare till en medlem | Resultatet av en SELECT ägs av anroparen, och av ett trettiotal anropare frigjorde tre det. Allt som lästes vid laddning och ett eller flera resultat per sparat objekt läckte. Det är ett fel även i vanlig GnuCash | Medel: två alias-rader och tre borttagna rader, men varje ny anropare upstream som gör `delete result;` ger kompileringsfel efter en synk (lätt att se och rätta). Kan försvinna om upstream rättar samma sak | 2026-11-27 |
| `libgnucash/CMakeLists.txt` | `add_subdirectory (se)` och `${se_DIST}` | Bygger `libgnucash/se/` | Liten: två rader i en fil som sällan ändras | 2026-11-26 |
| `po/POTFILES.in` | raden för `libgnucash/se/gnc-se.cpp` | Filen skrivs om av bygget när en källfil med översättbara texter tillkommer | Liten: genereras om vid nästa bygge efter en synk | 2026-11-26 |
| `po/sv.po` | ett block sist i filen med svenska översättningar av motorns SE-texter (64 st sedan GTK-texterna togs bort 2026-11-27) | SE ska vara på svenska | Medel: upstream skriver om filen vid varje release. Blocket ligger sist och är märkt; efter en synk kontrolleras att det finns kvar (`msgfmt --check po/sv.po`) | 2026-11-26 |
| `libgnucash/engine/CMakeLists.txt` | `engine_se_SOURCES` läggs till i `gnc-engine`, och `libgnucash/se` i include-sökvägen | Låsen ska gälla för alla program som använder motorn, och motorn själv behöver fråga efter låsdatum och låsta transaktioner | Liten | 2026-11-26 |
| `libgnucash/engine/gnc-engine.cpp` | `gnc_se_init()` i `gnc_engine_init_part1()` | Sätter vakten när motorn startas | Liten: en rad | 2026-11-26 |
| `libgnucash/engine/qofbook.cpp` | `qof_book_uses_autoreadonly()` och `qof_book_get_autoreadonly_gdate()` räknar med låsdatumet (dagen efter är första öppna dag; den senare av de två trösklarna gäller) | Registret följer redan den här tröskeln: låsta rader går inte att redigera och datum i låst period nekas, utan ny gränssnittskod | Medel: två funktioner med flera anropare. Ändrar upstream tröskelns betydelse måste kroken ses över. Anroparna är GTK-registret; kroken ses över när kommandolagret byggs | 2026-11-26 |
| `libgnucash/engine/gncInvoice.c` | `gncInvoiceUnpost()` returnerar FALSE om fakturans transaktion eller en länktransaktion i dess lot är låst | Avregistrering tar bort transaktionerna; utan kroken stoppade vakten borttagningen men fakturan markerades ändå som ej bokförd | Liten: ett block före funktionens arbete | 2026-11-26 |
| `libgnucash/engine/Account.cpp` | `xaccAccountDestroy()` raderar inte ett konto som har låsta transaktioner (eget eller underkontons) | Kontoradering tar bort kontots splits även ur skrivskyddade transaktioner | Liten | 2026-11-26 |
| `libgnucash/app-utils/gnc-sx-instance-model.c` | `create_transactions_for_instance()` skapar inget om förekomstens datum är låst, och lägger ett besked i fellistan | En schemalagd transaktion ska inte hamna i låst period | Liten | 2026-11-26 |
| `libgnucash/engine/Transaction.h`, `Transaction.cpp` | `GncTransCommitGuard`, `gnc_trans_set_commit_guard()` och ett anrop först i `xaccTransCommitEdit()`: säger vakten nej rullas redigeringen tillbaka med `xaccTransRollbackEdit()` | Enda sättet att stoppa ändring och radering av låsta transaktioner oavsett vem som anropar motorn. Utan registrerad vakt ändras ingenting | Medel: `xaccTransCommitEdit()` är central. Blocket ligger först i funktionen och rör ingen befintlig rad | 2026-11-26 |
| `libgnucash/engine/Transaction.cpp` | `xaccTransSetDatePostedGDate()` öppnar och stänger en redigeringsomgång runt hela funktionen | Funktionen skrev datumet till KVP (`date-posted`) *före* `xaccTransBeginEdit()`. Kopian som en återställning utgår från hade då redan det nya datumet, så ett nekat datumbyte låg kvar i KVP. Det är ett fel även utan SE | Liten: två rader. Kan försvinna om upstream rättar samma sak | 2026-11-26 |
| `libgnucash/engine/qofsession.h`, `qofsession.cpp` | `qof_session_load_in_progress()` och en räknare runt de två anropen av `m_backend->load()` | En backend som läser in en bok skapar transaktionerna genom samma `Begin/CommitEdit` som en redigering. Vakten måste kunna skilja inläsning från redigering | Liten: räknaren rör inget annat | 2026-11-26 |

## Borttaget 2026-11-27

När GTK-programmet togs ur bygget återställdes alla krokar under `gnucash/` till upstreams innehåll:
`datecell-gnome.c`, `split-register.c`, `dialog-invoice.c`, `gnc-plugin-page-register.cpp`, `gnc-split-reg.c`,
`top-level.c` och de två CMake-filerna. Insticksmodulen `gnc-plugin-se` (menyn Åtgärder → Bokföring) och
provkörningen på virtuell skärm (`util/se/gui-test/`, `util/se/containers/future-gui/`) togs bort. Koden finns
kvar i historiken (sista commit med den: `50fe4e5fa7`) som förlaga för webbklienten.

## Ändrat beteende utan kodändring

Sådant som SE gör annorlunda genom att använda upstreams mekanismer på ett nytt sätt (t.ex. att sätta en
befintlig KVP-nyckel).

| Mekanism | Hur SE använder den | Status |
|---|---|---|
| Egna tabeller i bokens databas | `se_events` och `se_watched` skapas bredvid GnuCash tabeller och förs in i tabellen `versions`. Vanlig GnuCash läser dem inte och rör dem inte, men en "Spara som" därifrån till en ny databas tar dem inte med | byggd (design 06) |
| KVP `trans-read-only` på transaktioner | Sätts när SE låser en transaktion, så att den ärvda koden (register, radering, makulering) hanterar låsta poster. Beslut 2026-11-26, design 01 avsnitt 4–5 | byggd (`gnc_se_lock_through()`) |
| QOF-händelser (`qof_event_register_handler()`) | SE lyssnar på ändringar av konton och momstabeller och för in dem i behandlingshistoriken i bokens KVP. Bara i böcker som förs i SE | byggd (`history_event_handler()` i `gnc-se.cpp`) |
| Transaktionens `num` | Skrivs över med verifikationsnumret (`A 12`) vid låsning; den gamla texten sparas i `se/former-num` | byggd |
