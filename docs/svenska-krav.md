<!-- Kopierad 2026-11-26 från Isaks följeslagarrepo gnucash-sweden (docs/svenska-krav.md, läge 2026-11-24),
     oförändrad. Rättelser och tillägg görs här i forken från och med nu. -->
<!-- Researchunderlag sammanställt 2026-11-24 av en AI-agent. [V] = läst i primärkälla, [S] = sekundärkälla,
     [EJ VERIFIERAT] = ej bekräftat. Kontrollera mot källorna innan något här används som beslutsunderlag. -->

# Underlag: svenska lagkrav och tekniska gränssnitt för GnuCash (aktiebolag med K2 och enskild firma med K1)

Rapporten gäller läget 2026-11-24. **[V]** betyder att jag har läst uppgiften i primärkällan, i det flesta fall som fulltext ur PDF via `pdftotext`. **[S]** betyder sekundärkälla. **[EJ VERIFIERAT]** betyder att jag inte kunde hämta eller bekräfta uppgiften. Inga filer har skrivits till disk; allt har lästs via stdout eller i minnet.

**Viktigast för planen:**
- **Låsning av bokförda poster är det största lagkravet på GnuCash.** BFN skriver uttryckligen att ett program där poster kan raderas eller ändras inte har *bokfört* något förrän posterna är låsta. GnuCash behöver alltså låsning, rättelse bara via ny verifikation, obrutna nummerserier och en logg över alla ändringar.
- **BAS-kontoplanen är en möjlig blockerare.** Den maskinläsbara JSON-versionen har en licens som inte går ihop med GPL. PDF- och Excel-filerna saknar helt licenstext. Skriftligt tillstånd från BAS bör begäras innan något nytt läggs in i GnuCash.
- **API:erna förutsätter ett företag med avtal och hemliga nycklar, inte en programinstans per användare.** Det gäller både Skatteverket och Bolagsverket. Det realistiska alternativet är filuppladdning till Skatteverket och papper till Bolagsverket, eller en central tjänst som drivs av en juridisk person.

---

## A. Bokföringslagen (BFL 1999:1078) och BFNAR 2013:2

Källor:
- Lagtext (ändrad t.o.m. SFS 2024:342): https://www.riksdagen.se/sv/dokument-och-lagar/dokument/svensk-forfattningssamling/bokforingslag-19991078_sfs-1999-1078/ [V]
- BFN:s vägledning Bokföring, VL 13:2 (uppdaterad 2024-09-16): https://www.bfn.se/wp-content/uploads/vl13-2-bokforing.pdf [V]

### A1. Grund- och huvudbokföring, verifikationer
- **5 kap. 1 § BFL:** affärshändelserna ska kunna presenteras i registreringsordning (grundbokföring) och i systematisk ordning (huvudbokföring). Det ska gå att kontrollera att bokföringsposterna är fullständiga.
- **1 kap. 2 § p. 6 BFL:** en bokföringspost är "varje enskild notering i grundbokföringen eller huvudbokföringen".
- **BFNAR punkt 2.2:** inom den systematiska ordningen ska registreringsordningen framgå per konto. Används flera nummerserier ska ordningen framgå inom varje serie.
- **BFNAR punkt 2.3:** för varje bokföringspost ska det gå att utläsa:
  - registreringsordning
  - redovisningsperiod (kalendermånad eller motsvarande, punkt 1.8)
  - verifikationsnummer eller annat identifieringstecken
  - kontering
  - bokfört belopp

  I datorbaserad bokföring ska registreringsdagen alltid framgå av behandlingshistoriken (kommentaren till punkt 2.3, med hänvisning till punkt 9.16).
- **5 kap. 6 § BFL:** det ska finnas en verifikation för varje affärshändelse. Gemensam verifikation är tillåten för likartade händelser och för en dags kontantförsäljning.
- **5 kap. 7 § BFL – vad en verifikation ska innehålla:**
  - när den sammanställdes och när affärshändelsen inträffade
  - vad händelsen avser, vilket belopp den gäller och vilken motpart den berör
  - vid behov hänvisning till underliggande handlingar och var de finns
  - verifikationsnummer eller annat identifieringstecken, plus det som behövs för att koppla verifikationen till den bokförda händelsen
- **BFNAR punkt 5.4:** uppgifterna i verifikationen ska vara varaktiga och får inte raderas.
- **BFNAR punkt 5.9 (obruten följd):** "En serie av verifikationsnummer eller andra identifieringstecken ska vara obruten för en tidsperiod. Serien ska föras i en följd utan att mellanliggande nummer eller tecken hoppas över. Det ska i bokföringssystemet gå att kontrollera att verifikationsserien är obruten."
- **BFNAR punkt 5.10:** flera nummerserier är tillåtna om bokföringen förblir överskådlig. En serie kan börja om varje räkenskapsår eller löpa över flera år.
- **BFNAR punkt 9.6:** systemdokumentationen ska beskriva hur serierna är indelade och vilken tidsperiod de omfattar.

### A2. Rättelse och vad "bokförd" betyder – det centrala för GnuCash
- **5 kap. 5 § BFL:** när en bokföringspost rättas ska det anges när rättelsen gjordes och av vem. Görs rättelsen med en särskild rättelsepost ska man vid granskning av den rättade posten "utan svårighet" kunna se att den har rättats.
- **5 kap. 9 § BFL:** samma krav gäller rättelse av en verifikation.
- **BFNAR punkt 2.1:** "Bokföringen ska göras på ett varaktigt sätt. Det innebär att det som har bokförts inte ska kunna raderas eller på annat sätt göras oläsligt."
- **Kommentaren till punkt 2.1 (s. 15–16), ordagrant:**
  - "I en manuell bokföring görs noteringen på ett varaktigt sätt t.ex. genom att affärshändelsen noteras med bläck och i ett bokföringsprogram genom att registreringen inte kan tas bort eller ändras."
  - "Detsamma gäller så kallad preliminär bokföring, dvs. registrering av en affärshändelse i ett bokföringsprogram i vilket bokföringsposterna kan raderas eller ändras fram till dess att en aktiv åtgärd vidtas för att låsa bokföringen. Så länge bokföringen inte är låst, kan affärshändelserna inte anses bokförda. Bokföringen måste låsas så att den blir varaktig senast vid den tidpunkt då affärshändelserna ska vara bokförda enligt bestämmelserna i kapitel 3."
  - Det är "först när en affärshändelse är bokförd som bestämmelserna om rättelser blir aktuella".
- **BFNAR punkt 2.17:** "I en datorbaserad bokföring ska en bokföringspost rättas genom en särskild rättelsepost." Överstrykning är bara tillåten i manuell bokföring.
- **BFNAR punkt 2.18:** en rättelsepost är en ny bokföringspost och ska ha en egen verifikation.
- **Kommentaren till punkt 2.18:**
  - Rättelseposten bokförs i den period då felet upptäcktes.
  - Om arbetet med den period där felet uppstod inte är avslutat, hänförs rättelsen till den perioden.
  - Upplysningen om rättelsen kan göras "genom att ange rättelsepostens verifikationsnummer på verifikationen till den rättade bokföringsposten".
- **BFNAR punkterna 5.15–5.16:** den ursprungliga uppgiften i en rättad verifikation ska klart framgå. En ersatt verifikation förblir räkenskapsinformation.

**Slutsats för GnuCash:** fri redigering är tillåten så länge posterna är *preliminära*. GnuCash behöver en uttrycklig "bokför/lås"-åtgärd. Efter den får inga ändringar eller raderingar göras, bara rättelseverifikationer med en hänvisning tillbaka till den rättade posten. Låsningen måste ske senast vid de tidsgränser som anges i A4.

### A3. Systemdokumentation och behandlingshistorik
- **5 kap. 11 § BFL:** företaget ska ha systemdokumentation (överblick över systemet) och behandlingshistorik (så att man kan följa varje bokföringspost).
- **BFNAR punkt 9.2 – systemdokumentationen ska innehålla:**
  - a) kontoplan
  - b) hur konton används och summeras till bokslutet, om det inte framgår av kontoplanen
  - c) samlingsplan
  - d) arkivplan (punkt 8.3)

  Undantag för *enkla bokföringssystem* (punkt 9.5). Men "Används automatkonteringar i bokföringsprogrammet anses bokföringssystemet inte vara enkelt."
- **BFNAR punkt 9.3:** kontoplanen är en sammanställning av kontonamn och, i förekommande fall, kontonummer.
- **BFNAR punkt 9.4:** samlingsplanen dokumenterar hur systemet är uppbyggt och hur delarna hänger ihop.
- **BFNAR punkt 9.9:** behandlingsregler, till exempel automatkontering, ska beskrivas.
- **BFNAR punkt 9.12:** det ska beskrivas hur elektronisk räkenskapsinformation tas fram som utskrift på papper.
- **Kommentaren till punkt 9.1:** ändras systemdokumentationen ska den ersatta versionen sparas.
- **BFNAR punkt 9.16:** "Behandlingshistoriken ska visa de bokföringsposter som har tillförts bokföringssystemet samt, i datorbaserade system, registreringsdatum. Behandlingshistoriken ska även beskriva förändringar i bokföringssystemet som påverkar bokföringsposternas behandling samt när dessa förändringar infördes." Kommentarens exempel är nya programversioner, ändrad momssats för automatkontering och nya konton i kontoplanen.

### A4. Tidpunkt för bokföring, kontantmetod och faktureringsmetod
- **5 kap. 2 § BFL:**
  - Kontanta in- och utbetalningar ska bokföras senast påföljande arbetsdag.
  - Andra affärshändelser ska bokföras "så snart det kan ske".
  - Företag med nettoomsättning normalt högst 3 mkr får vänta tills betalning sker (kontantmetoden). Vid årets slut ska alla obetalda fordringar och skulder bokföras.
- **BFNAR punkterna 3.1–3.3:** definitioner av "påföljande arbetsdag" och "så snart det kan ske".
- **BFNAR punkt 3.4:** fordringar och skulder får bokföras först när fakturan utfärdas eller tas emot (faktureringsmetoden).
- **Tillåten senareläggning (gäller inte kontanta betalningar i registreringsordning):**
  - Punkt 3.6: 50 dagar efter månadens utgång.
  - Punkt 3.7: 50 dagar efter kvartalets utgång vid omsättning högst 3 mkr.
  - Punkt 3.8: 60 dagar efter räkenskapsåret vid högst 50 verifikationer och 250 affärshändelser samt omsättning högst 1 mkr.
  - Punkt 3.9: enskild näringsidkare får vänta till deklarationsdagen om de tre villkoren är uppfyllda (bland annat ingen gränsöverskridande EU-handel).
- **BFNAR punkt 3.5:** senareläggning kräver att verifikationerna under tiden ordnas i takt med att de kommer in.
- **BFNAR punkterna 3.14–3.15:** kontantmetoden.
- Skatteverket kopplar momsmetoden till detta: faktureringsmetoden, eller bokslutsmetoden vid högst 3 mkr. https://www.skatteverket.se/foretag/moms/deklareramoms/narskajagdeklareramoms.4.6d02084411db6e252fe80008988.html [V]

### A5. Arkivering (7 kap. BFL, ändrat genom SFS 2024:342)
- **7 kap. 1 §:** räkenskapsinformation sparas som pappershandling eller elektronisk handling som omedelbart kan skrivas ut. Mottagna eller upprättade handlingar sparas i det skick, format och med det innehåll de hade när de kom in eller sammanställdes.
- **7 kap. 2 §:**
  - Handlingarna ska vara varaktiga och lätt åtkomliga.
  - De sparas "fram till och med det sjunde året efter utgången av det kalenderår då räkenskapsåret avslutades".
  - Förvaring i Sverige. Utrustning och system för att skriva ut ska också finnas tillgängliga i Sverige.
- **7 kap. 3 a §:** förvaring i annat EU-land (eller vissa länder utanför EU) är tillåten om:
  - platsen anmäls till Skatteverket
  - Skatteverket och Tullverket kan få omedelbar elektronisk åtkomst
  - informationen kan skrivas ut omedelbart i Sverige
- **7 kap. 6 § – överföring:** handlingar får förstöras om informationen förs över till en annan pappers- eller elektronisk handling utan risk att den ändras eller försvinner. Det här är kärnan i 2024-ändringen.
- **Ändringen 2024:** SFS 2024:342 bygger på prop. 2023/24:78 ("Avskaffat krav på bevarande av räkenskapsinformation i original") och trädde i kraft 1 juli 2024 [S – SFS-PDF och KPMG; https://svenskforfattningssamling.se/sites/default/files/sfs/2024-05/SFS2024-342.pdf]. BFNAR 2024:1 upphävde punkterna 7.2 och 7.6 [V].
- **BFNAR punkt 7.5:** egenupprättad information anses sammanställd när den fått den form och det format den ska presenteras i.
- **BFNAR punkt 8.2:** elektronisk räkenskapsinformation ska säkerhetskopieras. Kopian förvaras åtskild från originalet.
- **BFNAR punkt 8.3:** arkivplan vid behov.

### A6. Räkenskapsår, avslut och K-regelverk
- **3 kap. 1 § BFL:** räkenskapsåret är 12 månader. Fysiska personer har kalenderår. Övriga får ha brutet räkenskapsår.
- **3 kap. 3 §:** förkortat eller förlängt räkenskapsår (högst 18 månader) när bokföringsskyldigheten inträder eller räkenskapsåret läggs om.
- **6 kap. 1 §:** aktiebolag ska alltid avsluta med årsredovisning.
- **6 kap. 3 §:** andra företag avslutar med årsbokslut. Vid nettoomsättning normalt högst 3 mkr får det vara ett *förenklat årsbokslut* (6 kap. 6 §).
- **6 kap. 7 §:** årsbokslutet ska vara klart senast 6 månader efter räkenskapsårets utgång.
- **4 kap. 3 §:** öppningsbalansräkning när bokföringsskyldigheten inträder.
- **4 kap. 6 §:** redovisningsvaluta SEK. Aktiebolag får använda EUR.

**Gällande regelverk** (BFN: https://www.bfn.se/redovisningsregler/allmanna-rad/ och https://www.bfn.se/redovisningsregler/beslutade-redovisningsregler/ [V]):
- **Enskild firma med förenklat årsbokslut:** K1, BFNAR 2006:1. Vägledning, konsoliderad 2026: https://www.bfn.se/wp-content/uploads/vl06-1-k1enskilda-kons2026.pdf
- **Litet aktiebolag:** K2, BFNAR 2016:10. Vägledning, konsoliderad 2026: https://www.bfn.se/wp-content/uploads/vl16-10-k2-kons2026.pdf. Alternativet är K3, BFNAR 2012:1.

**Ändringar från BFN:s översyn av K-regelverken 2025–2026** [V]:
- **K2 (beslut 2025-06-16), tillämpas för räkenskapsår som börjar efter 2025-12-31:**
  - nytt tillämpningsområde (bostadsrättsföreningar flyttas till K3)
  - ändrade uppställningsformer för resultat- och balansräkning
  - periodiseringsgränsen höjs till 7 000 kr
  - fel får rättas mot ingående balans
  - nya regler om aktieägartillskott
- **K3:** ändringar beslutade 2025-06-16 och 2025-12-15 (BFNAR 2025:3).
- **Årsbokslut, BFNAR 2017:3 (beslut 2026-05-18):** tillämpas för räkenskapsår som börjar efter 2026-12-31.
- **K1, BFNAR 2006:1:**
  - Beslut 2025-03-24: gränsen för direktavdrag höjs till ett halvt prisbasbelopp.
  - Beslut 2026-10-29: upplysningar om obeskattade reserver tas bort. Ruta B11 ändras och rutorna U1–U4 utgår. Gäller räkenskapsår som börjar efter 2025-12-31.
- **Aviserat men inte beslutat – SOU 2026:59 (september 2026):**
  - ny kategori *mikroföretag* i ÅRL (högst ett av: mer än 3 anställda, balansomslutning över 1,5 mkr, nettoomsättning över 3 mkr)
  - gränsen för större företag höjs till 55 mkr balansomslutning och 110 mkr nettoomsättning
  - föreslagen ikraftträdande 2028-01-01
  - https://data.riksdagen.se/dokument/HEB359.html [V, förslaget]

  Vilka förenklingar mikroföretag skulle få har jag inte läst i detalj [EJ VERIFIERAT].

### A7. Måste kontoplanen vara BAS?
Nej. BAS-föreningen skriver: "Det är inte lagstadgat vilken kontoplan som ska användas i Sverige … Uppskattningsvis använder 95 % av alla företag i Sverige någon form av BAS-kontoplan." https://www.bas.se/om-bas/ [V]

Lagkravet är bara att det finns en kontoplan som en del av systemdokumentationen (BFNAR 9.2–9.3). I praktiken krävs att varje konto kan kopplas till SRU-koder för deklarationen och till raderna i årsredovisningen.

---

## B. BAS-kontoplanen 2026

### B1. Struktur [V]
- **Kontoklasser:**
  - 1 Tillgångar
  - 2 Eget kapital och skulder
  - 3 Intäkter
  - 4 Varor och material
  - 5–6 Övriga externa kostnader
  - 7 Personal och avskrivningar
  - 8 Finansiella poster, bokslutsdispositioner och skatt
  - Klass 0 och 9 ligger utanför den formella planen (statistik och internredovisning).
  - Källa: https://www.bas.se/kontoplaner/kontoplanens-uppbyggnad-och-anvandning/
- **Nivåer:** tvåsiffriga kontogrupper (till exempel "10 IMMATERIELLA ANLÄGGNINGSTILLGÅNGAR"), huvudkonton på xxx0 och underkonton.
- **Markering i PDF:en BAS_kontoplan_2026_v2.pdf** (https://www.bas.se/wp-content/uploads/2026/04/BAS_kontoplan_2026_v2.pdf) [V, fulltext]:
  - Rubriken lyder "KONTOPLAN BAS 2026 v 1.1".
  - Teckenförklaringen är "**# = Konton som inte ska användas när K2 tillämpas**", till exempel 1010#.
  - Symbolen ■ förekommer inte i 2026-PDF:en.
  - BAS webbsida säger fortfarande att konton "som är tillräckliga för de flesta företag [är] markerade med en fyrkant ■" (https://www.bas.se/kontoplaner/skapa-en-egen-kontoplan/). I Excel och JSON motsvaras det av kolumnen "Lämplig för basuppsättning" respektive fältet `isBasic`.
  - Fritt konto anges som till exempel "1230 (Fritt konto för …)".
- **Konton per företagsform:** samma kontonummer kan ha olika namn och användning per företagsform. Exempel: 2010 "Eget kapital" och 2019 "Årets resultat, delägare 1" för enskild firma och handelsbolag, mot 2081 "Aktiekapital" för aktiebolag. Excel-kolumnen heter "Företagsformer" och JSON-fältet `validForOrganizationTypes`.

### B2. Ändringar 2026 jämfört med 2025 och hur de publiceras
- **Publicering:** för 2026 finns ingen jämförelsesida på webben, bara en Excel-fil: "För 2026 presenterar vi en Excelfil med alla ändringar då det är relativt omfattande." https://www.bas.se/kontoplaner/jamfor-kontoplaner/ → https://www.bas.se/wp-content/uploads/2026/01/Kontoplansforandringar_2026.xlsx [V]
- **Filens flikar:** "Konton 2026" med 1 290 rader, "Konton 2025" med 1 230 rader och SRU-kolumner, samt "Ändr. i kontoplanen just 260114" med 272 rader.
- **Min räkning på den sista fliken:** 113 nya konton, 54 borttagna, 105 med ändrat namn [V, egen räkning].
  - **Nytt**, exempel: omstrukturerat 1090–1099 och 121x/122x; 1686; Periodiseringsfond 2027 (2127/2137); ett helt nytt inköpsträd 4010–4099 och 4300–4890 med varianter för omvänd skattskyldighet, EU och import per momssats; detaljerade fordonskonton 5621–5699.
  - **Borttaget**, exempel: 1213, 1223–1259 (bil- och datorkonton), 1580, 2128/2138 (periodiseringsfond 2018), 2331/2332, 4790, 5211–5252, 5490–5493, 6213–6215 (mobilsökning, fax, telex), 6410, 7833–7835.
  - Bakgrunden är bland annat BFN:s ändrade K2/K3 [S: https://www.faronline.se/nyheter/2026/januari/forandringar-i-bas-kontoplan-2026/].
- **Rytm och versioner:**
  - Kontoplanen uppdateras årligen ("BAS-kontoplanen uppdateras årligen", https://www.bas.se/kontoplaner/allmant-om-kontoplanen/).
  - Revisioner under året förekommer: ändringsfliken är justerad 2026-01-14, och PDF och xlsx "v2" laddades upp i april 2026 med intern rubrik "v 1.1".
  - Hur ofta v2-revisioner kommer finns inte beskrivet [EJ VERIFIERAT].
- **Kommande:** projektet "Framtidens kontoplan" fortsätter med en större omstrukturering. https://www.bas.se/kontoplaner/framtidens-kontoplan/

### B3. Maskinläsbara format och SRU-koppling [V]
- **Gratis att ladda ner:**
  - BAS 2026 som PDF och XLSX
  - en engelsk översättning
  - BAS 2018 för K1 och "K1 Mini" som PDF och XLS
  - https://www.bas.se/kontoplaner/
- **Mot betalning:** "BAS-kontoplanen 2026 i JSON-format" för **4 000 kr exkl. moms**. https://www.bas.se/kontoplaner/kontoplanen-i-maskinlasbart-format/
  - Spec: "Kontoplan Dataformat", mars 2026, https://www.bas.se/wp-content/uploads/2026/04/Kontoplan-Dataformat.pdf
  - En fil, `listOfAccounts.json`, i UTF-8 med **1 283 konton** och 14 fält per konto: `accountNumber`, svenskt och engelskt namn, `isBasic` (380 konton), `allowedInK2` (1 253), `validForOrganizationTypes`, normal transaktion och normalsaldo, `newInstruction`, `isSubAccount`, under-, relaterade och motkonton samt `SRU` med koder för NE, INK2, INK3 och INK4.
  - Dokumentet anger INK3 = handelsbolag och INK4 = ekonomiska föreningar. Det verkar vara omkastat jämfört med Skatteverkets blanketter, där INK3 avser ideella föreningar och stiftelser och INK4 handelsbolag. Detta bör kontrolleras.
- **SRU-tabeller som PDF och XLSX:** https://www.bas.se/kontoplaner/sru/
  - INK2 i grupperad form och som intervall, daterad 2024-11-19
  - INK3, INK4
  - NE för K1 (2020) och NE för övriga (2023)
  - Det finns ingen 2026-märkt SRU-tabell. Den senaste INK2-tabellen är från 2024.
- **SIE-fil med kontoplanen:** finns inte på bas.se [EJ VERIFIERAT att någon finns].
- **Oberoende av BAS:** Skatteverket publicerar fältkoderna per blankett (INK2R, INK2S, NE m.fl.) som Excel i en zip-fil. Det är bara kopplingen *konto → SRU* som är BAS:s arbete. Se E1.

### B4. Licens och upphovsrätt – möjlig blockerare [V, ordagranna citat]
**"Allmänna villkor – BAS-kontogruppen i Stockholm AB", version 2026-04-14**, https://www.bas.se/wp-content/uploads/2026/04/Allmanna-Villkor-2026.pdf. Villkoren gäller *den maskinläsbara (JSON) kontoplanen*:
- **2.1:** "…får användas för eget bruk i Användarens interna verksamhet eller vidarelicenseras till tredje part genom integrering med av Användaren tillhandahållna affärssystem, i enlighet med punkten 4.3. den Maskinläsbara BAS-kontoplanen får inte användas på annat sätt eller för andra ändamål."
- **3.2:** "När BAS-kontopgruppen tillgängliggör en ny årsversion … får den tidigare inte användas för utveckling av nya produkter eller för försäljning i produkter där den nya versionen av kontoplanen ska användas."
- **4.1:** "icke-exklusiv, icke-överlåtbar och icke-vidareupplåtbar rätt".
- **4.3:** "…rätt att integrera Maskinläsbara BAS-kontoplanen i sådant affärssystem för vidarelicensering till tredje part … rätt att göra mindre anpassningar av innehållet, såsom till exempel förkortningar av kontonamn…"
- **4.4:** "Annan användning … förutsätter BAS-kontogruppens skriftliga godkännande."
- **4.5:** "Användaren ska inte (a) obehörigen kopiera, sprida, eller bearbeta, exempelvis genom översättning, innehåll … eller (b) underlicensiera, hyra ut, låna ut, eller på annat sätt låta obehörig tredje part få tillgång…"
- **4.6:** BAS får granska användningen.
- **6.2:** upphovsrättsmärkningar får inte tas bort.

**Bedömning:**
- **JSON-varianten:** villkoren är knutna till en namngiven kund och ger rätt att *vidarelicensiera* men inte att sprida fritt. Det går inte ihop med GPL:s krav på fri vidarespridning. Den kan inte läggas i GnuCash källkod.
- **PDF och Excel:** filerna har ingen licenstext och ingen ©-märkning. Jag sökte efter "copyright", "upphovs", "©" och "licens" i båda och fick inga träffar. Sidan säger bara "Kontoplanen … kan laddas ner här." Ingen licens betyder inte att upphovsrätten är avstådd. Om en kontoplan med nummer och namn skyddas av upphovsrätt eller katalogskydd är en juridisk fråga som inte är utredd här [EJ VERIFIERAT].
- **GnuCash har redan BAS 2019:** filerna `data/accounts/sv_SE/bas_2019_k1.gnucash-xea` och `bas_2019_k2_grund.gnucash-xea` (409 konton) lades in i commit 2020-01-03, "L12N:sv_SE: Update account hierarchies according to BAS 2019" (https://github.com/Gnucash/gnucash/tree/stable/data/accounts/sv_SE) [V]. Filerna innehåller bara texten "För mer information om BAS kontoplaner se https://www.bas.se/". Något dokumenterat tillstånd från BAS hittade jag inte.
- **Rekommendation:** begär skriftligt tillstånd enligt punkt 4.4 via info@bas.se innan en BAS 2026-mall läggs in.

### B5. Antal konton och officiella urval
- **Antal:** 1 283 konton enligt JSON-specen. Excel-fliken har 1 290 rader, eftersom vissa konton förekommer flera gånger för olika företagsformer. Min räkning i PDF:en gav cirka 1 275.
- **Urval:**
  - "basuppsättning": 380 konton enligt JSON-specen, 388 rader i Excel
  - "BAS 2018 för K1" och "K1 Mini" (ett konto per rad i årsbokslutet)
  - Bokföringsboken och Bokslutsboken säljs som böcker
- Någon officiell "BAS Mini 2026" finns inte [EJ VERIFIERAT].

---

## C. SIE

### C1. Typer, version, teckenuppsättning och poster [V]
**Aktuell specifikation är *SIE filformat, utgåva 4C, 2025-08-06*,** inte 4B (2008-09-30). https://sie.se/wp-content/uploads/2026/02/SIE_filformat_ver_4C_2025-08-06.pdf
- Punkt 1.7: "I denna uppdatering … har inga förändringar gjorts gällande själva filformatet."
- 4C innehåller förtydliganden, exempel enligt BAS 2025 och en ny godkännandeprocess.

**Filtyper** (spec kap. 4 och https://sie.se/format/):

| Typ | Innehåll |
|---|---|
| 1 | Årssaldon (IB, UB, resultat). Tillåts främst av historiska skäl; punkt 4.4 rekommenderar minst typ 2 i nya implementationer. |
| 2 | Som typ 1 plus periodsaldon och periodbudget (#PSALDO, #PBUDGET). |
| 3 | Som typ 2 plus saldon per objekt (#DIM, #OBJEKT, #OIB, #OUB). |
| 4E | Export med alla verifikationer (#VER, #TRANS). |
| 4I | Import av verifikationer från försystem. Filändelse `.SI`; övriga typer `.SE`. |

**Allmänna regler:**
- Punkt 5.8: teckenrepertoaren är IBM PC 8-bitars extended ASCII, **codepage 437**. `#FORMAT PC8` är det enda tillåtna värdet.
- Punkt 5.5: varje post avslutas med LF; CR före LF tillåts.
- Punkt 5.7:
  - Fälten skiljs åt av mellanslag eller TAB.
  - Citattecken behövs bara om fältet innehåller mellanslag.
  - Ett citattecken inne i ett fält skrivs `\"`.
  - Kontrolltecken (ASCII 0–31 och 127) är förbjudna.
- Punkt 5.4: underposter skrivs inom `{` och `}`, och klamrarna står ensamma på var sin rad.
- Punkt 5.9: belopp med punkt som decimaltecken, högst två decimaler, minustecken före beloppet, inget plustecken.
- Punkt 5.10: datum som ÅÅÅÅMMDD.
- Belopp: "Kreditsaldo anges med negativt belopp", det vill säga debet positivt.
- Punkt 5.12, ordning: flaggpost → identifikationsposter → kontoplan → saldo- och verifikationsposter.
- Kap. 12: fälten är positionsbestämda. Ett tomt fält före ett ifyllt skrivs `""`.
- Punkterna 7.1–7.3: okända etiketter och okända extrafält i slutet av en post ska ignoreras av läsaren.
- Punkt 7.2: bara etiketter som finns i standarden får skrivas.
- Punkterna 2.1–2.2: ett program som exporterar bara en delmängd av de obligatoriska posterna "följer inte SIE-standarden och får inte marknadsföras som ett sådant".

**Obligatoriska poster** (kap. 6; ● = obligatorisk, o = frivillig, – = förbjuden):

| Post | 1 | 2 | 3 | 4I | 4E |
|---|---|---|---|---|---|
| #FLAGGA, #PROGRAM, #FORMAT, #GEN, #FNAMN | ● | ● | ● | ● | ● |
| #SIETYP | o | ● | ● | ● | ● |
| #RAR | ●† | ●† | ●† | o | ●† |
| #KONTO | ● | ● | ● | o | ● |
| #SRU | ● | ● | o | o | o |
| #IB, #UB, #RES | ●† | ●† | ●† | – | ●† |
| #PSALDO, #PBUDGET | – | ● | ● | – | o |
| #DIM, #OBJEKT | – | – | ● | o | o |
| #OIB, #OUB | – | – | ●† | – | o |
| #VER, #TRANS, #RTRANS, #BTRANS | – | – | – | o | o |
| #ORGNR, #KPTYP, #KTYP, #KSUMMA, #TAXAR, #FTYP, #VALUTA | o | o | o | o | o |

- † betyder att posterna ska finnas för innevarande och föregående år om föregående år finns.
- Punkt 5.16: #UB för år 0 ska alltid finnas, liksom antingen #UB för år −1 eller #IB för år 0.
- Punkt 5.17: saldoposter med nollbelopp kan utelämnas.

**Syntax** (kap. 11–12):
```
#FLAGGA x
#PROGRAM programnamn version
#FORMAT PC8
#GEN datum sign
#SIETYP typnr
#FNAMN företagsnamn
#ORGNR orgnr förvnr verknr          (orgnr med bindestreck efter sjätte siffran)
#RAR årsnr start slut                (0 = innevarande, -1 = föregående …)
#KPTYP typ                           (BAS95|BAS96|EUBAS97|NE2007; "BAS2xxx" tolkas som EUBAS97)
#KONTO kontonr kontonamn
#KTYP kontonr T|S|K|I
#SRU konto SRU-kod                   (flera poster per konto tillåtna)
#IB / #UB årsnr konto saldo kvantitet
#RES årsnr konto saldo kvantitet
#PSALDO / #PBUDGET årsnr period(ÅÅÅÅMM) konto {dim obj} saldo kvantitet
#DIM dimnr namn    #UNDERDIM dimnr namn superdim    #OBJEKT dimnr objnr namn
#OIB / #OUB årsnr konto {dim obj} saldo kvantitet
#VER serie vernr verdatum vertext regdatum sign
{
#TRANS kontonr {objektlista} belopp transdat transtext kvantitet sign
#RTRANS …   (tillagd rad; ska direkt följas av en identisk #TRANS)
#BTRANS …   (borttagen rad)
}
#KSUMMA [crc]
```
- Verifikationens rader ska summera till noll.
- Inom en serie skrivs verifikationerna i stigande nummerordning.
- Serie och nummer får vara tomma i 4I; mottagaren numrerar då själv.
- **Reserverade dimensioner:** 1 kostnadsställe, 2 kostnadsbärare, 6 projekt, 7 anställd, 8 kund, 9 leverantör, 10 faktura. Från 20 är dimensionerna fria.
- **#KSUMMA:** CRC-32 med polynom EDB88320H, start FFFFFFFFH, inverteras i slutet. Den beräknas på etiketter och fält utan blanksteg, citattecken och klamrar, i CP437. Spec kap. 10 har C-kod.
- **#RTRANS och #BTRANS** passar direkt för att föra över GnuCash rättelsehistorik.

### C2. SIE 5 [V]
- Specifikation: "SIE 5 – Ett nytt filformat för överföring av generiska redovisningsdata", 2016-12-09. https://sie.se/wp-content/uploads/2020/08/SIE-5-rev-161209-konsoliderad.pdf
- XSD: https://sie.se/sie5.xsd. Exempelfiler: https://sie.se/wp-content/uploads/2020/07/Sample-files.zip
- Formatet är XML med engelska etiketter, reskontror, anläggningsregister och bifogade dokument.
- **Signatur krävs:** "Varje program som skriver filer av typ SIE 5 ska skriva minst en elektronisk signatur och ska bifoga signaturernas certifikat i SIE-filen" (XMLDsig). Certifikatet får vara självsignerat. Bokföringsorder behöver ingen signatur.
- SIE 4 är fortfarande den de facto-standard som SIE-gruppen själv beskriver. Hur spritt SIE 5 är i dag har jag inte kunnat belägga [EJ VERIFIERAT]. Inget lagkrav på SIE finns. SIE 4 räcker i praktiken.

### C3. Licens, godkännande och testverktyg [V]
- **Licens:**
  - Spec punkt 1.1: "Formatbeskrivningen är öppen för alla att använda."
  - sie.se: "…men enbart SIE-föreningens medlemmars programvaror kan bli godkända."
  - Spec punkt 3.2: godkännande "ges inte för andra som implementerar stöd för SIE".
  - Punkt 3.5: godkännandet ger rätt att använda SIE-logotypen med angivna typer, till exempel "1, 2, 4, 4L".
  - Någon formell licens som CC eller liknande finns inte i dokumentet.
- **Kalla sig SIE-kompatibel:** spec kap. 2 kräver att alla obligatoriska poster för den angivna typen skrivs. Logotyp och godkännande kräver medlemskap. Medlemmarna är företag; föreningsformen för ett OSS-projekt är [EJ VERIFIERAT].
- **Testverktyg:** onlinetest på https://sietest.sie.se/. Exempelfiler: https://sie.se/wp-content/uploads/2024/01/SIE4-Exempelfil-Sample-file-1.zip och SIE 5-exemplen ovan.

### C4. Öppna implementationer [V via GitHub API]

| Projekt | Språk | Licens | Anmärkning |
|---|---|---|---|
| mfatgg/GnuCash-SIE4-export | Python | ingen angiven | SIE4-export från GnuCash-databas, senast ändrad 2024 |
| idstam/jsisie | C# | MIT | SIE-parser för .NET |
| blinfo/Sie4j | Java | GPL-3.0 | läser och skriver, aktiv 2026 |
| byrokrat/accounting | PHP | GPL-3.0 | SIE m.m. |
| barsoom/sie | Ruby | ingen i GitHub-metadata | parser och generator |
| johanwilfer/siephp | PHP | MIT | export, delmängd |
| magnusfroste/sie-parser | JS | MIT | |
| solidfox/sie-parser | Python | MIT | |
| JonasNorling/sie4parser | Python | ingen angiven | |
| akeamc/sie4 | Rust | ingen angiven | |

Jag hittade inget C- eller C++-bibliotek. Kvaliteten på projekten har jag inte granskat.

---

## D. Moms

### D1. Rutor, XML-element, API-fält och BAS-konton
Rutor och element: Skatteverkets tjänstebeskrivning för momsdeklarations-API:t, avsnitt 4.1.1.3 [V]. BAS-kolumnen är **min egen härledning ur kontonamnen i BAS 2026**. Någon officiell tabell BAS-konto → ruta har jag inte hittat.

| Ruta | Benämning | XML-element (eSKD) | API-fält | Typiska BAS 2026-konton |
|---|---|---|---|---|
| 05 | Momspliktig försäljning | ForsMomsEjAnnan | momspliktigForsaljning | 3001, 3002, 3003, (3106) |
| 06 | Momspliktiga uttag | UttagMoms | momspliktigaUttag | uttagskonton (moms 2612/2622/2632) |
| 07 | Vinstmarginalbeskattning | UlagMargbesk | vinstmarginal | 3211, 3212 (moms 2616/2626/2636) |
| 08 | Hyresinkomster, frivillig skattskyldighet | HyrinkomstFriv | hyresInkomst | moms 2613/2623/2633 |
| 10–12 | Utgående moms 25/12/6 % | MomsUtgHog/Medel/Lag | momsForsaljningUtgaendeHog/Medel/Lag | 2610–2613, 2620–2623, 2630–2633 |
| 20 | Inköp av varor från EU | InkopVaruAnnatEg | inkopVarorEU | 4515–4517, 4075–4077 |
| 21 | Inköp av tjänster från EU | InkopTjanstAnnatEg | inkopTjansterEU | 4535–4537 |
| 22 | Inköp av tjänster utanför EU | InkopTjanstUtomEg | inkopTjansterUtanforEU | 4531–4533 |
| 23 | Inköp av varor i Sverige, omvänd skattskyldighet | InkopVaruSverige | inkopVarorSE | 4415–4417, 4065–4067 |
| 24 | Inköp av tjänster i Sverige, omvänd skattskyldighet | InkopTjanstSverige | inkopTjansterSE | 4425–4427 |
| 30–32 | Utgående moms på inköp | MomsInkopUtgHog/Medel/Lag | momsInkopUtgaende… | 2614, 2624, 2634 |
| 35 | Varor till EU | ForsVaruAnnatEg | forsaljningVarorEU | 3108 |
| 36 | Varor utanför EU | ForsVaruUtomEg | forsaljningVarorUtanforEU | 3105 |
| 37/38 | Trepartshandel | InkopVaruMellan3p / ForsVaruMellan3p | inkopVaror3pHandel / forsaljningVaror3pHandel | – |
| 39 | Tjänster till EU (huvudregeln) | ForsTjSkskAnnatEg | forsaljningTjansterEU | 3308 |
| 40 | Övriga tjänster utomlands | ForsTjOvrUtomEg | ovrigForsaljningTjansterUtanforSE | 3305 |
| 41 | Köparen skattskyldig i Sverige | ForsKopareSkskSverige | forsaljningBskKopareSE | 3231 |
| 42 | Övrig försäljning | ForsOvrigt | momsfriForsaljning | 3004 m.fl. |
| 48 | Ingående moms | MomsIngAvdr | ingaendeMomsAvdrag | 2640–2649 |
| 49 | Moms att betala eller få tillbaka | MomsBetala | summaMoms | motbokas på 2650 |
| 50 | Beskattningsunderlag vid import | MomsUlagImport | import | 4545–4547, 4085–4087 |
| 60–62 | Utgående moms på import | MomsImportUtgHog/Medel/Lag | momsImportUtgaende… | 2615, 2625, 2635 |

### D2. Skatteverkets API och filalternativet
**API Momsdeklaration, version 1.0** [V].
- Tjänstebeskrivning (dokumentversion 1.5): https://www7.skatteverket.se/portal-wapi/open/apier-och-oppna-data/utvecklarportalen/v1/getFile/tjanstebeskrivning-momsdeklaration-v1-0/pdf/1.0.7/Tjanstebeskrivning-Momsdeklaration-v1.pdf
- Allmänna villkor (version 1.0, gäller från 2025-11-20): https://www7.skatteverket.se/portal-wapi/open/apier-och-oppna-data/utvecklarportalen/v1/getFile/allmanna-villkor-momsdeklaration

**Operationer:**
- Spara utkast: `POST /utkast/{redovisare}/{period}`, där perioden anges ÅÅÅÅMM och redovisaren med 12 siffror.
- Kontrollera: `POST /kontrollera/{redovisare}/{period}`.
- Lås: `PUT /las/…`.
- Lås upp, radera (`DELETE /utkast/…`), hämta (`GET` eller `POST /utkast`).
- Hämta inlämnade uppgifter och hämta beslutade uppgifter.

**Underskrift:**
- Den görs inte via API:t. "Det är alltså inte möjligt att skriva under direkt via API."
- Lås-operationen returnerar en djuplänk. Sedan augusti 2026 går länken till Skatteverkets e-tjänst "Skriv under digitalt", där en behörig person loggar in med e-legitimation och kan skriva under flera utkast samtidigt (Utvecklarportalens RSS 2026-08-26).
- Utkast tas bort efter 30 dagar om ingen arbetat med dem.

**Behörighet för slutanvändaren:** firmatecknare, deklarationsombud, ombudsrollen "Momsdeklaration" eller representant för enkla bolag och partrederier.

**Autentisering (OAuth2):**
- Authorization Code Grant: client ID + client secret + organisationslegitimation (Expisoft) eller e-legitimation.
- Client Credentials Grant: med organisationslegitimation.
- Varje anrop kräver dessutom APIgw client ID och secret samt headern `skv_client_correlation_id`.
- Redirect-URI:n måste vara förregistrerad (format `https://domännamn[:port]/[resurs]`). Dokumentationens exempel använder `https://localhost/test`.
- Skatteverket skriver om ACG: "används främst när den anropande programvaran är serverbaserad".
- Ändpunkter för organisationslegitimation: `orgoauth2[.test].skatteverket.se`. För e-legitimation: `peroauth2[.test].skatteverket.se`. Refresh token gäller 65 minuter.
- Källa: https://www.skatteverket.se/omoss/digitalasamarbeten/borjaanvandaapier/authorizationcodegrantacg.4.96cca41179bad4b1aa8f05.html [V]

**Testmiljö:** en "komplett testtjänst" (sandbox) med dynamiska testdata. Nycklar beställs via formulär, men användarvillkoren måste godkännas.

**Villkor för produktion** (allmänna villkor punkt 4.1):
- Avtal med Skatteverket krävs.
- "Programvaruföretaget" ska vara registrerat i aktiebolags- eller handelsregister, momsregistrerat och godkänt för F-skatt. Det "kan även vara enskild näringsidkare" med F- eller FA-skatt och momsregistrering.
- Inga skulder hos Kronofogden.
- Personuppgifter som tas emot ska behandlas i en miljö inom EU/EES (punkt 4.2).
- Användarna ska informeras om att uppgiftsskyldigheten är fullgjord först när deklarationen är underskriven (punkt 5).
- Skatteverket får kontrollera efterlevnaden och stänga av programvaruföretaget (punkterna 6 och 12).

**Alternativ utan avtal – filuppladdning:** i e-tjänsten "Lämna momsdeklaration" finns valet "Deklarera via fil", där filen ska vara XML. https://www.skatteverket.se/sdginstruktioner/howtouseoureservicefilevatreturn.4.7da1d2e118be03f8e4fc83.html [V]
- DOCTYPE (enligt Microsofts dokumentation): `<!DOCTYPE eSKDUpload PUBLIC "-//Skatteverket, Sweden//DTD Skatteverket eSKDUpload-DTD Version 6.0//SV" https://www1.skatteverket.se/demoeskd/eSKDUpload_6p0.dtd>` [S: https://learn.microsoft.com/en-us/dynamics365/finance/localizations/emea-swe-vat-declaration-sweden].
- Länken till DTD:n gav **404** när jag försökte hämta den. Själva DTD-texten, de obligatoriska elementen och vad Skatteverket i dag har för specifikation av filformatet: [EJ VERIFIERAT].

### D3. Redovisningsperioder och förfallodagar [V]
Källa: https://www.skatteverket.se/foretag/moms/deklareramoms/narskajagdeklareramoms.4.6d02084411db6e252fe80008988.html

**Perioder efter beskattningsunderlag:**
- Högst 1 mkr: helår, alternativt kvartal eller månad.
- Högst 40 mkr: kvartal, alternativt månad.
- Över 40 mkr: månad.

**Förfallodagar:**
- **Kvartal:** den 12:e i andra månaden efter perioden; i augusti den 17:e.
- **Månad, högst 40 mkr:** den 12:e i andra månaden efter perioden; i januari och augusti den 17:e.
- **Månad, över 40 mkr:** den 26:e i månaden efter perioden; i december den 27:e.
- **Helår, enskild näringsidkare utan EU-handel:** 12 maj året efter (26 juni med byråanstånd).
- **Helår, enskild näringsidkare med EU-handel:** 26 februari.
- **Helår, aktiebolag utan EU-handel:** i anslutning till inkomstdeklarationen enligt Skatteverkets tabell. Exempel: räkenskapsår som slutar september–december ger 12 juli på papper och 17 augusti digitalt.
- **Förseningsavgift:** 625 kr.

---

## E. Inkomstdeklaration

### E1. INK2 med INK2R och INK2S, NE till INK1, samt SRU-filformatet [V]
Källor:
- Teknisk information om filöverföring (ersätter SKV 269 från 2024-01-01): https://www.skatteverket.se/foretag/inkomstdeklaration/forredovisningsbyraer/tekniskinformationomfiloverforing.4.13948c0e18e810bfa0cca8.html
- Zip-fil "Nyheter from beskattningsperiod 2026P4" (dokument ändrat 2026-11-09): https://www.skatteverket.se/download/18.5fb2fa71a0b0adb74a564b/1794382662598/_Nyheter_from_beskattningsperiod_2026P4.zip

**Zip-filens innehåll:**
- fältnamnstabeller per blankett: `INK2_`, `INK2R_`, `INK2S_SKV2002-34-01-26-04.xls`, `NE_SKV2161-14-02-26-02.xls`, `INK1_…`, `N3A`, `N8`, `T1`, `T2`, `K10` m.fl.
- `Vardeforrad_2027.xls`, `Filexempel.xlsx`
- en PDF med blankettbilder och fältnamn

**Blankettblock:** INK2-2026P4, INK2R-2026P4 (sid 2–3, räkenskapsschema), INK2S-2026P4 (sid 4, skattemässiga justeringar), NE-2026P4 (sid 1–2) och INK1-2026P4. INK1 och INK2 lämnas som *utkast* som sedan måste signeras på Mina sidor.

**Filformat:**
- Två filer: `INFO.SRU` (alternativt `INFOSRU`) och `BLANKETTER.SRU`.
- Teckenkodning **ISO 8859-1**, högst 5,0 MB.
- Varje post skrivs `#POST<WS>värde<LF>`. Belopp utan tecken och utan inledande nollor; negativa belopp med "-". Person- och organisationsnummer med 12 siffror (organisationsnummer med prefix 16).
- **INFO.SRU, i fast ordning:**
  `#DATABESKRIVNING_START`, `#PRODUKT SRU`, (`#MEDIAID`), (`#SKAPAD`), (`#PROGRAM`), `#FILNAMN BLANKETTER.SRU`, `#DATABESKRIVNING_SLUT`, `#MEDIELEV_START`, `#ORGNR`, `#NAMN`, (`#ADRESS`), `#POSTNR`, `#POSTORT`, (`#AVDELNING`/`#KONTAKT`/`#EMAIL`/`#TELEFON`/`#FAX`), `#MEDIELEV_SLUT`
- **BLANKETTER.SRU, per blankettblock:**
  `#BLANKETT <typ>`, `#IDENTITET <orgnr> <datum> [tid]`, (`#NAMN`), `#UPPGIFT <fältkod> <värde>`…, högst en `#SYSTEMINFO`, `#BLANKETTSLUT`, och sist i filen `#FIL_SLUT`.
- **Fel:**
  - Nivå 1 avvisar hela sändningen.
  - Nivå 2 avvisar enskilda block.
  - Ett senare framställningsdatum ersätter ett tidigare.
  - Rättelser ska lämnas som kompletta blankettblock.
- Inloggning med e-legitimation. Testfiler kan lämnas ungefär en månad innan produktionen öppnar.
- **Viktigt:** en filöverförd inkomstdeklaration räknas som inlämnad först när den skrivits under på Mina sidor. Bilagor räknas som inlämnade direkt.

### E2. API eller filöverföring
**API Inkomstdeklaration 2–4, version 1.0** (tjänstebeskrivning 1.02, 2025-10-24) [V]. https://www7.skatteverket.se/portal-wapi/open/apier-och-oppna-data/utvecklarportalen/v1/getFile/tjanstebeskrivning-inkomstdeklaration2-4-v1-02
- **Operationer:**
  - Lämna inkomstdeklaration till eget utrymme. Underlaget är **XBRL** enligt taxonomin på taxonomier.se, packat i JSON, högst 1 MB. Svaret innehåller en djuplänk till "Granska och skicka in" på Mina sidor.
  - Hämta förifylld inkomstdeklaration, som SRU eller PDF.
  - Hämta status.
- **Behörighet:** ingen ombudsbehörighet krävs för att lämna till eget utrymme, men underskrift och hämtning kräver ombudsroll.
- Stöds från deklarationsperiod 2020P4 för INK2.
- Allmänna villkor (version 1.0, från 2025-05-15) har samma krav på programvaruföretaget som momsvillkoren: registrering, F- eller FA-skatt, enskild näringsidkare tillåten.

**API Inkomstdeklaration 1:** finns med i Skatteverkets lista över auktorisationsflöden per API (ACG och CCG). https://www.skatteverket.se/omoss/digitalasamarbeten/borjaanvandaapier/auktorisationsflodenperapi.4.7da1d2e118be03f8e4f8246.html [V]. Tjänstebeskrivningen kunde jag inte hämta, så om API:t täcker NE-bilagan är [EJ VERIFIERAT].

**Utan avtal:** e-tjänsten **Filöverföring** med SRU-filer för INK1, INK2 och bilagor (enligt E1). Det kräver bara e-legitimation.

### E3. Rättslig vägledning, sida 3205
Sidan https://www4.skatteverket.se/rattsligvagledning/edition/2026.14/3205.html gick inte att läsa.
- curl och WebFetch gav "Request Rejected" respektive "Please enable JavaScript".
- Rendering via TinyFish gav tomt innehåll.
- Wayback Machine hade inga sparade kopior.

Vad sidan handlar om och vilka slutsatser Skatteverket drar där: **[EJ VERIFIERAT]**. Jag gissar inte. Den behöver öppnas manuellt i en vanlig webbläsare.

---

## F. Årsredovisning till Bolagsverket

### F1. Vem kan lämna digitalt, och är det obligatoriskt?
- Bolagsverket (sidan uppdaterad 2026-07-27): "det för närvarande endast är **aktiebolag** som kan lämna in årsredovisningen, och när så krävs revisionsberättelsen, digitalt." Det kräver ett program eller en tjänst som stöder digital inlämning. https://bolagsverket.se/sjalvservice/etjanster/lamnainarsredovisningendigitalt.1663.html [V]
- Ekonomiska föreningar lämnar på papper även för räkenskapsår 2026, enligt nyhet 2026-08-28 [V].
- **Obligatorium:** en promemoria från december 2022 föreslog obligatorisk digital inlämning [V, https://www.regeringen.se/contentassets/d80917262ab14de5bbff617fcbda2b2c/digital-ingivning-av-arsredovisningar.pdf]. Sekundärkällor säger att förslaget inte genomförts och att pappersinlämning fortfarande är tillåten [S]. Att inget lagkrav har beslutats är **[EJ VERIFIERAT mot SFS]**, men Bolagsverkets egen formulering tyder på att digital inlämning är frivillig.
- Enskild firma lämnar ingen årsredovisning till Bolagsverket. Taxonomier för "Årsbokslut" och "Förenklat årsbokslut" har status "okänd", alltså ingen planerad.

### F2. Format och taxonomier [V]
Formatet är **Inline XBRL (iXBRL, XHTML)** med instansdokument. Taxonomier med status FINAL (https://www.taxonomier.se/taxonomier-info-final.html):

| Taxonomi | Versioner |
|---|---|
| Årsredovisning K2 för aktiebolag | 2024-09-12, 2021-10-31 (även ek. för., HB/KB och filialer 2024-09-12) |
| Årsredovisning K3 för aktiebolag | 2021-10-31, 2020-12-01 |
| Koncernredovisning K3 | 2021-10-31, 2020-12-01 |
| Revisionsberättelse för aktiebolag (med eller utan koncern) | 2020-12-01 (och 2017-09-30) |
| Fastställelseintyg för års- och koncernredovisning | 2020-12-01; ESEF 2022-09-01 |
| Utökad information | 2017-09-30 |
| INK3/INK4 | 2026-08-31 (INK2: versionsnumret syntes inte på sidan) |

- Om en K2-taxonomi som är anpassad till K2-ändringarna 2026 är på väg: [EJ VERIFIERAT].
- **Exempel:** https://www.taxonomier.se/taxonomier-k2-exempel.html. Bolagsverkets exempel 1 som XHTML, XBRL och PDF: https://xbrl.taxonomier.se/se/exempel/arsredovisning/exempel-1/arsredovisning-exempel-1.xhtml
- **Krav på utseende** (https://bolagsverket.se/apierochoppnadata/lamnaforetagsinformation/apifordigitalinlamningavarsredovisningochrevisionsberattelse/forutsattningarvidinforandeavelektroniskaarsredovisningshandlingariinlinexbrlformat.5957.html):
  - iXBRL-filen ska ge en "väl disponerad utskrift med sidbrytningar" både i webbläsare och som PDF i testbänken.
  - Det synliga innehållet och instansdokumentet ska stämma överens.
  - sign-attribut, format-attribut och Transformation Registry ska användas.
  - Exemplen visar CSS för utskrift (`@media print`).
- **Bolagsverkets dokument** (https://bolagsverket.se/apierochoppnadata/lamnaforetagsinformation/apifordigitalinlamningavarsredovisningochrevisionsberattelse/tekniskdokumentationfordigitalinlamningavarsredovisning.5937.html):
  - Tekniskt ramverk 1.2
  - Teknisk guide 3.6
  - Anslutningsanvisningar 1.8
  - Aviseringar 2.2
  - Tillämpningsanvisning iXBRL 1.8
  - Kombinationer av taxonomirapporter 1.4

  Jag kunde inte läsa PDF:erna, eftersom Bolagsverket visar en CAPTCHA för automatiserad hämtning. Jag försökte inte kringgå den.

### F3. API, anslutning, underskrift och arkitekturfrågan
**API för inlämning av digitala årsredovisningar, version 2.1** (Swagger 2.0) [V]. https://media.bolagsverket.se/diar/services/2.1/lamnaInArsredovisning-2.1.yaml
- Host `api.bolagsverket.se`, basePath `/lamna-in-arsredovisning/v2.1`.
- Operationer:
  - `POST /skapa-inlamningtoken` (anrop med personnummer och organisationsnummer)
  - `POST /kontrollera/{kontrolltoken}`
  - `POST /inlamning/{inlamningtoken}`: lägger årsredovisningen i eget utrymme; "årsredovisningen ska skrivas under senare via webben"
- Tillhörande tjänster (version 2025-02-13): årsredovisningshändelser 2.0, information 1.4, prenumeration 2.0, kontrollsumma 1.1, statistik 1.0.

**Underskrift:** användaren laddar upp, anger personnummer och e-post till en styrelseledamot eller VD, och den personen "loggar in med sin e-legitimation, skriver under fastställelseintyget och skickar in" i Bolagsverkets e-tjänst [V].

**Autentisering av programvaran:** om det sker med klientcertifikat, och i så fall vem som utfärdar det, står i de PDF:er jag inte kunde läsa **[EJ VERIFIERAT]**.

**Anslutningsprocessen (sidan uppdaterad 2026-09-02)** [V]. https://bolagsverket.se/apierochoppnadata/lamnaforetagsinformation/apifordigitalinlamningavarsredovisningochrevisionsberattelse/anslutningtilltjanstenlamnainarsredovisningendigitalt.5936.html
1. Testbänk utan avtal. Den egna publika IP-adressen måste öppnas i Bolagsverkets brandvägg.
2. Uppstartsmöte. **Fullbokat 2026**, med kö till möten 2027 och kallelser tidigast februari 2027.
3. Avtal: "Vi skriver endast avtal med **juridisk person** där verksamhetsbeskrivningen är relevant för uppdraget."
4. Granskning av testfiler, som "ofta tar flera månader".
5. Produktion.

**Kan en GPL-skrivbordsapp där varje användare kör sin egen instans ansluta?**
- **Bolagsverket – nej i praktiken:**
  - avtalet tecknas bara med en juridisk person
  - leverantören granskas och godkänns
  - leverantörens IP-adresser vitlistas
  - en enskild användares app är ingen godkänd leverantör

  Möjliga vägar är en central tjänst som en juridisk person driver och som godkänts av Bolagsverket, eller export till en sådan tjänst, eller pappersinlämning.
- **Skatteverket – API:erna kräver:**
  - avtal med ett "Programvaruföretag" (aktiebolag, handelsbolag eller enskild näringsidkare med F-skatt och momsregistrering)
  - **hemliga** OAuth-nycklar (`client_secret` och APIgw-secret) samt förregistrerade redirect-URI:er
  - mottagning av personuppgifter inom EU/EES

  Hemliga nycklar kan inte ligga i öppen källkod eller distribuerade binärer.

  Två varianter: (a) en central "broker" som drivs av en juridisk person, till exempel en förening eller ett AB, och som har avtal och nycklar, med GnuCash som klient; (b) att användaren själv, med sitt eget AB eller sin enskilda firma med F-skatt och momsregistrering, tecknar ett eget avtal och registrerar egna nycklar och redirect-URI. Villkoren utesluter inte (b) i ord, men om Skatteverket godtar det är **[EJ VERIFIERAT]**.
- **Alternativ som fungerar utan avtal i dag:**
  - **moms:** XML-uppladdning i e-tjänsten (formatspecen saknas, se D2)
  - **inkomstdeklaration:** SRU-filöverföring
  - **årsredovisning:** papper
  - Underskrift sker alltid hos myndigheten med användarens egen e-legitimation, så GnuCash behöver inte vara BankID-förlitande part.

### F4. Öppna verktyg för svensk iXBRL
- **Arelle:** Apache 2.0, licensfil "Copyright © 2011-present Workiva, Inc." [V]. Validerar XBRL och iXBRL.
- **Färdig svensk iXBRL-generator med öppen licens:** hittade jag ingen. Närmast är digitalk2se/digitalk2-plugin (MIT, MCP-plugin för K2). Generatorn måste alltså byggas utifrån taxonomin och exemplen på taxonomier.se.

---

## G. Sammanfattning

### Prioriterad kravlista

**(i) Litet aktiebolag med K2**

*Måste enligt lag:*
1. Grund- och huvudbokföring med registreringsordning, period, verifikationsnummer, kontering och belopp per post (5:1 BFL, BFNAR 2.2–2.3).
2. **Låsning och "bokföring"** som gör posterna oföränderliga senast vid tidsgränserna i 5:2 BFL och BFNAR kap. 3.
3. **Rättelse bara via rättelseverifikation**, med uppgift om vem och när och en ömsesidig hänvisning (5:5, 5:9 BFL, BFNAR 2.17–2.18, 5.15–5.16).
4. Obrutna verifikationsserier som systemet kan kontrollera (BFNAR 5.9–5.10).
5. Verifikationsinnehåll enligt 5:7 BFL och koppling till underlag och bilagor.
6. Behandlingshistorik med registreringsdatum och logg över systemändringar (momssatser, nya konton, versioner) (5:11 BFL, BFNAR 9.16).
7. Systemdokumentation: export av kontoplan, kontosummering till bokslutet och samlingsplan; ersatta versioner sparas (BFNAR 9.2–9.4, 9.12).
8. Arkiv i sju år, utskrift på begäran, säkerhetskopiering och export till ett läsbart format (7 kap. BFL, BFNAR 8.2).
9. Räkenskapsår inklusive brutet och förlängt år, öppningsbalans och bokslutstransaktioner (3 kap., 4:3, 5:3 BFL).
10. Möjlighet att ta fram en årsredovisning enligt ÅRL och K2.

*Måste i praktiken:*
- BAS-kompatibel kontoplan med versionshantering och migrering mellan årgångar (2025 → 2026: 113 nya, 54 borttagna, 105 omdöpta konton).
- Momskoder per konto och momsrapport för rutorna 05–62.
- SRU-koppling per konto för INK2R och INK2S.
- Export i SIE 4 (minst typ 4E, gärna 1–3) och import av 4I.
- Export av momsdeklaration som XML och inkomstdeklaration som SRU-filer.

*Bra att ha:* iXBRL för K2, SIE 5, API-integration via en broker, dimensioner och objekt (SIE typ 3), anläggningsregister (BFNAR kap. 4).

**(ii) Enskild firma med förenklat årsbokslut (K1)**

*Måste enligt lag:* punkterna 1–9 ovan. Dessutom:
- kontantmetoden med bokföring av obetalda poster vid årets slut (5:2 tredje stycket)
- senareläggning enligt BFNAR 3.7–3.9
- förenklat årsbokslut enligt BFNAR 2006:1 med 2026-ändringarna (ruta B11, U1–U4 utgår)
- systemdokumentation kan utgå vid "enkelt system" (BFNAR 9.5), men inte om automatkontering används

*Måste i praktiken:*
- NE-koppling per konto
- export av NE-2026P4 som SRU
- momsdeklaration för helår med förfallodag 12 maj
- kontoplan med eget kapital för enskild firma (2010–2019 enligt BAS)

*Bra att ha:* K1-mall motsvarande "BAS K1 Mini", SIE-utbyte med redovisningskonsult.

### Juridiska och organisatoriska blockerare och möjliga vägar runt

| Blockerare | Möjlig väg |
|---|---|
| **BAS-licens** (JSON-licensen går inte ihop med GPL; PDF och Excel saknar licens) | Begär skriftligt tillstånd (punkt 4.4, info@bas.se) för en GPL-mall. Alternativt: leverera bara struktur, momskoder och en importfunktion som läser användarens egen nedladdade BAS-xlsx vid körning. Låt juridik bedöma om nummer och namn är skyddade. Ta upp att GnuCash redan har BAS 2019. |
| **BAS SRU-koppling** (BAS-verk, senaste INK2-tabell 2024) | Bygg kopplingen själv från Skatteverkets publika fältnamnstabeller (zip-filen för 2026P4). Användaren bekräftar SRU per konto. |
| **SIE-godkännande och logotyp** (bara medlemmar) | Implementera hela specen och skriv "läser och skriver SIE typ 1–4 enligt 4C" utan logotyp. Validera mot sietest.sie.se. Undersök om ett projekt eller en förening kan bli medlem [EJ VERIFIERAT]. |
| **Skatteverkets API:er** (avtal, hemliga nycklar, registrerat programvaruföretag, EU/EES) | (1) Fil i stället: moms-XML i e-tjänsten och SRU via Filöverföring. (2) Valfri broker som en juridisk person driver med avtal. (3) Fråga Skatteverket (icc@skatteverket.se) om användare kan registrera egna nycklar. |
| **eSKD-DTD otillgänglig** (404) | Begär aktuell specifikation från Skatteverket. Testa mot e-tjänsten. |
| **Bolagsverkets API** (bara juridisk person, granskning i månader, IP-vitlista, kö till 2027) | Pappersinlämning (som fortfarande verkar vara tillåten), eller export av iXBRL till en godkänd tredjepartstjänst, eller en egen juridisk person som går igenom anslutningen. |
| **Okända autentiseringsdetaljer hos Bolagsverket** (klientcertifikat?) | Läs Anslutningsanvisningar 1.8 och Tekniskt ramverk 1.2 manuellt i webbläsare (CAPTCHA). |
| **Rättslig vägledning 3205** oläst | Öppna manuellt. |

### Det jag inte kunde verifiera
- Innehållet på rättslig vägledning 3205.
- eSKDUpload 6.0-DTD:ns innehåll.
- Hur Bolagsverkets API autentiserar programvaran.
- Om digital inlämning till Bolagsverket har blivit obligatorisk genom beslutad lag.
- Hur spritt SIE 5 är.
- Omfattningen av API:t för Inkomstdeklaration 1 (NE).
- Hur ofta BAS gör v2-revisioner.
- Om nya K2-taxonomier för 2026 är på väg.
- Om BAS-listor skyddas av upphovsrätt eller katalogskydd.
