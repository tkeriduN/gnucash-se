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
that GnuCash isn't suitable for them." (John Ralls, gnucash-devel 2026-11-26). Därför byggs de här, i ett
fristående projekt som bara använder GnuCash som startpunkt och källa för löpande ändringar.

## Beslut (Isak 2026-11-26)

| Fråga | Beslut |
|---|---|
| Basgren | `future` (blivande 6.0). Inga användbara SE-releaser före 6.0, men ingen stor flytt av motorkod i mars |
| Bokformat | En SE-bok används i SE. Vanlig GnuCash ska kunna öppna den för granskning, inte för arbete. Lås, serier och historik i KVP, ingen feature-flagga. Ändringar utanför SE upptäcks och rapporteras som fel |
| Förhållande till upstream | Fristående projekt. Upstream är startpunkt och källa för ändringar via synk; forken skickar inget tillbaka |
| Första funktion | Fast lås och obruten verifikationsnumrering |
| Första steg | Grunden: dokumentation, byggmiljö, baslinje, designskiss. Ingen funktionskod |
| Lås och numrering | Periodlåsning (ingen "Bokför direkt"); en serie per räkenskapsår som standard. Detaljer i designskissens avsnitt 9 |

Tidigare beslut som också gäller forken:
- Filvägen före API: SE skriver filer (SIE, eSKD-XML, SRU) som laddas upp i myndighetens e-tjänst. Ingen
  nätverkskod mot Skatteverket eller Bolagsverket i det här skedet.
- AI-märkning: trailern `Assisted-by: Claude Code` i commits. Inget krav på `Signed-off-by`: Isak är ensam
  utvecklare, åtminstone det närmaste året.
- BAS-data (kontonamn ur BAS filer) läggs inte in innan rättighetsfrågan är utredd.

## Ordning

| Steg | Innehåll | Lagkrav |
|---|---|---|
| **0 Grund** (klart 2026-11-26) | `docs/`, byggmiljö i container, baslinje på `future`, designskiss för lås och numrering | – |
| **1 Lås och numrering** (byggt 2026-11-26, se designskissens avsnitt 10) | Periodlåsning med ett låsdatum per bok som bara kan flyttas framåt. Verifikationsserier, standard en per räkenskapsår. Nummer tilldelas vid låsning i registreringsordning. Spärr mot ändring och radering av låsta transaktioner. Kontroll att serien är obruten. Upptäckt av ändringar gjorda utanför SE. Design: [`design/01-las-och-numrering.md`](design/01-las-och-numrering.md) | BFNAR 2.1, 5.9–5.10 |
| **2 Rättelse** (byggt 2026-11-26, [`design/02-rattelse.md`](design/02-rattelse.md)) | Rättelseverifikation från en låst post, med hänvisning åt båda håll och vem/när | 5:5, 5:9 BFL, BFNAR 2.17–2.18, 5.15–5.16 |
| **3 Behandlingshistorik** (byggt 2026-11-26, [`design/03-behandlingshistorik.md`](design/03-behandlingshistorik.md)) | Logg över låsningar, rättelser och systemändringar (konton, momstabeller, inställningar, programversion) som följer med boken, och uppställning av varje bokförd post med registrerings- och bokföringstid. **Systemdokumentation (BFNAR 9.2) ingår inte**: struken av Isak 2026-11-26 i väntan på ombyggnaden till serverläge | 5:11 BFL, BFNAR 9.16 |
| **4 Räkenskapsår** | Räkenskapsår per bok, även brutet och förlängt; serier per år | 3 kap. BFL |
| **5 Rapportering** | SRU-filer (INK2R/INK2S, NE), momsdeklaration som eSKD-XML, BAS-årgångsbyte med uttrycklig mappning och bekräftade matchningar | – |
| **6 Senare** | iXBRL för K2-årsredovisning, ev. API-brygga (kräver avtalspart), Server Edition | – |

Steg 1–3 hänger ihop och designas tillsammans, men byggs och levereras i den ordningen.

## Avgränsning mot upstreamarbetet

Isak bidrar också till `Gnucash/gnucash` i ett separat spår (egen klon, egen plan). Samma sak ska inte byggas
på två ställen. Det som går in upstream kommer hit via synken. Läget 2026-11-26 enligt upstreamspårets plan:

| Område | Upstreamspåret | Forken |
|---|---|---|
| Lås, verifikationsnumrering, rättelse, behandlingshistorik | Avvisat av upstream 2026-11-26; byggs inte där | **Byggs här** (steg 1–3) |
| Räkenskapsår per bok | Inget arbete; upstreams bugg 493789 är öppen sedan 2007 | Byggs här (steg 4), enkel bokinställning redan i steg 1 |
| Rättningar av sv_SE-mallarna och mall-laddaren | Pågår (PR mot `stable`) | Byggs inte här; kommer via synk |
| Kontomatchning när kontoträd slås ihop | Buggfix planerad | Byggs inte här; kommer via synk |
| BAS 2026-kontomallar | Planerat upstream | Byggs inte här; kommer via synk |
| SIE-export och -import | Planerat upstream (övertagande av PR #2217, mot 6.0) | Byggs inte här; kommer via synk. SE behöver bara att verifikationsserie och nummer går att läsa av exporten |
| Byte mellan BAS-årgångar | Planerat som fristående verktyg med uttrycklig mappning | **Överlapp att avgöra före steg 5**: ett verktyg räcker, antingen det fristående eller inbyggt här |
| Redovisningskoder per konto, SRU | Skjutet till efter 6.0, diskussion med upstream först | **Överlapp att avgöra före steg 5**: vänta in upstreams lösning eller bygg eget |
| Moms: rapport och eSKD-fil | Inget beslutat; upstream hänvisar till momstabellerna | Avgörs före steg 5 |

Steg 1–4 har alltså inget överlapp. Steg 5 påbörjas inte förrän de tre markerade raderna är avgjorda.

## Tidpunkter som styr

- Upstream 6.0: feature freeze 2027-02-07, release 2027-03-28. Forken synkar `upstream/future` löpande, och
  särskilt kring frysningen.
- Inget av steg 1–3 behöver ändras om upstream ändrar sin releaseplan, men varje krok i upstreams motorkod
  är en framtida synkkostnad (se [`avvikelser-mot-upstream.md`](avvikelser-mot-upstream.md)).
