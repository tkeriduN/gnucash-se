# GnuCash SE

GnuCash SE är en fristående variant av [GnuCash](https://www.gnucash.org) för svenska aktiebolag (K2) och
enskilda firmor (K1). SE står för Swedish Edition och på sikt Server Edition. Här byggs det som svensk lag kräver
men som inte passar in i GnuCash själv:

- fasta lås: bokförda poster går inte att ändra eller radera (BFNAR 2.1),
- obrutna verifikationsserier som systemet kan kontrollera (BFNAR 5.9–5.10),
- rättelse bara genom rättelseverifikation med hänvisning åt båda håll (5 kap. 5 § BFL, BFNAR 2.17–2.18),
- behandlingshistorik (5 kap. 11 § BFL, BFNAR 9.16),
- senare: SRU, momsdeklaration (eSKD), iXBRL och byte mellan BAS-årgångar med bekräftade matchningar.

Upstreams underhållare har bedömt att lås och verifikationsnumrering inte passar GnuCash övergripande design
(John Ralls på gnucash-devel 2026-11-26). Därför byggs de här och inte upstream. Rättningar och funktioner som
upstream vill ha skickas fortfarande dit, från en separat klon av `Gnucash/gnucash`, inte från den här forken.

## Förhållande till upstream

| Remote | Repo | Roll |
|---|---|---|
| `origin` | `tkeriduN/gnucash-se` | den här forken (inte en GitHub-fork, så ingen "Sync fork"-knapp) |
| `upstream` | `Gnucash/gnucash` | källan som forken följer |

Grenar:

| Gren | Följer | Roll |
|---|---|---|
| `future` | `upstream/future` (blivande 6.0) | **arbetsgren**: alla egna funktioner byggs här |
| `stable` | `upstream/stable` (5.x) | synkgren mot upstreams stabila release; inga egna funktioner |
| `feat/…`, `fix/…`, `docs/…`, `chore/…` | `future` | eget arbete, en gren per ändring |

Forken bygger på `future` eftersom 6.0 (feature freeze 2027-02-07, release 2027-03-28) ändrar motorn
(KVP bara via accessorer, mindre GLib). Egen kod på 5.x skulle behöva flyttas i mars. När 6.0 är släppt blir
`future` (eller det som upstream då kallar sin stabila gren) forkens default-gren.

### Synka med upstream

```sh
git fetch upstream --tags
git switch -c chore/sync-upstream-ÅÅÅÅ-MM-DD future
git merge upstream/future          # merge, inte rebase: grenen är delad
# lös konflikter, bygg och testa (docs/byggmiljo.md), merge in i future
```

Forkens ändringar hålls små och avgränsade så att synkningarna blir billiga: egna filer och moduler hellre än
ändringar mitt i upstreams kod. Varje ändring i en upstreamfil förs in i
[`avvikelser-mot-upstream.md`](avvikelser-mot-upstream.md).

## Bokformat

Böcker som skapats eller låsts i GnuCash SE ska gå att öppna i vanlig GnuCash. SE lagrar sina uppgifter (lås,
serier, historik) i bokens KVP-data och sätter ingen feature-flagga som får vanlig GnuCash att vägra öppna
boken. Priset är att vanlig GnuCash kan ändra låsta poster. SE måste därför upptäcka sådana ändringar i
efterhand; se [`design/01-las-och-numrering.md`](design/01-las-och-numrering.md).

## Innehåll i docs/

| Fil | Innehåll |
|---|---|
| [`fardplan.md`](fardplan.md) | forkens mål, beslut och ordning |
| [`svenska-krav.md`](svenska-krav.md) | lagkrav och tekniska gränssnitt (BFL, BFNAR, BAS, SIE, moms, deklaration, årsredovisning) |
| [`avvikelser-mot-upstream.md`](avvikelser-mot-upstream.md) | allt forken ändrar i upstreams filer |
| [`byggmiljo.md`](byggmiljo.md) | bygga och testa forken |
| [`design/`](design/) | designskisser, en per funktion |

Kontoplanen BAS läggs inte in i forken innan rättighetsfrågan är utredd (se `svenska-krav.md`, avsnitt B4).

## Licens

Samma som GnuCash: GNU General Public License, version 2 eller version 3 (de flesta filer "version 2 eller
senare"); detaljer i `LICENSE` i repots rot. Forkens egna filer licensieras som GnuCash, så att delar kan
skickas vidare till upstream.
