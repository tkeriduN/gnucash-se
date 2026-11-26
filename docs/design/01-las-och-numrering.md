# Design 01 – fast lås och obruten verifikationsnumrering

Status: **skiss för Isaks granskning**, 2026-11-26. Ingen kod är skriven. Kodhänvisningar gäller
`upstream/future` `515c96a83d` (2026-11-25).

## 1. Vad lagen kräver och vad funktionen ska göra

| Krav | Källa (se `../svenska-krav.md`, avsnitt A) | Funktion i SE |
|---|---|---|
| Det bokförda ska vara varaktigt. I ett program där poster kan ändras eller raderas är ingenting bokfört förrän en aktiv åtgärd låser dem | BFNAR 2.1 med kommentar | **Låsning** ("Bokför") som en uttrycklig åtgärd. Efter den kan posten inte ändras eller raderas i SE |
| Låsning senast vid tidsgränserna i 5 kap. 2 § BFL och BFNAR kap. 3 (kontant: påföljande arbetsdag; övrigt: så snart det kan ske, med tillåten senareläggning till t.ex. 50 dagar efter månadens utgång, punkt 3.6) | BFNAR 2.1, 3 kap. | SE påminner när preliminära poster är äldre än gränsen. Ingen automatisk låsning i första versionen |
| Varje verifikation har verifikationsnummer eller annat identifieringstecken | 5 kap. 7 § BFL, BFNAR 2.3 | Varje låst transaktion får serie + löpnummer |
| Serien ska vara obruten för en tidsperiod och det ska gå att kontrollera i systemet | BFNAR 5.9 | Nummer tilldelas vid låsning; kontrollfunktion som visar luckor och avvikelser |
| Flera serier tillåtna om bokföringen förblir överskådlig; en serie kan börja om varje räkenskapsår eller löpa över flera år | BFNAR 5.10 | Serier konfigureras per bok; omstart per år eller löpande |
| Registreringsordningen ska framgå per serie | BFNAR 2.2 | Numren följer registreringsordningen inom serien |
| Rättelse av bokförd post bara genom rättelsepost med egen verifikation | BFNAR 2.17–2.18 | Redigering av låst post nekas; användaren hänvisas till rättelse (design 02) |
| Behandlingshistorik med registreringsdatum; systemändringar loggas | BFNAR 9.16 | Låsning och ändrade serieinställningar blir historikhändelser (design 03). Den här designen definierar bara gränssnittet |

Utanför den här designen: rättelseverifikationen själv (02), behandlingshistoriken (03), räkenskapsår per bok
(04). Designen ska inte stänga någon av dem.

## 2. Begrepp

- **Preliminär transaktion**: allt som inte är låst. Kan ändras och raderas fritt, precis som i GnuCash i dag.
  Har inget verifikationsnummer.
- **Bokförd (låst) transaktion**: låst av SE. Bär serie, nummer, låstidpunkt och kontrollsumma.
- **Låsdatum** per bok: alla transaktioner med bokföringsdatum (`date_posted`) till och med låsdatumet är låsta.
  Låsdatumet kan bara flyttas framåt. Skiljer sig från upstreams rullande tröskel (`autoreadonly-days`), som
  kan ändras fritt och bara gäller i gränssnittet.
- **Verifikationsserie**: kod (t.ex. `A`), beskrivning, period (löpande eller per räkenskapsår), nästa nummer.
  Varje transaktion hör till en serie; standardserie per bok, och på sikt per kontotyp eller importkälla
  (t.ex. `L` för lön, `B` för bank).
- **Registreringsordning**: `date_entered` (registreringstidpunkt, finns redan i GnuCash), med transaktionens
  GUID som avgörande vid lika tidpunkt.

## 3. Numrering vid låsning, inte vid registrering

Program som numrerar när en verifikation sparas måste förbjuda radering av preliminära poster, annars blir det
luckor. Det går inte ihop med att GnuCash fritt låter användaren radera och ändra, och det går inte att
garantera när boken också öppnas i vanlig GnuCash.

SE gör därför så här: vid låsning till datum D tas alla preliminära transaktioner med `date_posted` ≤ D, de
grupperas per serie och sorteras i registreringsordning, och nästa nummer i serien tilldelas i den ordningen.
Raderade preliminära poster har då aldrig fått något nummer, och serien blir obruten av konstruktion.

Konsekvenser:
- Preliminära poster saknar nummer. Registret visar "preliminär" i stället. Det är förenligt med BFNAR 2.1:
  preliminära poster är inte bokförda.
- Efter låsning till D nekar SE nya transaktioner med `date_posted` ≤ D. Det som upptäcks senare bokförs i
  öppen period (BFNAR 2.18 kommentar), eller som rättelse.
- Den som vill numrera direkt kan låsa varje transaktion när den sparas ("Bokför direkt"). Det är samma
  mekanism med D = transaktionens datum, men kräver att transaktionerna registreras i datumordning. Se öppen
  fråga 3.

## 4. Befintliga mekanismer i `future`

| Mekanism | Var | Vad den gör | Användning i SE |
|---|---|---|---|
| `xaccTransSetReadOnly` / `xaccTransGetReadOnly` / `xaccTransClearReadOnly`, KVP `trans-read-only` (en text med orsaken) | `libgnucash/engine/Transaction.cpp` | `xaccTransDestroy` vägrar radera; `xaccSplitDestroy` vägrar ta bort splits; `xaccTransVoid` vägrar; registret (`split-register-model.c`, `gnc-split-reg.c`) visar orsaken och vägrar redigera | **Sätts även av SE** vid låsning, så att vanlig GnuCash skyddar posten i gränssnittet (se 6.2) |
| `xaccTransIsReadonlyByPostedDate`, `qof_book_get_num_days_autoreadonly` | `Transaction.cpp`, `qofbook.cpp` | Rullande tröskel "i dag minus N dagar", bara i gränssnittet | Används inte. SE har ett eget fast låsdatum |
| Bokräknare `qof_book_get_counter`, `qof_book_increment_and_format_counter`, KVP `counters/<namn>` och `counter_formats/<namn>` | `libgnucash/engine/qofbook.cpp` | Generiska räknare med format, används för fakturanummer m.m. | Används **inte** för serierna: de kan ändras i bokinställningarna i vanlig GnuCash. SE håller nästa nummer i egna nycklar och kontrollerar mot de låsta transaktionerna |
| `num`-fältet / split-action (`qof_book_use_split_action_for_num_field`) | `Transaction.cpp`, `qofbook.cpp` | Fritt nummerfält | SE skriver det formaterade verifikationsnumret (`A 12`) i transaktionens `num` vid låsning, för visning, utskrift och SIE. Sanningen ligger i SE:s egna nycklar |
| `date_entered`, `xaccTransGetDateEntered` | `Transaction.cpp` | Registreringstidpunkt | Registreringsordning (kan sättas av import med `xaccTransSetDateEnteredSecs`; acceptabelt) |
| `xaccTransReverse`, KVP `reversed-by` | `Transaction.cpp` | Klonar och negerar; sätter `reversed-by` på originalet, ingen länk tillbaka | Underlag för rättelse (design 02). Observera att funktionen redigerar originalet, så låsspärren måste tillåta just den nyckeln |
| Transaktionsloggen `xaccTransWriteLog` (`*.log`) | `libgnucash/engine/TransLog.cpp` | Logg för återställning efter krasch, inte en del av boken | Inte behandlingshistorik. Design 03 |
| `gnc-features` | `libgnucash/engine/gnc-features.cpp` | Får vanlig GnuCash att vägra öppna boken | Används **inte** (beslut: nedåtkompatibelt) |

### Fallgropar i upstream

- **`ScrubBusiness.c`** (`gncScrubBusinessSplit`): en skrivskyddad, icke makulerad transaktion av typ
  `TXN_TYPE_NONE` som ligger i en lot (kundfordringar och leverantörsskulder) tolkas som en dubbelbokförd
  faktura. Skrivskyddet rensas, memot skrivs om till "Please delete this transaction" och splitten tas ur
  loten. Vanlig GnuCash kan alltså lyfta skyddet från en manuell, låst transaktion mot 1510 eller 2440 vid
  "Kontrollera och reparera" (kontoträdet, `gncScrubBusinessAccount`, och registret för en enskild
  transaktion i ett reskontrakonto). SE måste stänga av den grenen för SE-låsta transaktioner (en krok), och
  upptäckten (6.3) måste fånga det när det händer i vanlig GnuCash.
- **Fakturor**: när en faktura bokförs sätts skrivskydd med orsaken "Generated from an invoice…", och
  "Avbokför" rensar det (`gncInvoice.c`). SE får inte skriva över orsaken på sådana transaktioner, och måste
  neka avbokföring av en faktura vars transaktion är låst.
- **Logguppspelning** (`gnc-log-replay.cpp`) rensar och återställer skrivskydd. Uppspelning av en logg in i en
  bok med låsta poster måste gå genom samma spärr.
- `xaccTransBeginEdit` returnerar `void` och kan inte neka. Spärren måste sitta i `xaccTransCommitEdit`
  (rulla tillbaka med `xaccTransRollbackEdit`) och i radering.

## 5. Var spärren sitter

| Alternativ | Skydd | Avvikelse mot upstream |
|---|---|---|
| A. Bara gränssnitt (som `autoreadonly`) | Svagt: Python-bindningar, import, scrub och skript går förbi | Litet, men spritt över många GUI-filer |
| B. Spärr i motorn | Starkt i SE, oavsett vem som anropar | En krok i `xaccTransCommitEdit`, en i `xaccTransDestroy`/`xaccSplitDestroy`, en i `ScrubBusiness.c` |
| C. Som B, plus upstreams `trans-read-only` | Som B, och dessutom gränssnittsskydd i vanlig GnuCash | Som B |

**Rekommendation: C.**

Spärrens innehåll: vid `xaccTransCommitEdit` på en SE-låst transaktion jämförs de skyddade fälten med
klonen `trans->orig`. Har något skyddat fält ändrats rullas ändringen tillbaka (`xaccTransRollbackEdit`) och
ett fel rapporteras. Skyddade fält: datum (bokförings- och registrering), beskrivning, `num`, valuta, och per
split konto, värde, mängd, memo och action. Fritt efter låsning: avstämningsstatus och avstämningsdatum, lot,
dokumentlänk, anteckningar, `reversed-by`. Det är den praxis som gäller i svenska program: avstämning ändrar
inte bokföringsposten.

Kod: ett eget bibliotek `libgnucash/se/` (`gnc-se`), som länkar mot `gnc-engine`. Motorn får ett enda
anropsställe: en vakt som är tom i vanlig GnuCash och registreras av `gnc-se` vid uppstart:

```c
/* Transaction.h, forkens enda krok i motorns gränssnitt */
typedef gboolean (*GncTransCommitGuard)(const Transaction *trans);
void gnc_trans_set_commit_guard (GncTransCommitGuard guard);
```

Med en vakt i stället för SE-logik i `Transaction.cpp` blir synkkonflikterna små, och låslogiken går att
testa för sig. Allt annat ligger i `libgnucash/se/`: låsning, serier, kontroll, kontrollsummor, KVP-åtkomst
via egna accessorer (6.0-linjen: KVP bara via accessorer).

## 6. Lagring och upptäckt

### 6.1 KVP-nycklar (prefix `se/`)

På boken:

| Nyckel | Typ | Innehåll |
|---|---|---|
| `se/lock-date` | datum | låsdatum D |
| `se/series/<kod>/description` | text | t.ex. "Löpande verifikationer" |
| `se/series/<kod>/period` | text | `continuous` eller `fiscal-year` |
| `se/series/<kod>/next` | heltal | nästa nummer (per år om `fiscal-year`: `se/series/<kod>/<år>/next`) |
| `se/series/<kod>/head` | text | kontrollsumma för seriens senast låsta transaktion (6.3) |
| `se/default-series` | text | standardserie |
| `se/version` | heltal | formatversion för SE:s nycklar |

På transaktionen:

| Nyckel | Typ | Innehåll |
|---|---|---|
| `se/series` | text | serie (sätts redan på den preliminära posten, ändringsbar tills låsning) |
| `se/number` | heltal | löpnummer |
| `se/locked-at` | tid | låstidpunkt |
| `se/hash` | text | kontrollsumma (6.3) |

Plus upstreams `trans-read-only` med orsaken "Bokförd i GnuCash SE (A 12). Rätta med en rättelseverifikation."
när transaktionen inte redan har en orsak från fakturadelen.

KVP fungerar likadant i XML-backend och SQL-backend (tabellen `slots`). Vanlig GnuCash läser och skriver
tillbaka okända slots orörda.

### 6.2 Vad vanlig GnuCash gör med en SE-bok

- Öppnar boken utan varning.
- Registret vägrar redigera och radera låsta transaktioner (tack vare `trans-read-only`).
- Går förbi skyddet via: "Kontrollera och reparera" (fallgropen i 4), Python-bindningar, logguppspelning,
  redigering av XML-filen eller databasen, och ändring av nya transaktioner med datum före låsdatumet (vanlig
  GnuCash känner inte till `se/lock-date`).

### 6.3 Kontrollsummekedja och kontroll

För varje serie bildas en kedja: `h(n) = SHA-256(h(n−1) ‖ kanonisk(trans n))`, med `h(0)` härledd ur bokens
GUID och seriekoden. `kanonisk` är en fast, versionerad serialisering av de skyddade fälten (5) i bestämd
ordning (splits sorterade på GUID), med belopp som exakta bråk. Hashen lagras i `se/hash` på transaktionen och
seriens senaste i `se/series/<kod>/head`.

Kontrollen körs när en bok öppnas i SE och på begäran ("Kontrollera verifikationsserier"). Den hittar:
- luckor och dubbletter i numreringen (raderad eller ändrad post),
- transaktioner vars skyddade fält har ändrats (hashen stämmer inte),
- transaktioner med `date_posted` ≤ låsdatumet som saknar nummer (skapade i vanlig GnuCash),
- låsta transaktioner som saknar `trans-read-only` (scrub-fallgropen).

Vid avvikelse lagar SE ingenting tyst. Den visar vad som avviker, för in avvikelsen i behandlingshistoriken
(03) när den finns, och låter användaren hantera det: rättelse, eller låsning av de nyupptäckta posterna i
öppen period.

**Gräns:** en öppen fil kan aldrig göras manipuleringssäker. Den som skriver om boken och räknar om kedjan
kommer förbi kontrollen. Kedjan skyddar mot misstag och mot ändringar i vanlig GnuCash, inte mot avsiktligt
fusk. Ett yttre ankare, t.ex. seriens `head` i utskrift, SIE-export eller en signerad historikfil, kan
läggas till senare. Det ska stå i systemdokumentationen (BFNAR 9.2) som SE genererar.

SHA-256: GLib `GChecksum` finns redan i beroendena. Om 6.0-linjen med mindre GLib gör den olämplig i
motornära kod finns andra vägar (en liten inbyggd implementation). Beslutas när koden skrivs.

## 7. Gränssnitt (först i en senare version, efter motorn)

- **Bokinställningar**, flik "GnuCash SE": serier, standardserie, periodtyp.
- **Åtgärder → Bokför till och med…**: välj D, se antal preliminära poster per serie, bekräfta. Låsningen sker
  i en enda redigeringsomgång och sparas direkt.
- **Registret**: kolumnen "Num" visar `A 12` eller "preliminär". Försök att redigera en låst post ger orsaken
  och en knapp till rättelse (02).
- **Påminnelse** när det finns preliminära poster äldre än tidsgränsen (1).

## 8. Testplan

- gtest i `libgnucash/se/test/`:
  - låsning numrerar i registreringsordning per serie; raderade preliminära poster ger inga luckor;
  - låsdatumet kan inte flyttas bakåt;
  - vakten rullar tillbaka ändring av varje skyddat fält, och släpper igenom avstämning, lot och
    `reversed-by`;
  - radering och borttagning av split nekas;
  - kontrollsummekedjan: oförändrad bok ger ingen avvikelse; varje typ av avvikelse i 6.3 upptäcks;
  - kanonisk serialisering är stabil (gyllene värden).
- Rundtur: lås, spara och läs om i både XML och SQLite; alla `se/`-nycklar och `trans-read-only` kvar.
- Nedåtkompatibilitet: öppna en SE-bok med upstreams motor (samma bygge med vakten avregistrerad, och
  `future` utan SE), gör ändringar via motorn och kör scrub. Läs sedan in boken i SE och kontrollera att
  varje ändring upptäcks.
- Hela upstreams testsvit ska vara lika grön som baslinjen (140/140 på `future`).

## 9. Öppna frågor till Isak

1. **Serier som standard**: en serie (`A`) för allt, eller färdiga serier efter vanlig svensk praxis? Förslag:
   en serie `A` som standard, fler kan läggas till.
2. **Löpande eller per räkenskapsår**: förslag per räkenskapsår (vanligast i svenska program; SIE:s `#VER`
   bär serie och nummer per räkenskapsårsfil). Då
   behövs räkenskapsåret innan design 04 är klar: till att börja med kalenderår, eller ett räkenskapsår som
   anges i bokinställningarna.
3. **"Bokför direkt"** (låsning när en transaktion sparas) i första versionen, eller bara periodlåsning?
   Förslag: bara periodlåsning först.
4. **Befintliga böcker**: när en befintlig bok låses första gången numreras hela historiken till och med D.
   Ska den gamla `num`-texten sparas undan (t.ex. i `se/former-num`), eller räcker det att den skrivs över?
   Förslag: sparas undan.
5. **Upstreams `trans-read-only`** (alternativ C): okej att SE sätter upstreams nyckel? Det är det enda som
   ger skydd i vanlig GnuCash, men det är också en "ändrat beteende"-avvikelse.
6. **Nya transaktioner före låsdatumet i vanlig GnuCash**: SE upptäcker dem (6.3). Ska de låsas in i en senare
   serieposition med en historikanteckning, eller räknas som fel som användaren måste ta bort eller flytta?
