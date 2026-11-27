# GnuCash SE – färdplan

Läge 2026-11-27. Underlag: [`svenska-krav.md`](svenska-krav.md) (lagkraven med källor), designskisserna i
[`design/`](design/) (det som är byggt) och [`avvikelser-mot-upstream.md`](avvikelser-mot-upstream.md) (krokarna
i upstreams kod). Planen nedan godkändes av Isak 2026-11-27 och ersätter den tidigare stegordningen.

## Varför en fork

Den svenska bokföringslagen kräver kontroller som GnuCash saknar:

| Krav | Källa | GnuCash i dag (`future`, 2026-11-25) |
|---|---|---|
| Det bokförda ska vara varaktigt; så länge posterna kan ändras är inget bokfört | BFNAR 2.1 med kommentar | Skrivskyddet är rullande ("i dag minus N dagar") och kontrolleras bara i gränssnittet |
| Rättelse bara genom en särskild rättelsepost med egen verifikation, och det ska synas på den rättade posten | 5 kap. 5 och 9 §§ BFL, BFNAR 2.17–2.18, 5.15–5.16 | Fri redigering; `xaccTransReverse` länkar bara från originalet till rättelsen |
| Obruten verifikationsserie som systemet kan kontrollera | BFNAR 5.9–5.10, 9.6 | Nummerfältet är fritext; ingen serie, ingen kontroll |
| Behandlingshistorik med registreringsdatum och systemändringar | 5 kap. 11 § BFL, BFNAR 9.16 | Registreringsdatum finns; transaktionsloggen (`*.log`) är ingen historik i lagens mening |
| Räkenskapsår per bok | 3 kap. BFL | Saknas (upstreams bugg 493789) |

Upstream har avböjt lås och numrering: "I don't think that lock date and transaction numbering are feasible
with GnuCash's overall design. We've always advised businesses that require that level of built-in controls
that GnuCash isn't suitable for them." (John Ralls, gnucash-devel 2026-11-26). Därför byggs de här, i ett
fristående projekt som bara använder GnuCash som startpunkt och källa för löpande ändringar.

## Byggt hittills

Grundfunktionerna i motorn (`libgnucash/se`), byggda 2026-11-26–27. Numren är designskissernas.

| Design | Innehåll | Lagkrav |
|---|---|---|
| [01](design/01-las-och-numrering.md) | Periodlåsning med ett låsdatum per bok som bara kan flyttas framåt. Verifikationsserier, en per räkenskapsår som standard. Nummer tilldelas vid låsning i registreringsordning. Spärr mot ändring och radering av låsta transaktioner. Kontroll att serien är obruten. Upptäckt av ändringar gjorda utanför SE | BFNAR 2.1, 5.9–5.10 |
| [02](design/02-rattelse.md) | Rättelseverifikation från en låst post, med hänvisning åt båda håll och vem och när | 5:5, 5:9 BFL, BFNAR 2.17–2.18, 5.15–5.16 |
| [03](design/03-behandlingshistorik.md) | Logg över låsningar, rättelser och systemändringar som följer med boken, och uppställning av varje bokförd post med registrerings- och bokföringstid | 5:11 BFL, BFNAR 9.16 |
| [04](design/04-rakenskapsar.md) | Räkenskapsår per bok, även brutet, förkortat och förlängt; serier per år; avsluta räkenskapsår | 3 kap. BFL |

Funktionerna nåddes genom en insticksmodul i GnuCash GTK-program. Den togs bort 2026-11-27 (steg 0 nedan), så
tills webbklienten klarar kärnan (steg 5) finns de bara som API i motorn, med tester.

Inte byggt av lagkraven i underlaget: systemdokumentation (BFNAR 9.2), steg 17 nedan.

## 1. Målbild

GnuCash SE blir ett serverprogram med webbklient. Samma program körs som servertjänst för flera samtidiga
användare eller lokalt på en dator. Allt arbete, både bokföring och administration, går genom ett API.
GnuCash C++-motor med SE-modulen är kärnan; GTK-gränssnittet används inte längre.

## 2. Beslut (Isak 2026-11-27)

| Fråga | Beslut |
|---|---|
| Gränssnitt | GTK släpps helt. Webbklienten är det enda gränssnittet, för bokföring och för administration |
| GTK-programmet | Tas ur bygget direkt. Filerna i `gnucash/` ligger kvar orörda så att synken mot upstream inte ger konflikter |
| Motorn | libgnucash + SE-modulen behålls nu. API:t får inte läcka motorns egenheter. Kontrollpunkt K1 (avsnitt 6) avgör om motorn byts bakom API:t |
| Driftmodell | Egen drift med flera företag, gemensamma användare och behörighet per företag. Samma program ska kunna sättas upp som SaaS: åtskilda organisationer, självregistrering, kvoter |
| Lokalt läge | Samma serverprogram, lyssnar bara på den egna datorn, öppnar webbklienten i webbläsaren. Eget programfönster (Tauri) och installationspaket senare |
| Licens | Nya delar (server, CLI, webbklient) AGPL-3.0-or-later. GnuCash-koden och `libgnucash/se` förblir GPL-2.0-or-later. Enbart FOSS i beroenden, tjänster, typsnitt, ikoner och ljud |
| Serverteknik | C++ med Boost.Beast, Asio och JSON (Boost krävs redan av motorn) |
| Webbklient | Svelte/SvelteKit med TypeScript, byggd till statiska filer som servern levererar |
| Behörighet | Fyra fasta roller per företag (administratör, redovisare, registrerare, läsare), byggda internt på enskilda rättigheter så att egna roller kan komma senare |
| Inloggning | Lokala konton, API-nycklar, inbyggd OIDC och dessutom betrodda huvuden från en auth-proxy |
| SE-data | Får flyttas från bokens KVP till egna tabeller i företagets databas om mätningen i steg 1 kräver det. Kedjans senaste kontrollsumma stannar i KVP |
| Kontokoder | Momsruta och redovisningskod per konto lagras under egna `se/`-nycklar nu. Väntar inte på upstreamspåret |
| Valuta | Flervaluta från första versionen |
| PDF | Skapas på servern från första versionen, med Typst som separat process. E-postutskick av fakturor ingår |
| Nummerformat | Variabler i klamrar (`F-{nr:3}`), med fler variabler än i den första skissen (avsnitt 8) |
| Bankimport | Eget steg efter första versionen. Isak beskriver vad som är bra och dåligt i iOrdnings lösning när designskissen skrivs |
| Bolagsverket | Bryggan (avtal, certifikat) är ett separat projekt. SE speglar Bolagsverkets API v2.1 mot en inställbar adress |
| Första versionen | Kärnan plus underlag, moms, SIE-import och kund- och leverantörsfakturor (avsnitt 5) |
| SIE | Byggs här genom API:t. Import ingår i första versionen (steg 9); export är första steget efter den (steg 12) |

Ytterligare val (kan ändras):
- Servern byggs och testas först för Linux. macOS och Windows fortsätter bygga motorn i CI och får servern i
  steget för lokalt läge.
- Upstreams CI-arbetsflöden behålls med ändrat standardvärde, och allt nytt testas i ett eget arbetsflöde.
- Företagets bok lagras med GnuCash SQL-backend: PostgreSQL på server, SQLite lokalt. XML blir bara import och
  export. Vanlig GnuCash kan därmed fortfarande öppna en bok för granskning.
- Rapporterna (huvudbok, balans, resultat, verifikationslista, moms) byggs nya. GnuCash Guile-rapporter används
  inte, eftersom de försvinner ur bygget med GTK.
- Schemalagda transaktioner, budget och värdepapper ingår inte i planen.

Beslut från 2026-11-26 som ändras (avsnitt 11): "all SE-data i KVP" (se ovan) och "ingen nätverkskod mot
myndigheter" (gäller fortfarande Skatteverket; för Bolagsverket byggs klienten mot bryggan i steg 21). Arbetssättet står kvar: ett
steg i taget på egen gren, designskiss först, grön CI, fråga Isak före merge, push på begäran.

## 3. Vad utredningen visade

**Upstream 6.0:** frysning 2027-02-07, release 2027-03-28. Det blir en modernisering av verktygskedjan (C++23,
CMake 4, nya minimiversioner), fortfarande GTK3. Inget ändras i SQL-backenden, sessionslagret,
`Transaction.cpp` eller KVP, och upstream har inga planer på fleranvändarläge eller API. Planen krockar alltså
inte med 6.0. I rörelse just nu: `Split.cpp`, `Scrub.cpp`, `qofid`, `guid`, `gnc-uri` och enstaka filer som
görs om från C till C++.

**Motorn:**
- Hela boken ligger i minnet. Motorn är inte trådsäker och har mycket processglobalt tillstånd, så det blir en
  process per öppet företag.
- SQL-backenden skriver ett objekt i taget. Ett kommando som rör flera objekt (en fakturabokning) är därför
  inte atomärt i databasen i dag.
- Bokens KVP-rader skrivs om allihop vid varje bokändring, alltså vid varje fakturanummer och varje
  historikhändelse. Uppskattat till tusentals SQL-satser per gång med en BAS-kontoplan. Inte mätt än.
- Utan GTK är 91 av de tidigare 141 testerna kvar (alla gröna 2026-11-27, se [`byggmiljo.md`](byggmiljo.md)).
  De 50 som försvann är rapporter (31), import (13) och GTK (6).
- Guile krävs fortfarande av bygget men länkas inte in i servern.
- Logik som bara finns i GTK-lagret och måste byggas om i servern: balanskontroll och växelkurs vid
  registrering, standardvärden på fakturarader, betalningsdialogens urval, bokslutsomföringen.

**Externt:**
- Bolagsverkets API är v2.1 (skapa inlämningstoken, kontrollera, lämna in). Det kräver avtal med juridisk
  person, organisationscertifikat och vitlistade IP-adresser. Underskriften görs med BankID hos Bolagsverket.
  Format: iXBRL, K2-taxonomi 2024-09-12.
- iOrdning: CSV-kontoutdrag, matchning mot verifikationer på datum och belopp, "Skapa verifikation" och förslag
  utifrån tidigare bokningar. Någon särskild "ny bankimport" gick inte att belägga.
- Befintliga webbprojekt kring GnuCash skriver direkt i databasen och går förbi motorn. De går inte att
  återanvända, eftersom de också skulle gå förbi SE:s lås.

Inte verifierat: att bygget utan GTK fungerar på Windows och macOS (prövat på Linux), hur libdbi beter sig mot PostgreSQL, och om
namnet "GnuCash" kan användas för ett program utan GnuCash gränssnitt (inte undersökt).

## 4. Arkitektur

```
webbklient (SvelteKit)      CLI (gcse)      andra program
          \                     |                /
           +------ HTTPS: REST + JSON, SSE -----+
                            |
        gateway: inloggning, behörighet, företagsregister, kvoter
                            |  samma HTTP + JSON över Unix-socket
        bokvärd per företag: kommandolager -> motor + SE-vakt
                            |
        företagets databas (PostgreSQL eller SQLite) + underlagsfiler
```

- **Bokvärd:** en process per öppet företag med boken i minnet och en enda motortråd som tar kommandon i kö.
  Den serverar hela API:t för sitt företag. Lokalt läge är en ensam bokvärd på 127.0.0.1.
- **Gateway:** autentiserande mellanhand som startar och stoppar bokvärdar, håller ett eget lås per företag och
  skickar vidare vem användaren är och vilken roll den har. Bokvärden kontrollerar rollen mot varje kommando.
- **Kommandolager** (`se/core`): typade kommandon och frågor med egna dataformat (verifikation, konto, faktura).
  Varje kommando körs i en databastransaktion. Går ett kommando fel halvvägs rullas databasen tillbaka och
  bokvärden startas om, eftersom minnet inte kan rullas tillbaka. Obalanserade verifikationer nekas.
- **Samtidighet:** motortråden serialiserar alla skrivningar. Klienten skickar med versionen den utgick från
  (ETag) och får ett tydligt konfliktsvar om någon annan hann före. Ändringar strömmas till klienterna med SSE.
- **Vem:** signaturen i SE är den inloggade användaren, och alla historikhändelser får en aktör.
- **Kontrolldatabas:** organisationer, användare, sessioner, API-nycklar, roller och företagsregister. SQLite
  först, PostgreSQL i SaaS-steget. Begreppet organisation finns i datamodellen från början.
- **Underlag** är räkenskapsinformation och lagras hos företaget: filer adresserade med SHA-256, länk på
  verifikationen.
- **API:** `se/api/openapi.yaml` (OpenAPI 3.1) är källan. Ur den genereras webbklientens typer, och varje svar
  kontrolleras mot den i testerna.
- **TLS** sköts av en omvänd proxy (Caddy eller nginx) framför gatewayen.
- **Lokalt läge:** slumpad port, kontroll av Host och Origin och en engångskod vid start, så att andra
  webbsidor inte kan anropa den lokala servern.

**Trädet:** allt nytt i en toppnivåkatalog `se/` (`core/`, `server/`, `cli/`, `web/`, `api/`, `test/`) med
AGPL-3.0-or-later, SPDX-huvuden och `LICENSES/`. En enda krok i toppnivåns `CMakeLists.txt`. `libgnucash/se`
ligger kvar i motorn.

## 5. Stegen

Storlek: S, M, L, XL. Varje steg börjar med en designskiss i `docs/design/`.

| # | Steg | Levererar | Storlek | Största risk |
|---|---|---|---|---|
| **0** (byggt 2026-11-27) | Omläggning | GTK ur bygget via eget standardvärde. Krokarna i GTK-filer återställs; `gnc-plugin-se.*` och `util/se/gui-test` tas bort. `distcheck` byts mot `check` i CI, eget arbetsflöde `se-ci.yml`, kontroll att `gnucash/` är identisk med upstream. Katalogen `se/` med licensfiler. Ny baslinje (91 tester). Färdplanen och avvikelselistan skrivs om | S–M | Bygget utan GTK är oprövat på Windows och macOS |
| 1 | Mätning och arkitektur (K0) | PostgreSQL i byggmiljön, bokgenerator, mätvärden (laddtid, minne, tid per bokändring, libdbi mot PostgreSQL). `docs/design/05-serverarkitektur.md` med gränsvärdena för K1 | M | libdbi mot PostgreSQL |
| 2 | Motorn för server | Kommandotransaktion i SQL-backenden, aktör per kommando, löpnummer per registrering (`se/seq`), fasta inställningar och språk per bokvärd. Historiken till egna tabeller om K0 kräver det | M | De sista nya krokarna i motorn. Bör vara klart före upstreams frysning |
| 3 | Kommandolager v1 | Kontoplan, verifikation med flervaluta, lås, rättelse, räkenskapsår med bokslutsomföring, historik, frågor för rapporterna | L | Att motorns egenheter läcker ut i API:t |
| 4 | Bokvärd, API v1 och CLI | HTTP-server, OpenAPI, ETag, kontraktstester, `gcse` | L | Handskriven HTTP-kod |
| 5 | Webbklient | 5a: designsystem, inloggning, kontoplan, tangentbordsdriven verifikationsregistrering. 5b: rapporter, lås, rättelse, historik, räkenskapsår, inställningar | XL | Omfånget |
| 6 | Gateway | Kontrolldatabas, lokala konton, sessioner, API-nycklar, roller, företagsregister, start och stopp av bokvärdar | L | Säkerhetskod |
| K1 | Kontrollpunkt för motorn | Beslut: behåll eller byt motor bakom API:t | S | – |
| 7 | Underlag | Uppladdning, lagring, koppling till verifikation, regler för underlag på låsta verifikationer | M | – |
| 8 | Moms | Momsruta och redovisningskod per konto, momsrapport per period, eSKD-fil | M | – |
| 9 | SIE-import | Läser SIE typ 4 (4E för flytt från ett annat program, 4I för verifikationer) och typ 1–3 (saldon) enligt utgåva 4C: kontoplan, räkenskapsår, ingående balanser, verifikationer, SRU-koder, objekt. Provas mot SIE-gruppens exempelfiler. Regler för importerade verifikationers serier och nummer och för import i låst period | M–L | Hur importerade verifikationer förhåller sig till lås och obruten numrering |
| 10 | PDF och e-post | Typst-mallar för rapporter, PDF/A, e-postutskick | M | Två ställen att underhålla utseende på |
| 11 | Fakturor | Kunder, leverantörer, fakturor, betalningar, reskontra, flervaluta, nummerformat, PDF och mejl | XL | Fakturalogiken som i dag ligger i GTK-lagret |
| | **Första användbara versionen** | | | |
| 12 | SIE-export | Skriver SIE typ 4E och 1–3 enligt utgåva 4C med alla obligatoriska poster, kontrollerad med sietest.sie.se | M | |
| 13 | Realtid | Ändringsström i webbklienten, närvaro, konfliktvisning | M | |
| 14 | SSO | Inbyggd OIDC, betrodda huvuden från proxy, enhetskod för CLI, tvåfaktor för lokala konton | M | Egen säkerhetskod |
| 15 | Lokalt läge och paketering | Startprogram, Windows och macOS, installationspaket. Tauri-fönster därefter | M–L | Windows och macOS |
| 16 | SaaS | Åtskilda organisationer, självregistrering, kvoter, PostgreSQL som kontrolldatabas, containeravbild, säkerhetskopiering | L | Antal öppna företag per server |
| 17 | Systemdokumentation | BFNAR 9.2, struken 2026-11-26 i väntan på serverläget | S–M | |
| 18 | Bankimport | Filimport (CSV, camt.053, OFX), inkorg, matchning, förslag och regler. Isak beskriver förebilden | L | |
| 19 | SRU | INK2R, INK2S, NE | L | |
| 20 | BAS-årgångsbyte | Överlappet mot upstreamspårets verktyg avgörs när steget börjar | L | |
| 21 | Årsredovisning | iXBRL för K2, klient som speglar Bolagsverkets API v2.1 med inställbar adress, API-nyckel eller klientcertifikat, testad mot egen attrapp | L–XL | Taxonomin |

Beroenden: 0 → 1 → 2 → 3 → 4, därefter 5 och 6 parallellt i ordning efter behov. 7, 8, 9 och 11 kräver 5 och 6;
9 kräver också 8 (SRU-koderna), och 11 kräver 10 och K1.

**Synk mot upstream:** direkt efter steg 0, veckan efter frysningen 2027-02-07 och vid 6.0 (2027-03-28). Ett
GTK-bygge körs bara på synkgrenarna, som kontroll av att upstreams kod fortfarande bygger hos oss.

## 6. Kontrollpunkt K1: behåll eller byt motor

Ligger efter steg 6 och före fakturorna, som är den mest motorbundna delen. Mäts med samma skript som i steg 1,
på en bok med 50 000 verifikationer och 200 000 rader. Förslag på gränser (fastställs av Isak i design 05):

| Mätvärde | Föreslagen gräns |
|---|---|
| Minne per öppet företag | högst 200 MB |
| Kallstart från PostgreSQL | högst 3 s |
| Skrivkommando, 95:e percentilen | högst 150 ms |
| Rapport, 95:e percentilen | högst 500 ms |
| Köväntan med 20 samtidiga användare, 95:e percentilen | högst 100 ms |
| Öppna företag per GB minne | mäts |
| Antal krokar i upstreamkod och tid per synk | mäts |

Faller två eller fler gränser byts motorn bakom API:t.

## 7. Nya verktyg och beroenden

| Steg | Beroende | Licens | Till vad |
|---|---|---|---|
| 1 | libdbd-pgsql, libpq, PostgreSQL | LGPL, PostgreSQL | Företagets databas på server |
| 4 | Boost.Beast, Asio, JSON (finns redan) | BSL-1.0 | HTTP, SSE, JSON |
| 5 | Svelte, SvelteKit, Vite, Vitest, openapi-typescript | MIT | Webbklient och tester |
| 5 | TypeScript, Playwright | Apache-2.0 | Typer, tester i webbläsare (bara vid bygge och test) |
| 5 | Inter, Lucide | OFL-1.1, ISC | Typsnitt och ikoner |
| 6 | libsodium, SQLite | ISC, public domain | Lösenord (argon2id), slumptal; kontrolldatabas |
| 10 | Typst, libcurl | Apache-2.0, curl | PDF; e-post (SMTP) |
| 14 | OpenSSL, jwt-cpp | Apache-2.0, MIT | HTTPS-klient och kontroll av ID-token |
| 15 | Tauri (senare) | MIT/Apache-2.0 | Eget programfönster |
| Drift | Caddy eller nginx | Apache-2.0, BSD | TLS framför servern |

Apache-2.0-bibliotek går bra ihop med AGPL-3.0. Byggimagen behöver byggas om i steg 1, 6, 10 och 14; inget
kräver systempaket på värden (node 24, npm 11, podman och psql 18 finns redan).

## 8. Detaljer

**Nummerformat (räknare), byggs i steg 11:**
- Syntax med variabler i klamrar. Urvalet fastställs i designskissen utifrån vad motorn har, minst: löpnummer
  med valfri bredd (`{nr}`, `{nr:3}`), år, kort år, månad, dag, vecka, kvartal, företagets datumformat
  (`{datum}`), räkenskapsår, dokumenttyp, kund- eller leverantörsnummer.
- Varje fält får en förklaring med alla variabler, en förhandsvisning av nästa nummer och ett förifyllt
  standardvärde.
- Löpnumret fortsätter utan omstart (obruten serie). Numret tas i samma databastransaktion som fakturan, så
  att en krasch inte ger en lucka.
- Översätts till motorns printf-format i kommandolagret; ingen krok i motorn.

**Bankimport, steg 18:** inkorg i webbklienten där kontoutdragsrader matchas mot verifikationer och omatchade
rader bokförs med förslag. Kända brister hos iOrdning att undvika: rubriken kopieras ordagrant från den
tidigare verifikationen, och förslag saknas när beloppet varierar.

**Modernt utseende:** ett designsystem med gemensamma designtokens, ljust och mörkt läge, svenska och engelska,
WCAG 2.2 AA, tangentbordet först (kommandopalett, registrering utan mus).

## 9. Test

| Lager | Metod |
|---|---|
| Motor och SE | Befintliga tester (91 utan GTK), `test-gnc-se` |
| Kommandolager | gtest mot motorn med SQLite i temporär fil, en delmängd mot PostgreSQL i CI. Felinjektion: avbryt mitt i ett kommando, ladda om, boken ska vara oförändrad |
| API | Kontraktstester mot `openapi.yaml` med en riktig bokvärd |
| Webbklient | Vitest för logik, Playwright mot en riktig bokvärd med testbok |
| SIE | SIE-gruppens exempelfiler in, jämförelse av saldon och verifikationer; export kontrolleras med sietest.sie.se |
| Hela kedjan | Registrera, låsa, rätta, ta ut rapport; därefter `gnc_se_verify()` och kontroll att boken går att öppna för granskning |
| Prestanda | Bokgenerator och mätskript från steg 1, samma vid K1 |

## 10. Risker

- Det finns inget användbart SE-program från steg 0 tills webbklienten klarar kärnan (steg 5).
- Handskriven HTTP- och inloggningskod i C++ kräver säkerhetsgranskning, särskilt OIDC.
- Flervaluta, PDF och e-post i första versionen gör den klart större.
- libdbi är i praktiken ounderhållet; beteendet mot PostgreSQL prövas i steg 1.
- En tung rapport blockerar registrering i samma företag, eftersom allt går genom en motortråd. Mäts.

## 11. Beslut från 2026-11-26 som står kvar

| Fråga | Beslut |
|---|---|
| Basgren | `future` (blivande 6.0) |
| Förhållande till upstream | Fristående projekt. Upstream är startpunkt och källa för ändringar via synk; forken skickar inget tillbaka |
| Bokformat | En SE-bok används i SE. Vanlig GnuCash ska kunna öppna den för granskning, inte för arbete. Ingen feature-flagga. Ändringar utanför SE upptäcks och rapporteras som fel. Ändrat 2026-11-27: historik och liknande får flyttas ur KVP om mätningen i steg 1 kräver det, och syns då inte i vanlig GnuCash |
| Lås och numrering | Periodlåsning (ingen "Bokför direkt"); en serie per räkenskapsår som standard |
| Myndigheter | SE skriver filer (SIE, eSKD-XML, SRU) som laddas upp i myndighetens e-tjänst. Ingen nätverkskod mot Skatteverket. Ändrat 2026-11-27 för Bolagsverket: steg 21 |
| AI-märkning | Trailern `Assisted-by: Claude Code` i commits. Inget krav på `Signed-off-by` |
| BAS-data | Kontonamn ur BAS filer läggs inte in innan rättighetsfrågan är utredd |

## 12. Avgränsning mot upstreamarbetet

Isak bidrar också till `Gnucash/gnucash` i ett separat spår (egen klon, egen plan). Det som går in upstream i
motorn kommer hit via synken. Det som upstream bygger i GTK-programmet (`gnucash/`) följer med i trädet men
byggs inte här.

| Område | Upstreamspåret | Forken |
|---|---|---|
| Lås, numrering, rättelse, behandlingshistorik, räkenskapsår | Avvisat av upstream 2026-11-26, respektive inget arbete | Byggt här |
| Rättningar av sv_SE-mallarna och mall-laddaren, BAS 2026-kontomallar | Pågår respektive planerat | Kommer via synk (datafiler och motorkod) |
| Kontomatchning när kontoträd slås ihop | Buggfix planerad | Kommer via synk om den ligger i motorn |
| SIE-export och -import | Planerat upstream (mot 6.0) | Byggs här genom API:t (import steg 9, export steg 12). Upstreams kod väntas hamna i GTK-programmet, som inte byggs här; hamnar tolkningen i motorn kan den återanvändas |
| Redovisningskoder per konto | Skjutet till efter 6.0 | Egna `se/`-nycklar nu (steg 8); upstreams lösning mappas in om den kommer |
| Moms: rapport och eSKD-fil | Inget beslutat | Byggs här (steg 8) |
| SRU | Skjutet till efter 6.0 | Byggs här (steg 19) |
| Byte mellan BAS-årgångar | Planerat som fristående verktyg | Avgörs när steg 20 börjar: ett verktyg räcker |
