# Design 05 – serverarkitektur och mätning

Status: **utkast 2026-11-27** på grenen `feat/server-measurements` (steg 1 i [färdplanen](../fardplan.md)).
Mätningen är gjord; arkitekturen är inte byggd. Gränsvärdena i avsnitt 7 är förslag som Isak fastställer.

## 1. Vad steget ska avgöra

Planen bygger servern runt GnuCash motor och låter en senare kontrollpunkt (K1) avgöra om motorn ska bytas
bakom API:t. Före det behövs svar på tre frågor:

1. Vad kostar motorn med en bok av realistisk storlek: laddtid, minne, tid per skrivning, tid per rapport?
2. Fungerar GnuCash SQL-backend (libdbi) mot PostgreSQL, och vad kostar den?
3. Håller beslutet att lägga all SE-data i bokens KVP, eller måste något flyttas?

Svaren styr vad steg 2 ("Motorn för server") ska göra och vilka gränser K1 mäts mot.

## 2. Mätningen (K0)

### 2.1 Metod

`se/tools/gcse-bench.cpp` är ett litet program som länkar motorn:

- `generate URI` bygger en bok i minnet och sparar den: 1 300 konton i en platt kontoplan (storleken hos en
  BAS-kontoplan; namnen är påhittade), ett antal hela räkenskapsår med verifikationer om två till fyra rader,
  varje månad låst med `gnc_se_lock_through()` och varje år utom det sista avslutat. De två sista månaderna
  lämnas öppna.
- `measure URI` öppnar boken och mäter: laddtid och minne, enskilda skrivningar (ny verifikation, ändrad text,
  nästa fakturanummer, namnbyte på ett konto, låsning av fyra veckor), läsningarna bakom rapporterna (saldo för
  alla konton, huvudbok och verifikationslista för ett räkenskapsår) och `gnc_se_verify()`.
- `selftest` gör båda med en liten bok i SQLite och körs som testet `test-gcse-bench`.

`util/se/bench-all` kör samma sak mot XML, SQLite och PostgreSQL, och `util/se/pg-env` startar en tillfällig
PostgreSQL-server i byggcontainern. Kommandon:

```sh
cd .build/future
../../util/se/future-env ../../util/se/pg-env ../../util/se/bench-all small   # eller full
```

Miljö: utvecklingsservern (delad med annat), byggcontainern Ubuntu 26.04, PostgreSQL 18 med `fsync` på, allt på
samma disk, en processorkärna per mätning. Siffrorna är från en körning per storlek och ska läsas som
storleksordningar, inte som exakta värden. Skrivningarna är upprepade 30 gånger (liten bok) respektive 20
gånger (stor bok); tabellerna visar medianen.

### 2.2 Liten bok: 1 300 konton, 1 år, 2 000 verifikationer (6 000 rader)

| | XML | SQLite | PostgreSQL |
|---|---|---|---|
| Spara hela boken första gången | 0,2 s | 3,2 s | 8,5 s |
| Ladda | 0,6 s | 5,6 s | 1,5 s |
| Minne efter laddning | 34 MB | 50 MB | 45 MB |
| Ny verifikation | 0,5 ms | 51 ms | 13 ms |
| Ändra text på en verifikation | 0,2 ms | 14 ms | 3,5 ms |
| **Nästa fakturanummer** (bokens räknare) | 0,01 ms | **980 ms** | **1 640 ms** |
| **Byta namn på ett konto** (historikhändelse) | 0,1 ms | **1 140 ms** | **1 910 ms** |
| Låsa fyra veckor (30 verifikationer) | 0,16 s | 4,4 s | 3,0 s |
| Saldo för alla konton | 0,7 ms | 0,9 ms | 0,7 ms |
| Huvudbok, ett år | 13 ms | 16 ms | 10 ms |
| Verifikationslista, ett år | 21 ms | 33 ms | 19 ms |
| `gnc_se_verify()` | 0,45 s | 0,60 s | 0,38 s |
| Minne efter mätningens ca 90 bokändringar | 39 MB | 235 MB | 353 MB |

XML skriver ingenting förrän hela filen sparas (0,23 s här), så dess skrivtider är bara minnesarbetet och
visar vad motorn själv kostar.

### 2.3 Stor bok: 1 300 konton, 5 år, 50 000 verifikationer (ca 150 000 rader)

| | XML | SQLite | PostgreSQL |
|---|---|---|---|
| Bygga boken i minnet, med 58 låsningar | 188 s | 189 s | 206 s |
| Spara hela boken första gången | 5,7 s | 81 s | 195 s |
| **Ladda** | 17 s | 83 s | **31 s** |
| **Minne efter laddning** | 176 MB | 496 MB | **444 MB** |
| Ny verifikation | 3,7 ms | 57 ms | 15 ms |
| Ändra text på en verifikation | 0,3 ms | 11 ms | 3,3 ms |
| **Nästa fakturanummer** (bokens räknare) | 0,01 ms | 1 380 ms | **1 820 ms** |
| **Byta namn på ett konto** (historikhändelse) | 0,3 ms | 1 440 ms | **2 080 ms** |
| **Låsa fyra veckor** (20 verifikationer) | 0,9 s | 17 s | **8,3 s** |
| Saldo för alla konton, första gången | 1,1 s | 1,3 s | 1,1 s |
| Saldo för alla konton, därefter | 1 ms | 3 ms | 1 ms |
| Huvudbok, ett år (30 000 rader) | 111 ms | 147 ms | 131 ms |
| Verifikationslista, ett år (10 000 verifikationer) | 189 ms | 194 ms | 191 ms |
| **`gnc_se_verify()`** | 14 s | 15 s | **14 s** |
| Minne efter mätningens ca 40 bokändringar | 180 MB | 511 MB | 557 MB |
| Storlek på disk | 13 MB | 110 MB | 176 MB |

I PostgreSQL har boken 398 000 rader i tabellen `slots` (KVP), ungefär åtta per verifikation. 4 400 av dem
är SE-historiken i bokens egen KVP.

### 2.4 Samma bok utan lås, serier och historik

För att skilja motorns kostnad från SE-modulens gjordes samma bok en gång till med `--no-lock`, alltså som
vanlig GnuCash skulle ha den (54 000 rader i `slots` i stället för 398 000). Bara laddningen mättes.

| | XML | SQLite | PostgreSQL |
|---|---|---|---|
| Bygga boken i minnet | 85 s | 87 s | 83 s |
| Spara hela boken första gången | 3,6 s | 40 s | 83 s |
| Ladda | 12 s | 30 s | 12 s |
| Minne efter laddning | 129 MB | 371 MB | 322 MB |

Motorn själv står alltså för ungefär 12 s laddtid och 320 MB vid 50 000 verifikationer från PostgreSQL; SE:s
uppgifter på varje verifikation lägger till 19 s och 120 MB. Räknat per tusen verifikationer är det ungefär
0,25 s och 6 MB för motorn, och 0,4 s och 2,5 MB till för SE som den lagrar i dag.

### 2.5 Slutsatser

1. **Läsningarna är inget problem.** Rapporternas underlag tas fram på millisekunder ur minnet, oavsett lagring.
2. **Varje ändring av boken själv kostar en till två sekunder i SQL**, redan med en liten bok. Det gäller varje
   fakturanummer, varje historikhändelse och varje låsning. Orsaken är hur SQL-backenden lagrar KVP: varje
   nästlad ram är en egen grupp rader, och när ett objekt sparas raderas och skrivs alla dess ramar om, med en
   SELECT och en DELETE per ram (`libgnucash/backend/sql/gnc-slots-sql.cpp`, `gnc_sql_slots_delete()`).
   SE-historiken lägger en ram per konto i bokens KVP (`se/history/account/<guid>/…`), alltså 1 300 ramar, och
   en ram per händelse. Kostnaden växer med antalet konton och med historikens längd, inte med antalet
   verifikationer.
3. **En verifikation kostar flera databastransaktioner.** Transaktionen och varje rad sparas var för sig, var
   och en med egen COMMIT (och egen synkning till disk). Därav 51 ms i SQLite. Det är också ett korrekthetsfel
   för en server: en krasch mitt i lämnar en halv verifikation i databasen.
4. **Minnet växer vid varje bokändring i SQL** med ungefär 2–3,5 MB per gång i mätningen. I XML växer det inte,
   så det ligger i SQL-backenden eller libdbi. Orsaken är inte utredd. En bokvärd lever länge, så det måste
   bort eller förklaras (steg 2, med ASAN-bygge).
5. **Låsning och kontroll växer med hela boken, inte med det som låses.** Att låsa tjugo verifikationer tar
   0,9 s i minnet med 50 000 verifikationer i boken (0,16 s med 2 000), och `gnc_se_verify()` tar 14 s. Att
   bygga boken med 58 låsningar tog 188 s mot 85 s utan. Det ligger i SE-modulen: varje låsning går igenom alla
   transaktioner och läser deras KVP.
6. **Laddtid och minne växer med boken och klarar inte de föreslagna gränserna vid 50 000 verifikationer**,
   inte ens utan SE (12 s och 320 MB från PostgreSQL). Med SE som det lagras i dag är det 31 s och 444 MB. En
   bok med 10 000 verifikationer hamnar enligt samma takt runt 2,5 s och 85 MB utan SE. Hela boken i minnet är
   motorns grundantagande och går inte att ändra med en krok.
7. **Vanliga skrivningar håller**: en ny verifikation tar 15 ms mot PostgreSQL (95:e percentilen 20 ms).

Beslutet 2026-11-27 var att SE-data får flyttas ur bokens KVP "om mätningen kräver det". **Mätningen kräver
det** för historikens läge per konto och för händelseloggen. Se avsnitt 6.

Punkt 6 är den som avgör motorfrågan vid K1, och den beror på vilken bokstorlek gränserna ska gälla för
(avsnitt 7).

## 3. PostgreSQL genom libdbi

- **Fungerar:** spara, ladda och enskilda skrivningar går igenom mot PostgreSQL 18 med `libdbd-pgsql`, och
  upstreams `test-backend-dbi` går igenom med `TEST_PGSQL_URL` satt.
- **Motorn kan inte skapa en ny databas.** `error_handler<DbType::DBI_PGSQL>` i
  `libgnucash/backend/dbi/gnc-backend-dbi.cpp` känner igen "databasen finns inte" på att felmeddelandet
  *börjar* med `FATAL:  database`. Sedan PostgreSQL 14 börjar libpq:s meddelande med
  `connection to server at "…", port … failed: `, så villkoret slår aldrig till och försöket slutar med
  "Unable to connect". Det gäller även vanlig GnuCash. Servern skapar därför databasen själv (det ska den
  ändå göra, med rätt ägare och rättigheter) och öppnar den sedan som befintlig och tom.
- **Databasen sväller** av att tusentals rader raderas och skrivs om vid varje bokändring: 60 MB för den lilla
  boken efter mätningen. Det försvinner med åtgärderna i avsnitt 6.
- **Lösenordet står i URI:n** som motorn får. Bokvärden får URI:n från gatewayen över sin egen kanal; den
  loggas aldrig och står inte på kommandoraden.
- Inte prövat: återanslutning efter att servern har startats om eller anslutningen har legat vilande, och
  beteendet över nätverk i stället för på samma maskin.

## 4. Arkitektur

### 4.1 Processer

```
webbklient      CLI (gcse)      andra program
      \             |              /
       +---- HTTPS: REST + JSON, SSE ----+        TLS i en omvänd proxy (Caddy, nginx)
                    |
   gateway: inloggning, behörighet, företagsregister, kvoter
                    |   samma HTTP + JSON över Unix-socket
   bokvärd per företag: kommandolager -> motor + SE-vakt
                    |
   företagets databas (PostgreSQL eller SQLite) och underlagsfiler
```

**Bokvärden** är en process per öppet företag. Motorn har processglobalt tillstånd (aktuell session,
händelsehanterare, vakten, datumformat) och är inte trådsäker, så en bok per process är ett villkor, inte ett
val. Bokvärden har:

- en nätverkstråd (Boost.Asio) som tar emot HTTP på en Unix-socket eller på 127.0.0.1;
- **en motortråd** som ensam rör motorn och tar kommandon och frågor ur en kö, ett i taget.

All läsning går genom samma tråd. Mätningen visar att rapporternas läsningar tar millisekunder, så det håller;
en riktigt tung fråga blockerar ändå registreringen i det företaget, och det mäts vid K1.

**Gatewayen** är en autentiserande mellanhand. Den äger kontrolldatabasen (organisationer, användare,
sessioner, API-nycklar, roller, företagsregister), startar en bokvärd när ett företag först används och
stänger den efter en tids vila, och skickar begäran vidare med betrodda huvuden: användare, roll,
begärans-id. Bokvärden litar på huvudena därför att bara gatewayen når dess socket (filrättigheter), och
kontrollerar själv rollen mot varje kommando.

Gateway och bokvärd talar **samma HTTP + JSON** som klienterna. Det ger ett protokoll och en
OpenAPI-beskrivning, och lokalt läge är bokvärden ensam. Gatewayen kan därför byggas efter bokvärden
(steg 6 efter steg 4).

**Lås och krasch.** Gatewayen håller ett eget lås per företag, så att två bokvärdar aldrig öppnar samma bok.
Motorns `gnclock`-rad ligger kvar efter en krasch; eftersom gatewayens lås är det som gäller öppnar bokvärden
med lås-brytning. En bokvärd som kraschar startas om vid nästa begäran, och klienterna får veta det genom
ändringsflödets epok (4.4).

**Lokalt läge** är en bokvärd med SQLite på 127.0.0.1: slumpad port, kontroll av `Host` och `Origin`, och en
engångskod vid start som webbläsaren byter mot en session. Utan de tre skulle vilken webbsida som helst kunna
anropa den lokala servern.

### 4.2 Kommandolagret

`se/core` är ett C++-bibliotek mellan HTTP-lagret och motorn. Det är här API:ts begrepp översätts till
motorns, och omvänt, så att motorn kan bytas utan att API:t ändras.

- **Kommandon** ändrar (skapa verifikation, lås till och med, rätta, lägg upp konto) och **frågor** läser
  (huvudbok, verifikationslista). Båda är typade strukturer med egna dataformat; ingenting ur motorn
  (`Transaction*`, KVP, GUID-typer) syns utanför lagret.
- **Validering före motorn.** En verifikation som inte balanserar nekas; motorns obalanskonto används inte. En
  transaktion som väl har skapats kan inte nekas säkert i vakten (lärdom från design 01), så allt som kan
  kontrolleras i förväg kontrolleras i förväg.
- **Ett kommando är en databastransaktion.** SQL-anslutningen stöder redan nästling: den första `BEGIN` blir
  en riktig transaktion och de inre blir `SAVEPOINT` (`gnc-dbisqlconnection.cpp`, `begin_transaction()`).
  Kommandolagret öppnar den yttre före kommandot och avslutar den efter. Då sparas allt eller inget, och det
  blir en synkning till disk per kommando i stället för en per objekt.
- **Fel mitt i ett kommando.** Databasen rullas tillbaka, men motorns minne kan inte rullas tillbaka som
  helhet. Bokvärden avslutar sig då och startas om från databasen. Det ska vara sällsynt, eftersom valideringen
  ligger före.
- **Aktör.** Varje kommando bär den inloggade användaren. SE-modulen får en "aktuell aktör" som sätts före
  kommandot, så att alla historikhändelser får en signatur, även de som i dag saknar den (konton,
  momstabeller, inställningar).

### 4.3 Samtidighet

Motortråden serialiserar alla skrivningar inom ett företag. Det räcker för fakturaräknare och
verifikationsnumrering, förutsatt att numret tas i samma kommando som objektet det hör till.

Mellan användare gäller optimistisk samtidighet: varje resurs har en `ETag`, klienten skickar `If-Match` när
den ändrar, och servern svarar `412 Precondition Failed` om någon annan hann före. En ändring utan `If-Match`
nekas med `428`. ETag är en kontrollsumma av resursens innehåll, så ingen versionsräknare behöver lagras.

Verifikationernas ordning inom serien bygger i dag på registreringstid med sekundupplösning och GUID som
särskiljare. Med flera användare blir det godtyckligt, så varje registrering får ett löpnummer (`se/seq`) som
avgör ordningen.

### 4.4 Ändringsflöde

`GET …/events` är en SSE-ström per företag. Varje händelse har ett löpnummer och säger vilken resurs som
ändrades; klienten hämtar själv det nya innehållet. Löpnumret ligger i bokvärdens minne tillsammans med en
**epok** som byts när bokvärden startar. En klient som återansluter med `Last-Event-ID` från en gammal epok
får en `reset`-händelse och läser om det den visar.

### 4.5 API-principer

| Område | Val |
|---|---|
| Källa | `se/api/openapi.yaml` (OpenAPI 3.1). Webbklientens typer genereras ur den, och varje svar kontrolleras mot den i testerna |
| Adresser | `/api/v1/…`, resurser per företag under `/api/v1/companies/{company}/…`. Engelska namn i API:t, med ordlista mot de svenska begreppen |
| Version | Bara tillägg inom `v1`. Det som bryter får `v2` |
| Belopp | Decimaltal som sträng (`"1234.50"`) tillsammans med valutakod. Aldrig flyttal |
| Datum | Bokföringsdatum som `ÅÅÅÅ-MM-DD`. Tidpunkter enligt RFC 3339 i UTC |
| Identiteter | Ogenomskinliga strängar. Klienten ska inte tolka dem |
| Fel | `application/problem+json` (RFC 9457) med en stabil kod för programmet och en text för människan, på språket i `Accept-Language` |
| Ändringar | `ETag` och `If-Match` (4.3) |
| Omförsök | `Idempotency-Key` på anrop som skapar något, så att ett omsänt anrop inte ger två verifikationer |
| Listor | Sidindelning med markör (`limit`, `after`), aldrig hela boken i ett svar |
| Händelser | SSE (4.4) |
| Inloggning | Webb: sessionskaka (`HttpOnly`, `Secure`, `SameSite=Lax`) och CSRF-skydd på ändrande anrop. CLI och andra program: API-nyckel som `Authorization: Bearer` |

### 4.6 Lagring

| Vad | Var |
|---|---|
| Företagets bok | GnuCash SQL-backend: en PostgreSQL-databas per företag på server, en SQLite-fil lokalt. Vanlig GnuCash kan öppna den för granskning. XML är bara import och export |
| SE-data | Det mesta i KVP som i dag; händelseloggen och historikens läge per konto flyttas (avsnitt 6) |
| Underlag | Filer i en katalog per företag, namngivna efter sin SHA-256. Länken ligger på verifikationen. Underlag är räkenskapsinformation och hör till företaget, inte till kontrolldatabasen |
| Kontrolldatabas | Organisationer, användare, sessioner, API-nycklar, roller, företagsregister. SQLite först, PostgreSQL när SaaS-steget kommer. Begreppet organisation finns med från början |

### 4.7 Motorns globala tillstånd

- **Aktuell session.** Motorn frågar själv efter `gnc_get_current_session()` på flera ställen (`Scrub.cpp`,
  `gnc-optiondb.cpp`, `engine-helpers.c`), och funktionen skapar tyst en tom bok om ingen session är satt
  (`gnc-session.c`). Bokvärden sätter sin session med `gnc_set_current_session()` direkt efter laddningen och
  kontrollerar före varje kommando att den aktuella sessionens bok är bokvärdens.
- **Inställningar.** Motorn läser användarinställningar genom `gnc-prefs`. Utan en registrerad källa blir
  svaren noll och falskt. Bokvärden registrerar en egen källa med fasta värden, så att beteendet inte beror
  på maskinen.
- **Språk.** Historikens texter skrivs på processens språk. Bokvärden kör med ett fast språk per företag.

## 5. Säkerhet i korthet

- Gatewayen är den enda som exponeras. Bokvärdarna lyssnar bara på Unix-socket eller 127.0.0.1.
- Lösenord lagras med argon2id (libsodium). API-nycklar lagras som kontrollsummor och visas en gång.
- Behörighet avgörs av rättigheter per kommando; de fyra rollerna är fasta uppsättningar av rättigheter.
- Betrodda huvuden från en auth-proxy godtas bara när det är påslaget och bara från angivna adresser.
- Handskriven HTTP- och inloggningskod granskas särskilt (färdplanens risker); OIDC kommer i ett eget steg.
- Inloggningar och administrativa åtgärder loggas i kontrolldatabasen, skilt från bokföringens
  behandlingshistorik.

## 6. Vad steg 2 ska göra

Mätningen ger steg 2 ("Motorn för server") det här innehållet, i prioritetsordning:

1. **Kommandotransaktion**: en liten krok i SQL-backenden som låter kommandolagret öppna och avsluta en yttre
   transaktion. Löser slutsats 3 (atomicitet, och en diskskrivning per kommando).
2. **Historikens läge per konto och momstabell ut ur bokens KVP.** Läget läggs på kontot respektive
   momstabellen själv (ett värde per objekt i stället för en ram per objekt i boken). Löser huvuddelen av
   slutsats 2.
3. **Händelseloggen bakom ett lagringsgränssnitt i SE-modulen.** KVP blir kvar som lagring för XML; med SQL
   skrivs händelserna till en egen tabell i företagets databas, en rad per händelse. Kedjans senaste
   kontrollsumma ligger kvar i bokens KVP, så att en bok vars logg har ändrats eller tappats fortfarande
   avslöjas. Vanlig GnuCash ser då bokföringen men inte loggen.
4. **Låsning och kontroll som inte går igenom hela boken**: låsningen ska bara röra transaktionerna efter
   förra låsdatumet, och kontrollen ska kunna göras per serie och period. Löser slutsats 5.
5. **SE:s uppgifter per verifikation** (serie, period, nummer, kontrollsumma m.m., i dag åtta KVP-rader per
   verifikation) ses över: färre och plattare värden ger kortare laddtid och mindre minne (slutsats 6, SE:s
   del).
6. **Minnesökningen per bokändring** utreds med ASAN-bygge och mäts om efter punkt 2 och 3.
7. **Aktör per kommando, löpnummer per registrering (`se/seq`), fasta inställningar och fast språk** (4.2, 4.3,
   4.7).
8. Mätningen körs om med `bench-all` och förs in här.

Punkterna 2, 3 och 5 ändrar hur redan byggda funktioner lagrar sina data. Böcker som finns i dag är bara
testböcker, så någon omvandling av gamla böcker byggs inte.

Öppet inför steg 2: felhanteraren för PostgreSQL (avsnitt 3) kan rättas med en rad, men servern behöver inte
rättningen. Den lämnas tills vidare.

## 7. Gränsvärden för K1 (förslag)

K1 ligger efter gatewayen och före fakturorna. Mäts med `bench-all full` plus samtidiga klienter mot API:t.

| Mätvärde | Föreslagen gräns | K0 i dag (stor bok, PostgreSQL) |
|---|---|---|
| Minne per öppet företag | högst 200 MB | 444 MB efter laddning, 557 MB efter mätningen (utan SE: 322 MB) |
| Kallstart från PostgreSQL | högst 3 s | 31 s (utan SE: 12 s) |
| Skrivkommando, 95:e percentilen | högst 150 ms | ny verifikation 20 ms; bokändring 2 000–2 200 ms; låsning 8,3 s |
| Rapport, 95:e percentilen | högst 500 ms | huvudbok 135 ms, verifikationslista 192 ms; första saldoberäkningen 1,1 s |
| Köväntan med 20 samtidiga användare, 95:e percentilen | högst 100 ms | inte mätbart före API:t |
| Öppna företag per GB minne | mäts | – |
| Antal krokar i upstreamkod och tid per synk | mäts | – |

Faller två eller fler gränser efter att steg 2 är gjort byts motorn bakom API:t.

**Att avgöra (Isak):**

1. **Vilken bokstorlek ska gränserna gälla för?** Med 50 000 verifikationer klarar motorn varken minnet eller
   kallstarten, oavsett vad steg 2 gör med SE:s del. Med 10 000 verifikationer (till exempel 2 000 om året i
   fem år) ser den ut att klara båda. Förslag: mät vid båda storlekarna, låt gränserna gälla vid 10 000, och
   ange 50 000 som den storlek där en bokvärd får ta längre tid på sig att starta men inte får bli långsam i
   arbetet.
2. **Hur viktig är kallstarten?** Den märks när ett företag öppnas efter att bokvärden har stängts i vila,
   och i lokalt läge varje gång programmet startas. Den går att mildra (håll bokvärdar igång längre, visa
   förlopp) men inte ta bort så länge hela boken läses in.
3. Gränserna i tabellen i övrigt.

## 8. Kända begränsningar i mätningen

- En körning per storlek, på en delad server. Inga konfidensintervall.
- Kontoplanen är platt och verifikationerna slumpade jämnt över alla konton. I verkliga böcker ligger de
  flesta raderna på några få konton (bank, moms, kundfordringar), och motorn räknar om ett kontos saldon när
  det får en ny rad. Skrivtiderna för en verklig bok kan därför vara högre än här; det mäts i steg 2 med en
  sned fördelning.
- Att bygga boken utan lås tog 85 s för 50 000 verifikationer, alltså 1,7 ms per verifikation i minnet. Varför
  så mycket är inte utrett.
- Inga fakturor, kunder eller underlag i boken.
- Allt på en maskin: nätverkets fördröjning mot en databasserver på annan maskin ingår inte.
- Mätprogrammet ändrar boken det mäter (lägger till verifikationer och låser dem).
