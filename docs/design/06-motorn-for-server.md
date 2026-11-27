# Design 06 – motorn för server

Status: **utkast 2026-11-27** på grenen `feat/server-engine` (steg 2 i [färdplanen](../fardplan.md)). Bygger på
mätningen i [design 05](05-serverarkitektur.md), avsnitt 2 och 6.

## 1. Vad steget ska åstadkomma

Mätningen visade fyra saker som måste ändras i motorn och SE-modulen innan kommandolagret byggs ovanpå:

| Problem (design 05) | Uppmätt | Mål efter steget |
|---|---|---|
| Varje ändring av boken själv skriver om alla bokens KVP-ramar | 1,8–2,1 s per fakturanummer eller historikhändelse (PostgreSQL, 1 300 konton) | under 50 ms |
| Låsning går igenom alla transaktioner i boken | 8,3 s för 20 verifikationer i en bok med 50 000 | i proportion till det som låses |
| SE:s uppgifter på varje verifikation fördyrar laddningen | +19 s och +120 MB vid 50 000 verifikationer | nära motorns egna 12 s och 320 MB |
| Minnet växer vid varje bokändring i SQL | 2–3,5 MB per ändring | ingen tillväxt |

Dessutom: en databastransaktion per kommando, en aktör på varje historikhändelse och en entydig
registreringsordning med flera användare.

## 2. Orsaken: nästlade KVP-ramar kostar en SQL-fråga var

SQL-backenden lagrar KVP i tabellen `slots`. Varje nästlad ram är en egen grupp rader med egen identitet:

- **vid laddning** görs en SELECT per ram (`gnc-slots-sql.cpp`, `load_slot()` → `slots_load_info()`);
- **vid sparning** av ett objekt raderas alla dess ramar rekursivt, med en SELECT och en DELETE per ram
  (`gnc_sql_slots_delete()`), och skrivs sedan om.

SE lägger i dag allt under en ram `se` på boken och på varje transaktion. Det ger en extra SELECT per
transaktion vid laddning (50 000 frågor), och på boken en ram per konto, per momstabell, per händelse, per
serie, per period och per räkenskapsår.

**Regel från och med det här steget: SE använder inga nästlade KVP-ramar.** Alla SE-värden är platta nycklar
direkt på objektet, och det som växer utan gräns (händelseloggen) ligger inte i KVP alls när boken lagras i SQL.

## 3. Ny lagring

### 3.1 Platta nycklar

Dagens sökväg `se/a/b/c` blir en enda nyckel `se-a:b:c` på objektets översta nivå. Exempel:

| I dag | Efter |
|---|---|
| transaktion: `se/number`, `se/series`, `se/hash` | `se-number`, `se-series`, `se-hash` |
| bok: `se/lock-date`, `se/version` | `se-lock-date`, `se-version` |
| bok: `se/series/A/periods/2025/next` | `se-series:A:periods:2025:next` |
| bok: `se/fiscal-years/2025/end` | `se-fiscal-years:2025:end` |

Koden i `gnc-se.cpp` fortsätter att arbeta med sökvägar; en enda funktion gör om sökvägen till nyckeln, och
uppräkningen av "barn" (serier, perioder, år) letar bland bokens nycklar efter prefixet. Seriekoder och
periodnamn innehåller bara bokstäver, siffror och bindestreck, så kolon är entydigt som skiljetecken.

Vanlig GnuCash som öppnar boken för granskning ser nycklarna som okända värden och lämnar dem orörda, precis
som förut.

### 3.2 Behandlingshistoriken

Händelseloggen och historikens läge per konto och momstabell hålls i minnet i ett **historiklager** som hör
till boken, och sparas genom en av två vägar:

| Bokens lagring | Var historiken sparas |
|---|---|
| XML, eller en bok som bara finns i minnet | bokens KVP (platta nycklar `se-history:…`). Där kostar mängden ingenting extra, eftersom hela filen ändå skrivs |
| SQL (SQLite, PostgreSQL) | två egna tabeller i företagets databas: `se_events` (en rad per händelse) och `se_watched` (en rad per konto eller momstabell) |

I bokens KVP ligger alltid, oavsett väg, kedjans läge: nästa händelsenummer (`se-history:next`) och senaste
kontrollsumma (`se-history:head`). En databas där loggen har ändrats, kortats eller tagits bort stämmer då
inte med boken, och `gnc_se_verify()` rapporterar det som förut.

Tabellerna hanteras av en ny objektbackend i `libgnucash/backend/sql/` (`gnc-se-sql.cpp`), registrerad bland
de andra. Den skapar tabellerna, läser in dem när boken laddas, skriver varje ny händelse som en rad, och
skriver hela historiken när en bok sparas till en ny databas (till exempel en bok som byggts i minnet eller
lästs från XML). Före en sådan sparning tas historiken bort ur bokens KVP, så att den inte hamnar på två
ställen.

Vägen tillbaka, från SQL till en XML-fil för export eller granskning, går genom
`gnc_se_history_to_book()`, som lägger historiken i bokens KVP igen. Kommandolagret anropar den före en
export.

Vanlig GnuCash som öppnar databasen ser bokföringen men inte tabellerna, alltså inte loggen (beslut
2026-11-27).

### 3.3 Följder för redan byggda funktioner

Nycklarna byter namn, så böcker som skapats före det här steget läses inte som SE-böcker efteråt. Det finns
bara testböcker, så ingen omvandling byggs (design 05, avsnitt 6). Kontrollsummorna räknas på innehållet,
inte på nycklarnas namn, så referenshashen i testerna ändras inte.

## 4. Låsning utan att gå igenom boken

Låsningen behöver veta vilka transaktioner som inte är låsta. I dag frågar den varje transaktion i boken.

SE-modulen håller i stället ett **register över olåsta verifikationer** per bok, i minnet:

- det byggs en gång, första gången det behövs, genom att gå igenom boken (vid serverstart);
- därefter hålls det aktuellt av vakten, som redan ser varje transaktion som sparas eller tas bort;
- låsningen läser registret, och en låst transaktion tas ur det.

Registret innehåller identiteter (GUID), inte pekare, och hör till boken så att det försvinner med den.
`gnc_se_verify()` går fortfarande igenom hela boken: den är en uttrycklig kontroll, inte något som görs vid
varje kommando. Dess tid per verifikation (0,28 ms) undersöks med profilering i det här steget.

## 5. Registreringsordning

Verifikationsnummer delas ut i den ordning verifikationerna registrerades. I dag avgörs det av
registreringstiden med sekundupplösning och, vid lika tid, av GUID. Med flera användare i samma sekund blir
ordningen då godtycklig.

Varje ny transaktion får därför ett löpnummer `se-seq` när den sparas första gången. Numret tas ur en räknare
i minnet (högsta numret i boken plus ett; räknaren byggs tillsammans med registret i avsnitt 4), så att det
inte krävs en bokändring per verifikation. Låsningen ordnar efter `se-seq`, och faller tillbaka på tid och
GUID för transaktioner som saknar nummer.

## 6. Aktör

`gnc_se_set_actor()` anger vem som utför det som händer härnäst (kommandolagret sätter den före varje
kommando; det finns en bok och en motortråd per process). Historikhändelser som inte får en uttrycklig
signatur får aktören: ändrade konton, momstabeller, inställningar och räkenskapsår. Utan aktör blir
signaturen tom, som i dag.

## 7. En databastransaktion per kommando

SQL-anslutningen nästlar redan transaktioner: den första `BEGIN` är en riktig transaktion, de inre blir
`SAVEPOINT`. Historiklagrets SQL-väg känner anslutningen, så SE-modulen får tre funktioner som går genom den:
`gnc_se_command_begin()`, `gnc_se_command_commit()` och `gnc_se_command_rollback()`. Mellan `begin` och
`commit` sparas allt som en enhet och synkas till disk en gång. För XML gör de ingenting.

Det kräver ingen ändring i `QofBackend`; kroken ligger i den nya filen i SQL-backenden.

## 8. Minnesläckan i SQL-backenden

ASAN-jobbet pekar ut resultaten av SELECT-frågor (`GncDbiSqlConnection::execute_select_statement()`), samt
kopior av GUID och tal, som aldrig frigörs. Felet finns i upstreams kod och rättas där det sitter, med en rad
i avvikelselistan. Mäts med `gcse-bench` i ett ASAN-bygge med läckagekontroll (`-D LEAKS=ON`).

## 9. Det som inte görs här

- Fasta inställningar (`gnc-prefs`) och fast språk per bokvärd hör till bokvärden och byggs med den (steg 4).
- Felhanteraren för PostgreSQL (design 05, avsnitt 3) lämnas; servern skapar databasen själv.

## 10. Ordning och kontroll

1. Platta nycklar (ren omläggning i `gnc-se.cpp`), testerna gröna.
2. Historiklagret med KVP-väg; därefter SQL-vägen med tabellerna.
3. Registret över olåsta verifikationer och `se-seq`.
4. Kommandotransaktion, aktör.
5. Läckan.
6. `bench-all small` och `full` körs om, och resultaten förs in i design 05.

Efter varje punkt: `test-gnc-se` och hela sviten, och den lilla mätningen.

## 11. Krokar i upstreams filer

| Fil | Krok |
|---|---|
| `libgnucash/backend/sql/CMakeLists.txt` | den nya källfilen `gnc-se-sql.cpp` |
| `libgnucash/backend/sql/gnc-sql-backend.cpp` | registrering av objektbackenden, och ett anrop först i `sync()` |
| `libgnucash/backend/dbi/gnc-dbisqlconnection.cpp` eller `gnc-dbisqlresult.cpp` | rättning av läckan (när den är lokaliserad) |

Alla förs in i `docs/avvikelser-mot-upstream.md` när de finns.
