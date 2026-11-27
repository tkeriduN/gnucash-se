# Design 02 – rättelseverifikation

Status: **byggt 2026-11-26** på grenen `feat/correction`. Bygger på design 01 (lås och numrering).

> **2026-11-27:** GTK-gränssnittet som beskrivs här är borttaget (steg 0 i [färdplanen](../fardplan.md)); motorn gäller oförändrat.
> Avsnitten om gränssnitt och provkörning beskriver det som fanns till och med `50fe4e5fa7` och är förlaga för webbklienten.

## 1. Vad lagen kräver och vad funktionen gör

| Krav | Källa (se `../svenska-krav.md`, avsnitt A2) | Funktion i SE |
|---|---|---|
| En bokförd post rättas genom en särskild rättelsepost, inte genom ändring | BFNAR 2.17 | En låst transaktion går inte att ändra (design 01). Den rättas med `gnc_se_correct()` |
| Rättelseposten är en ny bokföringspost med egen verifikation | BFNAR 2.18 | Rättelsen är en egen transaktion som får eget verifikationsnummer när den bokförs |
| Det ska anges när rättelsen gjordes och av vem | 5 kap. 5 och 9 §§ BFL | Rättelsen bär registreringstidpunkt, signatur (krävs) och anledning |
| Vid granskning av den rättade posten ska det utan svårighet gå att se att den har rättats | 5 kap. 5 § BFL, kommentaren till BFNAR 2.18 | Originalet pekar på rättelsen, och när rättelsen bokförs skrivs dess verifikationsnummer in i originalets anteckning |
| Den ursprungliga uppgiften ska klart framgå; en ersatt verifikation förblir räkenskapsinformation | BFNAR 5.15–5.16 | Originalet ligger kvar oförändrat |
| Rättelsen bokförs i den period då felet upptäcktes | kommentaren till BFNAR 2.18 | Rättelsens datum måste ligga i öppen period |

## 2. Hur en rättelse går till

1. Användaren står på en bokförd transaktion i registret och väljer Transaktion → Lägg till omvänd transaktion.
   För en bokförd transaktion öppnas SE:s dialog (för en preliminär fungerar kommandot som i GnuCash).
2. Dialogen frågar efter rättelsens datum (förval i dag), **signatur** (krävs, frågas varje gång) och anledning,
   och om en kopia av originalet också ska skapas.
3. SE skapar **rättelseposten**: en preliminär transaktion med originalets rader och omvända belopp, beskrivningen
   "Rättelse av A 12: …" och samma serie som originalet. Med kopian skapas dessutom en preliminär transaktion med
   originalets rader och belopp, som användaren ändrar till det som skulle ha bokförts.
4. Båda är preliminära och kan ändras eller tas bort tills de bokförs med "Bokför till och med". Då får de sina
   verifikationsnummer (rättelsen det lägre), och originalets anteckning får raden
   "Rättad genom A 15, daterad …, signatur …".

Ett original kan ha en rättelse åt gången. Tas en preliminär rättelse bort räknas originalet inte längre som
rättat. En bokförd rättelse som själv är fel rättas på samma sätt som vilken bokförd transaktion som helst.

## 3. Lagring

Nya nycklar under `se/`, alla på transaktioner:

| Nyckel | På | Innehåll |
|---|---|---|
| `se/corrects` | rättelsen | GUID för transaktionen som rättas |
| `se/signature` | rättelsen | vem som gjorde rättelsen |
| `se/reason` | rättelsen | anledningen (kan vara tom) |
| `se/replaces` | kopian | GUID för transaktionen som kopian ersätter |
| `se/corrected-by` | originalet | GUID för rättelsen |

Originalet får också den ärvda nyckeln `reversed-by`, som registret använder för att hoppa till den omvända
transaktionen. Registreringstidpunkten är transaktionens vanliga `date_entered`.

Skydd:
- På ett låst original är `se/corrected-by`, `reversed-by` och anteckningen fria fält (de ingår varken i
  kontrollsumman eller i det vakten jämför), så länkningen går igenom vakten utan undantag.
- När rättelsen eller kopian bokförs ingår `se/corrects`, `se/signature`, `se/reason` och `se/replaces` i det
  vakten skyddar och i kontrollsumman. De hashas bara när de finns, så transaktioner utan dem har samma
  kontrollsumma som i design 01 (referenshashen i testet är oförändrad).
- Kontrollen (`gnc_se_verify()`) rapporterar en bokförd rättelse vars original saknas eller inte är bokfört.

`xaccTransReverse()` används inte: den klonar originalets KVP, där lås-nycklarna ligger, och en sådan kopia
stoppas av vakten. Rättelsen byggs i stället från `xaccTransCloneNoKvp()`.

## 4. Signaturen

Signaturen är en parameter till `gnc_se_correct()` och lagras som text. Gränssnittet frågar efter den varje
gång och sparar den inte mellan gångerna (Isaks beslut 2026-11-26). När programmet får användare och
inloggning är det gränssnittet eller servern som fyller i parametern; motorn och bokformatet behöver inte
ändras.

## 5. Kända begränsningar

- Rättelsen tar alltid tillbaka hela originalet. En delrättelse görs genom att kopian ändras till det rätta,
  eller genom att användaren själv bokför skillnaden.
- Originalets anteckning är ett fritt fält och kan ändras av användaren. Den verkliga hänvisningen ligger i
  `se/corrected-by` och `se/corrects`.
- Beskrivningen "Rättelse av …" och anteckningsraden skrivs på det språk programmet kör med när de skapas.
- Rättelsen och kopian ligger i samma lot som originalets rader (kund- och leverantörsreskontra), på samma
  sätt som GnuCash egen omvända transaktion.
