# GnuCash SE – färdplan

Läge 2026-11-26. Underlag: [`svenska-krav.md`](svenska-krav.md) (lagkraven med källor) och följeslagarrepots
färdplan för upstreamarbetet. Den här filen gäller bara forken.

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
that GnuCash isn't suitable for them." (John Ralls, gnucash-devel 2026-11-26). Därför byggs de här.

## Beslut (Isak 2026-11-26)

| Fråga | Beslut |
|---|---|
| Basgren | `future` (blivande 6.0). Inga användbara SE-releaser före 6.0, men ingen stor flytt av motorkod i mars |
| Bokformat | Nedåtkompatibelt. Lås, serier och historik i KVP; vanlig GnuCash kan öppna boken. SE upptäcker ändringar i efterhand |
| Första funktion | Fast lås och obruten verifikationsnumrering |
| Första steg | Grunden: dokumentation, byggmiljö, baslinje, designskiss. Ingen funktionskod |

Tidigare beslut som också gäller forken:
- Filvägen före API: SE skriver filer (SIE, eSKD-XML, SRU) som laddas upp i myndighetens e-tjänst. Ingen
  nätverkskod mot Skatteverket eller Bolagsverket i det här skedet.
- AI-märkning som i upstream: trailern `Assisted-by:` i commits, Isak lägger själv `Signed-off-by` efter
  granskning.
- BAS-data (kontonamn ur BAS filer) läggs inte in innan rättighetsfrågan är utredd.

## Ordning

| Steg | Innehåll | Lagkrav |
|---|---|---|
| **0 Grund** (pågår) | `docs/`, byggmiljö i container, baslinje på `future`, designskiss för lås och numrering | – |
| **1 Lås och numrering** | Låsdatum per bok som bara kan flyttas framåt. Verifikationsserier. Nummer tilldelas vid låsning i registreringsordning. Spärr mot ändring och radering av låsta transaktioner. Kontroll att serien är obruten. Upptäckt av ändringar gjorda utanför SE. Design: [`design/01-las-och-numrering.md`](design/01-las-och-numrering.md) | BFNAR 2.1, 5.9–5.10 |
| **2 Rättelse** | Rättelseverifikation från en låst post, med hänvisning åt båda håll och vem/när | 5:5, 5:9 BFL, BFNAR 2.17–2.18, 5.15–5.16 |
| **3 Behandlingshistorik** | Logg över registreringar, låsningar och systemändringar (nya konton, ändrade momssatser, programversion) som följer med boken | 5:11 BFL, BFNAR 9.16 |
| **4 Räkenskapsår** | Räkenskapsår per bok, även brutet och förlängt; serier per år | 3 kap. BFL |
| **5 Rapportering** | SRU-filer (INK2R/INK2S, NE), momsdeklaration som eSKD-XML, BAS-årgångsbyte med uttrycklig mappning och bekräftade matchningar | – |
| **6 Senare** | iXBRL för K2-årsredovisning, ev. API-brygga (kräver avtalspart), Server Edition | – |

SIE och BAS 2026-mallarna drivs upstream; forken tar dem via synken. Steg 1–3 hänger ihop och designas
tillsammans, men byggs och levereras i den ordningen.

## Tidpunkter som styr

- Upstream 6.0: feature freeze 2027-02-07, release 2027-03-28. Forken synkar `upstream/future` löpande, och
  särskilt kring frysningen.
- Inget av steg 1–3 behöver ändras om upstream ändrar sin releaseplan, men varje krok i upstreams motorkod
  är en framtida synkkostnad (se [`avvikelser-mot-upstream.md`](avvikelser-mot-upstream.md)).
