# GnuCash SE

GnuCash SE är en fristående variant av [GnuCash](https://www.gnucash.org) för svenska aktiebolag (K2) och
enskilda firmor (K1). SE står för Swedish Edition och Server Edition: ett serverprogram med webbklient, byggt
runt GnuCash bokföringsmotor, som körs som tjänst för flera samtidiga användare eller lokalt på en dator. Här
byggs det som svensk lag kräver men som inte passar in i GnuCash själv:

- fasta lås: bokförda poster går inte att ändra eller radera (BFNAR 2.1),
- obrutna verifikationsserier som systemet kan kontrollera (BFNAR 5.9–5.10),
- rättelse bara genom rättelseverifikation med hänvisning åt båda håll (5 kap. 5 § BFL, BFNAR 2.17–2.18),
- behandlingshistorik (5 kap. 11 § BFL, BFNAR 9.16),
- räkenskapsår per bok (3 kap. BFL),
- på väg: API, webbklient, underlag, moms (eSKD), fakturor; senare SRU, iXBRL och byte mellan BAS-årgångar.

Det som är byggt i dag är motorn (`libgnucash/se`). GnuCash GTK-program byggs inte längre i forken, och
webbklienten finns inte än; se [`fardplan.md`](fardplan.md).

Upstreams underhållare har bedömt att lås och verifikationsnumrering inte passar GnuCash övergripande design
(John Ralls på gnucash-devel 2026-11-26). GnuCash SE är därför ett fristående projekt med egen inriktning.
GnuCash är startpunkten och källan för löpande ändringar, som tas in genom synk. Forken skickar inget tillbaka
och har ingen kontakt med upstreamprojektet.

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
[`avvikelser-mot-upstream.md`](avvikelser-mot-upstream.md). Katalogen `gnucash/` (GTK-programmet) byggs inte och
hålls identisk med upstream. På synkgrenarna (`chore/sync-upstream-…`) bygger CI dessutom hela GnuCash med GTK,
som kontroll av att det som togs in fortfarande bygger och klarar upstreams egna tester.

## Bokformat

En bok som har skapats eller öppnats i GnuCash SE är en SE-bok och används i SE. Vanlig GnuCash ska kunna
öppna den för att granska innehållet, men att arbeta i samma bok i både GnuCash och SE stöds inte. SE lagrar
sina uppgifter (lås, serier, historik) i bokens KVP-data och sätter ingen feature-flagga, eftersom en sådan får
vanlig GnuCash att vägra öppna boken helt. Ändringar som ändå görs utanför SE upptäcks och rapporteras som
fel; se [`design/01-las-och-numrering.md`](design/01-las-och-numrering.md).

## Innehåll i docs/

| Fil | Innehåll |
|---|---|
| [`fardplan.md`](fardplan.md) | forkens mål, beslut och ordning |
| [`svenska-krav.md`](svenska-krav.md) | lagkrav och tekniska gränssnitt (BFL, BFNAR, BAS, SIE, moms, deklaration, årsredovisning) |
| [`avvikelser-mot-upstream.md`](avvikelser-mot-upstream.md) | allt forken ändrar i upstreams filer |
| [`byggmiljo.md`](byggmiljo.md) | bygga och testa forken |
| [`design/`](design/) | designskisser, en per funktion: 01 lås och numrering, 02 rättelseverifikation, 03 behandlingshistorik, 04 räkenskapsår, 05 serverarkitektur och mätning, 06 motorn för server |
| [`../se/README.md`](../se/README.md) | katalogen för server, API och webbklient, och dess licens |

Kontoplanen BAS läggs inte in i forken innan rättighetsfrågan är utredd (se `svenska-krav.md`, avsnitt B4).

## Licens

GnuCash-koden och SE-modulen i motorn (`libgnucash/se`): GNU General Public License, version 2 eller version 3
(de flesta filer "version 2 eller senare"); detaljer i `LICENSE` i repots rot.

Allt under `se/` (kommandolager, server, kommandoradsklient, webbklient): GNU Affero General Public License,
version 3 eller senare; se [`../se/README.md`](../se/README.md). Bara fri programvara används som beroende.
