# Design 01 – fast lås och obruten verifikationsnumrering

Status: **steg 1 byggt 2026-11-26**: motorn i `libgnucash/se/` och gränssnittet i `gnucash/gnome/gnc-plugin-se.cpp`
(avsnitt 10 visar vad som finns och vad som återstår).
Skissen granskades av Isak 2026-11-26; besluten står i avsnitt 9. Kodhänvisningar gäller
`upstream/future` `515c96a83d` (2026-11-25).

## 1. Vad lagen kräver och vad funktionen ska göra

| Krav | Källa (se `../svenska-krav.md`, avsnitt A) | Funktion i SE |
|---|---|---|
| Det bokförda ska vara varaktigt. I ett program där poster kan ändras eller raderas är ingenting bokfört förrän en aktiv åtgärd låser dem | BFNAR 2.1 med kommentar | **Låsning** ("Bokför") som en uttrycklig åtgärd. Efter den kan posten inte ändras eller raderas i SE |
| Låsning senast vid tidsgränserna i 5 kap. 2 § BFL och BFNAR kap. 3 (se 3.1) | BFNAR 2.1, 3 kap. | Periodlåsning. SE påminner när preliminära poster är äldre än gränsen. Ingen automatisk låsning i första versionen |
| Varje verifikation har verifikationsnummer eller annat identifieringstecken | 5 kap. 7 § BFL, BFNAR 2.3 | Varje låst transaktion får serie + löpnummer |
| Serien ska vara obruten för en tidsperiod och det ska gå att kontrollera i systemet | BFNAR 5.9 | Nummer tilldelas vid låsning; kontrollfunktion som visar luckor och avvikelser |
| Flera serier tillåtna om bokföringen förblir överskådlig; en serie kan börja om varje räkenskapsår eller löpa över flera år | BFNAR 5.10 | Standard: en serie som börjar om varje räkenskapsår. Fler serier och löpande serier kan läggas till |
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
  Låsdatumet kan bara flyttas framåt. Skiljer sig från den ärvda rullande tröskeln (`autoreadonly-days`), som
  kan ändras fritt och bara gäller i gränssnittet.
- **Verifikationsserie**: kod (t.ex. `A`), beskrivning, period (löpande eller per räkenskapsår), nästa nummer.
  Varje transaktion hör till en serie; standardserie per bok, och på sikt per kontotyp eller importkälla
  (t.ex. `L` för lön, `B` för bank).
- **Registreringsordning**: `date_entered` (registreringstidpunkt, finns redan i GnuCash), med transaktionens
  GUID som avgörande vid lika tidpunkt.

## 3. Låsning per period, numrering vid låsning

### 3.1 Periodlåsning räcker enligt reglerna

Kontrollerat 2026-11-26 i BFN:s vägledning Bokföring, VL 13:2 (uppdaterad 2024-09-16):

- **Preliminär bokföring är tillåten.** Kommentaren till punkt 2.1 (s. 15): "Så länge bokföringen inte är låst,
  kan affärshändelserna inte anses bokförda. Bokföringen måste låsas så att den blir varaktig senast vid den
  tidpunkt då affärshändelserna ska vara bokförda enligt bestämmelserna i kapitel 3." Inget krav på att varje
  post låses när den registreras. Låsningen får göras samlat, så länge den sker i tid.
- **Tidsgränserna** (kapitel 3):
  - *Kontanta in- och utbetalningar*: senast påföljande arbetsdag (5 kap. 2 § BFL, punkterna 3.1–3.2, s. 33).
    "Kontanta" betyder sedlar och mynt, postväxlar, mottagna checkar och liknande (punkt 1.9, s. 12), inte
    bank- eller plusgiro, kontokort eller företagets egna checkar. För kontanta betalningar får bara
    presentationen i systematisk ordning senareläggas. Registreringsordningen ska vara bokförd senast
    påföljande arbetsdag (punkt 3.6 andra stycket med kommentar, s. 35), utom vid certifierat kassaregister
    (punkt 3.10).
  - *Övriga affärshändelser*: så snart det kan ske (3.3). Fakturor får vänta tills fakturan har utfärdats
    eller tagits emot (3.4). Bokföringen får senareläggas till 50 dagar efter månadens utgång (3.6), eller
    kvartalets vid nettoomsättning högst 3 mkr (3.7), om verifikationerna under tiden ordnas i takt med att de
    kommer in (3.5).
- **Verifikationsnumret** "ska anges på ett varaktigt sätt" (kommentaren till 5.9–5.10, s. 52, med hänvisning
  till 5.4). Jag har inte hittat något i vägledningen som kräver att numret sätts redan vid registreringen.

**Slutsats:** periodlåsning uppfyller kraven. Ett företag med kontanta betalningar måste låsa senast
påföljande arbetsdag, i praktiken "Bokför till och med i går" varje arbetsdag. SE påminner om det (7).
"Bokför direkt", alltså låsning när en transaktion sparas, behövs inte och byggs inte.

### 3.2 Numrering vid låsning

Program som numrerar när en verifikation sparas måste förbjuda radering av preliminära poster, annars blir det
luckor. Det går inte ihop med att GnuCash fritt låter användaren radera och ändra preliminära poster.

SE gör därför så här: vid låsning till datum D tas alla preliminära transaktioner med `date_posted` ≤ D, de
grupperas per serie och sorteras i registreringsordning, och nästa nummer i serien tilldelas i den ordningen.
Raderade preliminära poster har då aldrig fått något nummer, och serien blir obruten av konstruktion.

Konsekvenser:
- Preliminära poster saknar nummer. Registret visar "preliminär" i stället. Det är förenligt med BFNAR 2.1:
  preliminära poster är inte bokförda.
- Efter låsning till D tar SE inte emot nya transaktioner med `date_posted` ≤ D: registret vägrar datumet, och
  det som kommer in på annat sätt hindrar nästa låsning (se 5 och 7). Det som upptäcks senare bokförs i öppen
  period (BFNAR 2.18 kommentar), eller som rättelse.
- Pappersunderlag får sitt verifikationsnummer efter låsningen. Fram till dess ordnas de i den ordning de
  kommer in (punkt 3.5).
- Har transaktionen redan en text i `num` när den låses (t.ex. i en bok som funnits före SE) sparas den undan
  i `se/former-num` innan verifikationsnumret skrivs dit.

## 4. Befintliga mekanismer i `future`

| Mekanism | Var | Vad den gör | Användning i SE |
|---|---|---|---|
| `xaccTransSetReadOnly` / `xaccTransGetReadOnly` / `xaccTransClearReadOnly`, KVP `trans-read-only` (en text med orsaken) | `libgnucash/engine/Transaction.cpp` | `xaccTransDestroy` vägrar radera; `xaccSplitDestroy` vägrar ta bort splits; `xaccTransVoid` vägrar; registret (`split-register-model.c`, `gnc-split-reg.c`) visar orsaken och vägrar redigera | **Sätts även av SE** vid låsning. Då hanterar den ärvda koden (registret, radering, makulering) låsta poster utan ny gränssnittskod, och en bok som öppnas i vanlig GnuCash för granskning skyddas mot oavsiktliga ändringar |
| `xaccTransIsReadonlyByPostedDate`, `qof_book_get_num_days_autoreadonly` | `Transaction.cpp`, `qofbook.cpp` | Rullande tröskel "i dag minus N dagar", bara i gränssnittet | Används inte. SE har ett eget fast låsdatum |
| Bokräknare `qof_book_get_counter`, `qof_book_increment_and_format_counter`, KVP `counters/<namn>` och `counter_formats/<namn>` | `libgnucash/engine/qofbook.cpp` | Generiska räknare med format, används för fakturanummer m.m. | Används **inte** för serierna: de kan ändras fritt i bokens egenskaper (avsnittet Counters). SE håller nästa nummer i egna nycklar och kontrollerar mot de låsta transaktionerna |
| `num`-fältet / split-action (`qof_book_use_split_action_for_num_field`) | `Transaction.cpp`, `qofbook.cpp` | Fritt nummerfält | SE skriver det formaterade verifikationsnumret (`A 12`) i transaktionens `num` vid låsning, för visning, utskrift och SIE. Sanningen ligger i SE:s egna nycklar |
| `date_entered`, `xaccTransGetDateEntered` | `Transaction.cpp` | Registreringstidpunkt | Registreringsordning (kan sättas av import med `xaccTransSetDateEnteredSecs`; acceptabelt) |
| `xaccTransReverse`, KVP `reversed-by` | `Transaction.cpp` | Klonar och negerar; sätter `reversed-by` på originalet, ingen länk tillbaka | Underlag för rättelse (design 02). Observera att funktionen redigerar originalet, så låsspärren måste tillåta just den nyckeln |
| Transaktionsloggen `xaccTransWriteLog` (`*.log`) | `libgnucash/engine/TransLog.cpp` | Logg för återställning efter krasch, inte en del av boken | Inte behandlingshistorik. Design 03 |
| `gnc-features` | `libgnucash/engine/gnc-features.cpp`, kontrollen i `gnucash/gnome-utils/gnc-file.c` | En okänd funktion i boken får vanlig GnuCash att visa ett fel och inte öppna boken alls, inte ens för granskning | Används **inte**: granskning i vanlig GnuCash ska fungera (beslut 2026-11-26) |

### Fallgropar i den ärvda koden

- **`ScrubBusiness.c`** (`gncScrubBusinessSplit`): en skrivskyddad, icke makulerad transaktion av typ
  `TXN_TYPE_NONE` som ligger i en lot (kundfordringar och leverantörsskulder) tolkas som en dubbelbokförd
  faktura. Skrivskyddet rensas, memot skrivs om till "Please delete this transaction" och splitten tas ur
  loten. Vanlig GnuCash kan alltså lyfta skyddet från en manuell, låst transaktion mot 1510 eller 2440 vid
  "Kontrollera och reparera" (kontoträdet, `gncScrubBusinessAccount`, och registret för en enskild
  transaktion i ett reskontrakonto). I SE stoppar vakten (5) både rensningen av skrivskyddet och
  memoändringen, så ingen särskild krok behövs i `ScrubBusiness.c`. Kontrollen (6.3) fångar det när det har
  hänt i vanlig GnuCash.
- **Fakturor**: när en faktura bokförs sätts skrivskydd med orsaken "Generated from an invoice…", och
  "Avbokför" rensar det (`gncInvoice.c`). SE får inte skriva över orsaken på sådana transaktioner, och måste
  neka avbokföring av en faktura vars transaktion är låst.
- **Logguppspelning** (`gnc-log-replay.cpp`) rensar och återställer skrivskydd. Uppspelning av en logg in i en
  bok med låsta poster måste gå genom samma spärr.
- `xaccTransBeginEdit` returnerar `void` och kan inte neka. Spärren måste sitta i `xaccTransCommitEdit`
  (rulla tillbaka med `xaccTransRollbackEdit`) och i radering.
- **Inläsning ser ut som redigering.** En backend som läser en bok skapar varje transaktion med
  `xaccTransBeginEdit`/`xaccTransCommitEdit`. Motorn hade ingen uppgift om att en inläsning pågår, så forken
  lägger till `qof_session_load_in_progress()`.
- **`xaccTransSetDatePostedGDate` skrev datumet till KVP utanför redigeringsomgången.** Ett nekat datumbyte
  rullades då bara tillbaka till hälften. Rättat i forken (hittat av testerna).
- **Radering av ett konto** tar bort kontots splits även ur skrivskyddade transaktioner
  (`xaccSplitDestroy` släpper igenom när kontot håller på att raderas). Se avsnitt 10.

## 5. Var spärren sitter

| Alternativ | Skydd | Ändring i ärvd kod |
|---|---|---|
| A. Bara gränssnitt (som `autoreadonly`) | Svagt: Python-bindningar, import, scrub och skript går förbi | Litet, men spritt över många GUI-filer |
| B. Spärr i motorn | Starkt i SE, oavsett vem som anropar | En krok i `xaccTransCommitEdit` (radering går också den vägen), en uppgift om pågående inläsning i `qofsession.cpp` |
| C. Som B, plus den ärvda nyckeln `trans-read-only` | Som B. Dessutom fungerar det ärvda skyddet i register, radering och makulering utan ny kod, och vid granskning i vanlig GnuCash | Som B |

**Rekommendation: C.**

Spärrens innehåll: vid `xaccTransCommitEdit` på en SE-låst transaktion jämförs de skyddade fälten med
klonen `trans->orig`. Har något skyddat fält ändrats rullas ändringen tillbaka (`xaccTransRollbackEdit`) och
ett fel rapporteras. Skyddade fält: datum (bokförings- och registrering), beskrivning, `num`, valuta, och per
split konto, värde, mängd, memo och action, samt SE:s egna nycklar och att `trans-read-only` finns kvar. Fritt efter låsning: avstämningsstatus och avstämningsdatum, lot,
dokumentlänk, anteckningar, `reversed-by`. Det är den praxis som gäller i svenska program: avstämning ändrar
inte bokföringsposten.

Kod: egna källfiler i `libgnucash/se/`, som kompileras in i motorbiblioteket `gnc-engine` (ett separat
bibliotek kan inte startas av motorn utan cirkelberoende). Motorns egen kod får ett anropsställe för spärren:
en vakt som `gnc_se_init()` sätter när motorn startas:

```c
/* Transaction.h */
typedef gboolean (*GncTransCommitGuard) (Transaction *trans, const Transaction *orig);
void gnc_trans_set_commit_guard (GncTransCommitGuard guard);
```

Vaktens regler (`commit_guard` i `gnc-se.cpp`), i den här ordningen:

1. Pågår en inläsning släpps allt igenom.
2. Var transaktionen låst när redigeringen började: nej till radering, till borttaget skrivskydd och till
   varje ändring av skyddade fält.
3. Har transaktionen fått ett nummer under redigeringen: ja bara om det är `gnc_se_lock_through()` som låser.
4. Annars (preliminär): allt är tillåtet utom att *flytta in* transaktionen i låst period. Dit räknas också
   att återställa en makulerad transaktion som är daterad där. Mallar för schemalagda transaktioner,
   makulerade transaktioner och transaktioner utan splits räknas inte.

En transaktion som håller på att skapas nekas inte, även om den dateras i låst period. Koden som skapar den
(import, schemalagda transaktioner, fakturor) fortsätter att använda den efter sin commit, och en återställning
av något som aldrig har sparats lämnar en tom transaktion som sedan frigörs under fötterna på den koden. Sådana
transaktioner stoppas i stället före: registret vägrar datumet (se 7), schemalagda transaktioner skapas inte,
och det som ändå kommer in (t.ex. en bankimport som överlappar låst period) hittas av kontrollen och hindrar
nästa låsning tills det har flyttats eller tagits bort.

Vakten är inbyggd i motorbiblioteket och sätts av `gnc_engine_init()`. Den gäller därför i programmet, i
`gnucash-cli` och i Python- och Guile-bindningarna.

Med en vakt i stället för SE-logik i `Transaction.cpp` blir konflikterna vid synk mot upstream små, och låslogiken går att
testa för sig. Allt annat ligger i `libgnucash/se/`: låsning, serier, kontroll, kontrollsummor, KVP-åtkomst
via egna accessorer (6.0-linjen: KVP bara via accessorer).

## 6. Lagring och upptäckt

### 6.1 KVP-nycklar (prefix `se/`)

På boken:

| Nyckel | Typ | Innehåll |
|---|---|---|
| `se/lock-date` | datum | låsdatum D |
| `se/fiscal-year-start` | text | räkenskapsårets första dag som `MM-DD`, standard `01-01` (brutet räkenskapsår går att ange; förlängt och förkortat år kommer med design 04) |
| `se/series/<kod>/description` | text | t.ex. "Verifikationer" (standardserien `A`) |
| `se/series/<kod>/period` | text | `fiscal-year` (standard) eller `continuous` |
| `se/series/<kod>/periods/<period>/next` | heltal | nästa nummer i perioden. `<period>` är året då räkenskapsåret börjar (t.ex. `2025`), eller `all` för en löpande serie |
| `se/series/<kod>/periods/<period>/head` | text | kontrollsumma för periodens senast låsta transaktion (6.3) |
| `se/default-series` | text | standardserie |
| `se/version` | heltal | formatversion för SE:s nycklar |

På transaktionen:

| Nyckel | Typ | Innehåll |
|---|---|---|
| `se/series` | text | serie (sätts redan på den preliminära posten, ändringsbar tills låsning) |
| `se/period` | text | perioden inom serien som numret hör till (sätts vid låsning) |
| `se/number` | heltal | löpnummer |
| `se/locked-at` | tid | låstidpunkt |
| `se/hash` | text | kontrollsumma (6.3) |
| `se/former-num` | text | `num`-texten före låsningen, om det fanns någon |

Plus den ärvda nyckeln `trans-read-only` med orsaken "Bokförd i GnuCash SE (A 12). Rätta med en rättelseverifikation."
när transaktionen inte redan har en orsak från fakturadelen.

KVP fungerar likadant i XML-backend och SQL-backend (tabellen `slots`). Vanlig GnuCash läser och skriver
tillbaka okända slots orörda.

### 6.2 SE-böcker och vanlig GnuCash

En bok som har skapats eller öppnats i SE är en SE-bok. Den används i SE. Vanlig GnuCash ska kunna öppna den
för att granska innehållet, men att arbeta i samma bok i både GnuCash och SE stöds inte (beslut 2026-11-26).

- SE sätter därför ingen feature-flagga (då skulle vanlig GnuCash vägra öppna boken helt, se 4).
- Registret i vanlig GnuCash vägrar ändra och radera låsta poster tack vare `trans-read-only`.
- Det som ändå ändras utanför SE (t.ex. genom "Kontrollera och reparera", skript eller redigering av filen,
  eller nya transaktioner före låsdatumet) hittas av kontrollen i 6.3 och räknas som fel. SE har inget
  arbetsflöde för att ta in sådana ändringar.

### 6.3 Kontrollsummekedja och kontroll

För varje serie och period bildas en kedja: `h(n) = SHA-256(h(n−1) ‖ kanonisk(trans n))`, med `h(0)` härledd
ur bokens GUID, seriekoden och perioden. `kanonisk` är en fast, versionerad serialisering av transaktionens
GUID, serie, period och nummer följt av de skyddade fälten (5) i bestämd ordning (splits sorterade på GUID),
med belopp som förkortade bråk och varje fält föregånget av sin längd. Hashen lagras i `se/hash` på
transaktionen och periodens senaste i `…/periods/<period>/head`. Ett test med fasta GUID:n låser formatet:
ändras serialiseringen ändras testets hash, och då behövs en ny formatversion.

Varje transaktion kontrolleras mot den hash som är lagrad på föregående transaktion. En ändrad transaktion
rapporteras därför som en avvikelse, inte som att resten av serien är fel.

Kontrollen körs när en bok öppnas i SE och på begäran ("Kontrollera verifikationsserier"). Den hittar:
- luckor och dubbletter i numreringen (raderad eller ändrad post),
- transaktioner vars skyddade fält har ändrats (hashen stämmer inte),
- transaktioner med `date_posted` ≤ låsdatumet som saknar nummer (skapade utanför SE),
- låsta transaktioner som saknar `trans-read-only` (scrub-fallgropen).

Vid avvikelse lagar SE ingenting tyst. Den visar vad som avviker och för in avvikelsen i
behandlingshistoriken (03) när den finns. Användaren rättar: en ändrad låst post återställs eller rättas med
rättelseverifikation, och en post som skapats utanför SE före låsdatumet tas bort och registreras på nytt i
öppen period.

**Gräns:** en öppen fil kan aldrig göras manipuleringssäker. Den som skriver om boken och räknar om kedjan
kommer förbi kontrollen. Kedjan skyddar mot misstag och mot ändringar utanför SE, inte mot avsiktligt
fusk. Ett yttre ankare, t.ex. seriens `head` i utskrift, SIE-export eller en signerad historikfil, kan
läggas till senare. Det ska stå i systemdokumentationen (BFNAR 9.2) som SE genererar.

SHA-256: GLib `GChecksum` finns redan i beroendena. Om 6.0-linjen med mindre GLib gör den olämplig i
motornära kod finns andra vägar (en liten inbyggd implementation). Beslutas när koden skrivs.

## 7. Gränssnitt

Menyn **Åtgärder → Bokföring** (`gnucash/gnome/gnc-plugin-se.cpp`, `gnucash/ui/gnc-plugin-se.ui`):

- **Bokför till och med…** Dialogen föreslår förra månadens sista dag, visar hur många transaktioner som
  bokförs och vilka nummer de får per serie (`gnc_se_plan_lock()`), och säger varför det inte går när det inte
  går. Innan dialogen öppnas körs kontrollen: på en serie som redan är bruten numreras inget. Efter bokföringen
  påminns användaren om att spara boken.
- **Kontrollera verifikationsserier** kör `gnc_se_verify()` och listar avvikelserna i klartext.
- **Verifikationsserier och räkenskapsår…** Räkenskapsårets första dag, senareläggning (50 dagar efter månadens
  eller kvartalets utgång), serierna, standardserie och ny serie. En serie kan inte tas bort eller ändras.

När en SE-bok öppnas körs kontrollen, och användaren påminns om preliminära transaktioner som har passerat
tidsgränsen (3.1): kontanta betalningar (konton av typen Kontant) påföljande arbetsdag, övriga enligt bokens
senareläggning. Helgdagar är inte kända; lördag och söndag räknas inte som arbetsdagar.

**Registret** får sitt skydd från den ärvda mekanismen för skrivskyddströskel (`qof_book_uses_autoreadonly()`,
`qof_book_get_autoreadonly_gdate()`), som nu också räknar med låsdatumet:

- rader till och med låsdatumet går inte att redigera, och den röda linjen i registret visar var den låsta
  perioden slutar;
- ett datum i låst period i en ny eller duplicerad transaktion nekas med besked, och datumet sätts till första
  öppna dag;
- låsta transaktioner visar sitt verifikationsnummer (`A 12`) i kolumnen Num. Preliminära har sin egen
  `num`-text eller ingen. Ordet "preliminär" visas inte: fältet är användarens fram till låsningen.

Övrigt: en faktura vars transaktion är låst går inte att avregistrera (besked med hänvisning till
kreditnotering), och en schemalagd transaktion som skulle hamna i låst period skapas inte (besked i dialogen
"Sedan senaste körning").

Texterna är engelska i koden och översatta till svenska sist i `po/sv.po`.

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
- Granskning i vanlig GnuCash: öppna en SE-bok med motorn utan SE (samma bygge med vakten avregistrerad),
  gör ändringar via motorn och kör scrub. Läs sedan in boken i SE och kontrollera att varje ändring upptäcks.
- Hela den ärvda testsviten ska vara lika grön som baslinjen (140/140 på `future`).

## 9. Beslut (Isak 2026-11-26)

| Fråga | Beslut |
|---|---|
| Serier som standard | En serie, `A`, som börjar om varje räkenskapsår. Fler serier och löpande serier kan läggas till |
| Räkenskapsår innan design 04 finns | Bokinställningen `se/fiscal-year-start`, standard 1 januari |
| "Bokför direkt" | Byggs inte. Periodlåsning uppfyller kraven (3.1) |
| Gammal `num`-text vid första låsningen | Sparas i `se/former-num` |
| Den ärvda nyckeln `trans-read-only` | Används (alternativ C). Det är ett fält i bokfilen som koden SE har ärvt redan respekterar, inte någon koppling till upstreamprojektet |
| Ändringar och nya poster gjorda utanför SE | Stöds inte. Vanlig GnuCash är bara till för granskning. Kontrollen (6.3) rapporterar dem som fel |

## 10. Byggt och återstår (2026-11-26)

Byggt:

- `libgnucash/se/` (`gnc-se.h` för C, `gnc-se.hpp`, `gnc-se.cpp`), kompilerat in i `gnc-engine`:
  bokinställningar, serier, `gnc_se_plan_lock()`, `gnc_se_lock_through()`, vakten, kontrollsummekedjan,
  `gnc_se_verify()`, `gnc_se_overdue()`;
- krokarna i motorn: vakten i `xaccTransCommitEdit()`, uppgiften om pågående inläsning, låsdatumet som
  skrivskyddströskel, nekad avregistrering av låst faktura, nekad radering av konto med låsta transaktioner,
  ingen schemalagd transaktion i låst period;
- gränssnittet enligt avsnitt 7;
- tester: `libgnucash/se/test/gtest-gnc-se.cpp` (37 testfall, bl.a. rundtur genom XML och SQLite, faktura,
  kontoradering, tidsgränser och en oberoende uträknad referenshash). Gränssnittet är provkört på virtuell
  skärm med `util/se/gui-test/` (se `docs/byggmiljo.md`).

Kända begränsningar:

1. **Importer** (bankfil, CSV, QIF) kan skapa transaktioner i låst period. De nekas inte vid importen utan
   hittas av kontrollen och hindrar nästa låsning tills de har flyttats eller tagits bort. En spärr redan i
   importdialogen vore bättre.
2. **Kursvinsttransaktioner** och **bokslutstransaktioner** ("Stäng bok") som programmet skapar med datum i
   låst period hanteras som i punkt 1. Bokslutstransaktioner måste alltså skapas innan perioden låses.
3. **Helgdagar** är inte kända när påföljande arbetsdag räknas ut.
4. **Registreringsordningen** bygger på registreringstidpunkten med sekundupplösning. Transaktioner som
   registrerats samma sekund (t.ex. vid import) ordnas inbördes efter GUID, alltså godtyckligt men stabilt.
5. Gränssnittet har ingen automatisk test; det provkörs för hand eller med skripten i `util/se/gui-test/`.
6. Den som skriver direkt i bokfilen kommer förbi vakten, och den som dessutom räknar om kontrollsummorna
   kommer förbi kontrollen (se 6.3).
