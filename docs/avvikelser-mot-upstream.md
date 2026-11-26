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
4. **Ingen feature-flagga i boken** (`gnc-features`) som stänger ute vanlig GnuCash (beslut 2026-11-26).
5. **Ändringar som upstream kan vilja ha** skickas dit från klonen av `Gnucash/gnucash` och tas in i forken
   via synken, inte tvärtom.
6. **Upstreams stil** i allt som rör upstreamfiler: commitrubriker `[Modul] Kort rubrik`, upstreams kodstil.

## Filer och kataloger som bara finns i forken

| Sökväg | Innehåll | Synkrisk |
|---|---|---|
| `docs/` | forkens dokumentation (upstream har `doc/`, som lämnas orörd) | ingen |
| `util/se/` | byggcontainer för `future` (`future-env`, `containers/`). Ligger utanför `util_DIST` i `util/CMakeLists.txt` och kommer därför inte med i källtarbollen | ingen |

## Ändringar i upstreams filer

| Fil | Ändring | Varför | Synkrisk | Sedan |
|---|---|---|---|---|
| – | inga ännu | – | – | – |

## Ändrat beteende utan kodändring

Sådant som SE gör annorlunda genom att använda upstreams mekanismer på ett nytt sätt (t.ex. att sätta en
befintlig KVP-nyckel). Inga ännu.
