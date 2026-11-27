# Design 04 – räkenskapsår

Status: **byggt 2026-11-26** på grenen `feat/fiscal-years`. Bygger på design 01–03.

> **2026-11-27:** GTK-gränssnittet som beskrivs här är borttaget (steg 0 i [färdplanen](../fardplan.md)); motorn gäller oförändrat.
> Avsnitten om gränssnitt och provkörning beskriver det som fanns till och med `50fe4e5fa7` och är förlaga för webbklienten.

## 1. Vad lagen kräver och vad funktionen gör

| Krav | Källa (se `../svenska-krav.md`, avsnitt A6) | Funktion i SE |
|---|---|---|
| Räkenskapsåret är tolv månader; kalenderår eller brutet år | 3 kap. 1 § BFL | En lista med räkenskapsår per bok. Nya år blir tolv månader och följer direkt på det föregående |
| Förkortat eller förlängt räkenskapsår, högst 18 månader, när bokföringsskyldigheten inträder eller räkenskapsåret läggs om | 3 kap. 3 § BFL | Det första året kan anges fritt, och sista dagen i det senaste året kan flyttas. Längre än 18 månader nekas |
| Verifikationsserier får börja om varje räkenskapsår | BFNAR 5.10 | Serier som numreras per räkenskapsår använder årets nyckel som period |
| Den löpande bokföringen avslutas för varje räkenskapsår | 6 kap. BFL | "Avsluta räkenskapsår" bokför allt till och med årets sista dag och markerar året som avslutat. Själva bokslutsposterna görs med GnuCash "Stäng bok" eller för hand, före avslutet |

Årsredovisning, årsbokslut och förenklat årsbokslut (6 kap. BFL) ingår inte.

## 2. Räkenskapsåren

Varje år har första dag, sista dag och en **nyckel**. Nyckeln är också perioden i de verifikationsserier som
numreras per räkenskapsår (`A 2026`). Nyckeln sätts när året läggs upp och ändras aldrig:

- kalenderåret som räkenskapsåret börjar i (`2026`), eller
- årets första dag (`2026-07-01`) om boken redan har ett år som börjar samma kalenderår, vilket händer efter
  ett förkortat år.

Det är samma nycklar som design 01 räknade fram ur startdagen, så böcker som låstes före design 04 fortsätter
i sina perioder.

Åren följer på varandra utan luckor. De kommer till på tre sätt:

1. **Av sig själva vid låsning.** När något bokförs får boken de år som det bokförda ligger i. Har boken inga
   år utgår de från startdagen i inställningarna (förval 1 januari); annars läggs tolvmånadersår till efter
   det senaste året (eller före det tidigaste).
2. **I förväg**, när det första året inte är tolv månader. Ett nystartat bolag anger till exempel
   2024-11-15 till 2025-12-31.
3. **Genom att sista dagen i det senaste året flyttas**: förkortat eller förlängt år vid omläggning. Sista
   dagen kan inte flyttas till före låsdatumet, och ett avslutat år kan inte ändras.

`gnc_se_book_get_fiscal_year(book, datum)` svarar alltid: med ett av bokens år, eller med det år boken skulle
få. Planen för en låsning (`gnc_se_plan_lock()`) använder samma uträkning som låsningen sedan sparar, så
förhandsvisningen stämmer med utfallet.

När boken har fått räkenskapsår är startdagen inte längre en inställning (`gnc_se_book_set_fiscal_year_start()`
nekar). Året läggs då om genom punkt 3.

## 3. Avsluta räkenskapsår

`gnc_se_close_fiscal_year(book, datum, signatur)`:

1. nekar om året inte är slut, om ett tidigare år är öppet (åren avslutas i ordning) eller om signatur saknas;
2. bokför till och med årets sista dag med `gnc_se_lock_through()`, om det inte redan är gjort;
3. markerar året som avslutat, med tidpunkt och signatur;
4. skriver en händelse i behandlingshistoriken.

Ett avslutat år kan inte ändras. Det som är bokfört i det rättas som allt annat bokfört, med
rättelseverifikation i öppen period.

## 4. Lagring

```
se/fiscal-years/<nyckel>/start       första dag
se/fiscal-years/<nyckel>/end         sista dag
se/fiscal-years/<nyckel>/closed-at   när året avslutades
se/fiscal-years/<nyckel>/closed-by   signatur
se/fiscal-year-start                 startdag (MM-DD), mönster så länge boken saknar år
```

Ändringar av åren (upplagt, sista dag flyttad, avslutat) står i behandlingshistoriken som händelser av slaget
`fiscal-year`. `gnc_se_verify()` rapporterar en bokförd transaktion som är numrerad i ett annat år än det
som dess datum ligger i, vilket avslöjar om årens datum har ändrats utanför SE.

En bok som låstes före design 04 får sina år första gången något ändras (låsning, år anges, år avslutas):
från den tidigaste bokförda transaktionen till låsdatumet, med samma nycklar som den redan använder.

## 5. Gränssnitt

- **Verifikationsserier och räkenskapsår…**: listan med bokens år, och knappen "Ange eller ändra ett
  räkenskapsår…" med första och sista dag. Startdagen (dag och månad) går bara att ändra så länge boken saknar
  år.
- **Avsluta räkenskapsår…**: föreslår det tidigaste öppna året, visar vad som numreras och kräver signatur.
- Behandlingshistoriken listar åren.

## 6. Kända begränsningar

- Bokslutsposter (omföring av årets resultat) skapas inte av SE. De görs med det ärvda "Stäng bok" eller för
  hand, och måste vara gjorda innan året avslutas.
- Ingen kontroll av att årsbokslutet är klart i tid (6 kap. 7 § BFL: sex månader).
- Ett år kan inte delas eller slås ihop i efterhand, och ett avslutat år kan inte öppnas igen.
- Ingen kontroll av *varför* ett år är förkortat eller förlängt; lagen tillåter det bara i vissa lägen.
- Ingående balanser och årsskiftesrutiner (öppningsbalans för nytt år) hanteras som i GnuCash, alltså inte
  alls särskilt: kontona löper vidare.
