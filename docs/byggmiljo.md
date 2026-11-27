# Byggmiljö

GnuCash SE byggs på grenen `future` (blivande GnuCash 6.0). `future` kräver nyare bibliotek än de flesta
distributioner har i dag, så bygget körs i en podman-container med Ubuntu 26.04, samma distribution som
upstreams CI för `future`.

Sedan 2026-11-27 byggs bara motorn, bindningarna och forkens egen kod. GnuCash GTK-program (`gnucash/`) ligger
kvar i trädet men byggs inte: `se/cmake/SeDefaults.cmake` ger `WITH_GNUCASH` standardvärdet OFF.

## Containern

Filer i `util/se/`:

| Fil | Innehåll |
|---|---|
| `containers/future-ubuntu2604/Containerfile` | image med samma paket som upstreams CI-jobb "Ubuntu-26.04 CI Tests" |
| `future-env` | kör ett kommando i containern med repots rot monterad på samma sökväg som på värden |

`future-env` kör utan nätverk, med 4 processorer och `nice -n 19`. Arbetskatalogen är `.build/future`
(annan katalog under `.build/` med `FUTURE_BUILD`). Rotlös podman: root i containern motsvarar den egna
användaren, så byggfilerna ägs av den. Miljövariabler (`GNUCASH_SRC`, `FUTURE_IMAGE`, `FUTURE_BUILD`,
`FUTURE_NET`, `FUTURE_CPUS`, `TZ`) beskrivs i skriptets huvud.

Versioner i imagen (2026-11-24) mot kraven i `README.dependencies` på `future`:

| | Imagen | Krav |
|---|---|---|
| cmake | 4.2.3 | 4.0 |
| glib | 2.88.0 | 2.88.0 |
| gtk3 | 3.24.52 | 3.24.52 |
| boost | 1.90 | 1.90 |
| ICU | 78.2 | 78.0 |
| swig | 4.4.0 | 4.4.0 |
| python | 3.14.4 | 3.14 |
| guile | 3.0.11 | 3.0 |
| gcc | 15.2.0 | C++23 |

Flera ligger exakt på minimikravet. Höjer upstream kraven behöver imagen byggas om, eller byta bas.
Imagen innehåller fortfarande GTK och WebKit, eftersom den också används för det fullständiga bygget nedan.

## Kommandon

Byggkatalogen `.build/` står i `.git/info/exclude`.

```sh
podman build -t localhost/gnucash-future:ubuntu26.04 util/se/containers/future-ubuntu2604   # en gång
mkdir -p .build/future && cd .build/future
../../util/se/future-env cmake -G Ninja -DWITH_PYTHON=ON \
    -DCMAKE_INSTALL_PREFIX="$PWD/../inst-future" ../..                    # första gången
../../util/se/future-env ninja -j4
../../util/se/future-env sh -c 'CTEST_PARALLEL_LEVEL=4 ninja -j4 check'
../../util/se/future-env ctest -R <namn> --output-on-failure
../../util/se/future-env                                                   # interaktivt skal
```

Utvecklingsservern delas med produktionssiter: högst 4 jobb och ett bygge i taget.

### Hela GnuCash med GTK (bara vid synk)

På en synkgren (`chore/sync-upstream-…`) byggs och testas dessutom hela GnuCash, som kontroll av att det som
togs in från upstream fortfarande bygger. CI gör det i jobbet "Full GnuCash build" (`se-ci.yml`). Lokalt, i en
egen byggkatalog:

```sh
mkdir -p .build/gtk
FUTURE_BUILD=gtk util/se/future-env cmake -G Ninja -DWITH_GNUCASH=ON -DWITH_PYTHON=ON ../..
FUTURE_BUILD=gtk util/se/future-env ninja -j4
FUTURE_BUILD=gtk util/se/future-env sh -c 'CTEST_PARALLEL_LEVEL=4 ninja -j4 check'
```

## Baslinje

### Utan GTK (gäller nu)

`chore/drop-gtk-build` ovanpå `future` `50fe4e5fa7` (upstream `515c96a83d`, 2026-11-25). Uppmätt 2026-11-27 på
utvecklingsservern, bygge från tom katalog:

| Steg | Resultat | Tid |
|---|---|---|
| `cmake` | ok, `WITH_GNUCASH=OFF` utan flagga | ca 8 s |
| `ninja -j4` | 492 steg, ok | 2 min 59 s |
| `ninja -j4 check` (tidszon America/Los_Angeles, som upstreams CI) | **91 av 91 gröna** | 2 min 25 s, varav testerna 6,0 s |

- De 91 testerna: motorn 38, Guile-bindningarna 16, XML-backenden 13, core-utils 6, app-utils 6, gnc-module 5,
  SQL-backenden 2, Python-bindningarna 2, DBI 1, tax 1 och `test-gnc-se` 1.
- I testerna: 373 gtest-testfall, 135 Scheme-kontroller (`[pass]`) och 281 GLib-testfall (`ok`).
- Kompilatorvarningar: en "non-literal format string" från Guile-kompileringen och länkvarningen om `mktemp`
  (`gnc-xml-backend.cpp`). Inga fel; C/C++ byggs med `-Werror`.
- De 50 tester som försvann med GTK-programmet: rapporterna 31, import-export 13, GTK-lagret 6. Rapporttesterna
  körde hela motorn och fångade därför en del som motorns egna tester inte gör; det fullständiga bygget på
  synkgrenarna (ovan) kör dem fortfarande.
- **Inte prövat lokalt:** bygget utan GTK på macOS och Windows. Det visar CI först när grenen är pushad.

### Med GTK (till och med 2026-11-27)

`future` på `515c96a83d` (orörd upstream) plus forkens `docs/` och `util/se/`: `ninja -j4` 1121 steg på
5 min 23 s, `ninja -j4 check` 140 av 140 gröna på 3 min 22 s. Med forkens kod 141 av 141 (`50fe4e5fa7`).

## Att känna till

- Raderna `not ok … FATAL-WARNING` och `FATAL-CRITICAL` i `Testing/Temporary/LastTest.log` (36 st, i
  `test-engine` och `test-qof`) är förväntade: testerna startar delprocesser som ska avbrytas, och
  huvudprocessen rapporterar sedan `ok`.
- Det fullständiga bygget med GTK kör AqBanking-testerna, som lämnar en transaktionslogg i **källträdet**
  (`gnucash/import-export/aqb/test/file-book-hbcislot.gnucash.<tid>.log`). Ta bort den efter testkörningen;
  committa den inte. Bygget utan GTK lämnar inget i källträdet utom `po/POTFILES.in`, som cmake skriver om.
- Testerna skapar tomma kataloger under `~/.local/share/gnucash/` och `~/.config/gnucash/`.
- `ninja distcheck` går inte utan GTK-programmet (`common/cmake_modules/MakeDist.cmake`), och är därför utbytt
  mot `ninja check` i CI. Källtarboll görs inte från forken i dag.
- ASAN-bygge körs i CI, inte lokalt.
