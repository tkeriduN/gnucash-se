# Design 07 – kommandolagret

Status: **byggt 2026-11-27** på grenen `feat/command-layer` (steg 3 i [färdplanen](../fardplan.md)). Bygger på
[design 05](05-serverarkitektur.md), avsnitt 4.2–4.5, och [design 06](06-motorn-for-server.md). Vad som är
byggt och vad som återstår står i avsnitt 8.

## 1. Vad lagret är

`se/core` är ett C++-bibliotek (`gcse-core`, AGPL-3.0-or-later) mellan HTTP-lagret och motorn. Det är det
enda som anropar motorn. Allt ovanför (bokvärden, API:t, CLI:t, testerna för hela kedjan) ser bara lagrets
egna typer, så att motorn kan bytas utan att API:t ändras (kontrollpunkt K1).

Gränssnittet är en klass, `gcse::Company`: ett öppet företag. Metoderna är kommandon (ändrar) och frågor
(läser). Huvudfilen `gcse-core.hpp` inkluderar ingenting ur motorn.

## 2. Regler för gränssnittet

| Område | Regel |
|---|---|
| Typer | Egna strukturer med text, tal och listor. Inga pekare till motorns objekt, ingen KVP, inga GUID-typer |
| Identiteter | Text som anroparen inte ska tolka (i dag motorns GUID med 32 hexadecimala tecken) |
| Belopp | Decimaltal som text, `"1234.50"`, i kontots valuta eller i bokföringsvalutan enligt fältets beskrivning. Aldrig flyttal. Lagret räknar med motorns bråktal |
| Datum | `ÅÅÅÅ-MM-DD`. Tidpunkter i sekunder sedan 1970 (API:t gör om dem till RFC 3339) |
| Fel | `gcse::Error` med en stabil kod (`voucher.unbalanced`), ett meddelande på engelska för loggar och, där det går, vilket fält felet gäller. Klienterna översätter på koden. Motorns egna meddelanden (redan översatta) följer med där felet kommer därifrån |
| Resultat | `std::expected<T, Error>`: ett värde eller ett fel, aldrig undantag över gränsen |
| Version | Varje resurs som kan ändras har en `etag`, en kontrollsumma av dess innehåll. Ändringar tar den etag anroparen utgick från och nekas med `conflict` om resursen har ändrats sedan dess |
| Listor | Sidindelade: `limit` och en markör `after`; svaret har `next` när det finns mer |

## 3. Ett företag per process

Motorn klarar en bok per process (design 05, avsnitt 4.1). `Company::open()` och `Company::create()` nekar
därför om ett företag redan är öppet i processen. Lagret sätter motorns aktuella session till företagets och
stänger av transaktionsloggen. Det är inte trådsäkert: anroparen (bokvärdens motortråd) kör ett anrop i taget.

## 4. Kommandon

Varje kommando:

1. kontrollerar allt som går att kontrollera i förväg och nekar med ett fel utan att ha rört boken;
2. sätter aktören (`gnc_se_set_actor()`), öppnar en enhet i lagringen (`gnc_se_command_begin()`), gör
   ändringen genom motorn och avslutar enheten;
3. svarar med resursen som den ser ut efteråt.

Går något fel efter att ändringen har börjat rullas lagringen tillbaka, och företaget markeras som trasigt:
alla följande anrop svarar `reload-required` tills bokvärden har startats om. Minnet kan inte rullas tillbaka
(design 05). Kontrollerna i punkt 1 finns för att det inte ska hända.

### 4.1 Vad som ingår i första versionen

| Område | Kommandon och frågor |
|---|---|
| Företag | skapa (namn, organisationsnummer, bokföringsvaluta, räkenskapsårets första dag), öppna, läsa uppgifterna |
| Kontoplan | lista, läsa, lägga upp, ändra, ta bort (bara konton utan verifikationer) |
| Verifikationer | registrera, läsa, lista med urval, ändra och ta bort (bara preliminära) |
| Bokföring | plan för låsning, låsa till och med, vad som är försenat |
| Rättelse | rättelseverifikation, med eller utan ersättande verifikation |
| Räkenskapsår | lista, lägga upp, avsluta; verifikation för årets resultat |
| Historik och kontroll | lista händelser, kontrollera serier och historik |
| Rapporter | saldobalans för en period, huvudbok för ett konto, verifikationslista |

Balansrapport och resultatrapport i svensk uppställningsform bygger på saldobalansen och på koder per konto
som kommer i steg 8; de byggs där.

### 4.2 Verifikationer och valutor

En verifikation har ett datum, en text, en serie och rader. Varje rad har ett konto och ett belopp i
**bokföringsvalutan**, positivt för debet och negativt för kredit. Summan av beloppen ska vara noll; annars
nekas verifikationen (`voucher.unbalanced`). Motorns obalanskonto används inte.

En rad på ett konto i **annan valuta** har dessutom beloppet i kontots valuta (`foreign_amount`), med samma
tecken. Kursen är kvoten mellan de två och lagras inte för sig. Kursdifferenser och omräkning vid bokslut hör
till fakturorna och bokslutet och byggs med dem.

Övriga kontroller: minst två rader, inget belopp noll, kontona finns och är inte samlingskonton, datumet är
giltigt och inte i låst period, serien finns.

En bokförd (låst) verifikation kan inte ändras eller tas bort; vakten i motorn nekar även om lagret skulle
släppa igenom det. Den rättas med en rättelseverifikation.

Raderna har en ordning, den de registrerades i. Motorn lovar inte att bevara den, och SQL-backenden gör det
inte: den läser raderna i den ordning databasen lämnar dem. Varje rad bär därför sin plats (`se-line`), och
lagret lämnar alltid raderna i den ordningen. Rader som motorn själv har skapat (i en rättelseverifikation)
saknar plats och kommer sist, i motorns ordning.

### 4.3 Kontotyper

API:ts kontotyper är `asset`, `bank`, `cash`, `receivable`, `payable`, `liability`, `equity`, `income` och
`expense`. De motsvarar motorns typer med samma innebörd. Övriga typer i motorn (värdepapper, handelskonton)
visas som `other` och kan inte väljas.

### 4.4 Årets resultat

I svensk bokföring nollställs inte resultatkontona med omföringar konto för konto. Årets resultat bokas med en
verifikation mellan ett resultatkonto (BAS 8999) och ett konto i eget kapital (BAS 2099), och rapporterna
räknar resultatkontona per räkenskapsår. `result_voucher()` räknar fram årets resultat och registrerar den
verifikationen som preliminär; den låses med årets sista låsning. Motorns "close book" används inte.

## 5. Inställningar på boken

Företagets namn, organisationsnummer och bokföringsvaluta lagras som inställningar i SE-modulen
(`gnc_se_book_get_setting()` och `gnc_se_book_set_setting()`, nycklar `se-settings:<namn>`). Varje ändring
ger en händelse i behandlingshistoriken. GnuCash egna bokalternativ används inte: de hör till GTK-programmets
dialoger.

## 6. Test

`se/core/test/test-gcse-core.cpp` går bara genom `gcse::Company`, mot en bok i SQLite och en i XML i en
tillfällig katalog, och mot PostgreSQL när en server finns (`GCSE_PG_URL`, som `util/se/pg-env` sätter). Varje
kommando provas med ett giltigt anrop och med de fel det ska neka. Boken laddas om och jämförs, och
`verify()` ska vara tom. Mot PostgreSQL kontrolleras också att företaget stänger sin anslutning.

## 7. Det som inte görs här

- HTTP, JSON och behörighet (steg 4 och 6). Lagret litar på att anroparen har kontrollerat rollen.
- Momskoder och redovisningskoder per konto (steg 8), underlag (steg 7), fakturor (steg 11), SIE (steg 9).
- Fasta inställningar för `gnc-prefs` och språk (bokvärden, steg 4).

## 8. Byggt, och det som återstår

Allt i tabellen i 4.1 är byggt: `se/core/gcse-core.hpp` (gränssnittet, utan något ur motorn) och
`gcse-core.cpp`. Testet har tio testfall som körs per lagring: 20 utan PostgreSQL, 30 med. Hela sviten: 93 av
93.

I SE-modulen tillkom inställningar på boken (`gnc_se_book_get_setting()` och `set`), radernas plats
(`gnc_se_split_get_line()` och `set`) och `gnc_se_trans_get_seq()`.

Två fel i GnuCash SQL-backend upptäcktes och hanterades:

- **Efter att en bok sparats till en ny databas** betraktar backenden fortfarande objekten som nya och
  försöker lägga in dem igen vid nästa ändring (`UNIQUE constraint failed: books.guid`). `Company::create()`
  stänger därför företaget och öppnar det igen. Ingen krok.
- **En anslutning lämnades öppen** när en bok skapades i en databas som redan fanns. Rättat med en rad i
  `gnc-backend-dbi.cpp` (i avvikelselistan).

Återstår, och hör till senare steg eller till ett beslut:

- **Felmeddelandena är på engelska** och översätts av klienterna på koden. Texten på verifikationen för årets
  resultat ("Årets resultat 2025") skrivs av lagret på svenska; den bör komma från anroparen när API:t byggs.
- **Öppning av ett företag som redan har ett lås** (efter en krasch) nekas med `storage.locked`. Att bryta
  låset hör till gatewayen, som äger låset per företag (design 05, avsnitt 4.1).
- **Listan över verifikationer går igenom alla verifikationer** för varje sida. Det räcker för de storlekar
  som är mätta (design 05); ett register per datum kan läggas till om det behövs.
- **Rapporterna räknar i bokföringsvalutan** med de belopp som står på raderna. Omräkning av konton i annan
  valuta till dagskurs görs inte.
- **`se-line` ingår inte i verifikationens kontrollsumma**, så radernas ordning på en bokförd verifikation är
  inte skyddad av kedjan (beloppen, kontona och texterna är det).
