# Byggmiljö

GnuCash SE byggs på grenen `future` (blivande GnuCash 6.0). `future` kräver nyare bibliotek än de flesta
distributioner har i dag, så bygget körs i en podman-container med Ubuntu 26.04, samma distribution som
upstreams CI för `future`.

## Containern

Filer i `util/se/`:

| Fil | Innehåll |
|---|---|
| `containers/future-ubuntu2604/Containerfile` | image med samma paket som upstreams CI-jobb "Ubuntu-26.04 CI Tests" |
| `future-env` | kör ett kommando i containern med repots rot monterad på samma sökväg som på värden |

`future-env` kör utan nätverk, med 4 processorer och `nice -n 19`. Arbetskatalogen är `.build/future`.
Rotlös podman: root i containern motsvarar den egna användaren, så byggfilerna ägs av den. Miljövariabler
(`GNUCASH_SRC`, `FUTURE_IMAGE`, `FUTURE_NET`, `FUTURE_CPUS`, `TZ`) beskrivs i skriptets huvud.

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

## Baslinje (orörd upstream)

`future` på `515c96a83d` (upstream 2026-11-25), plus forkens `docs/` och `util/se/` som inte påverkar
bygget. Uppmätt 2026-11-26 på utvecklingsservern:

| Steg | Resultat | Tid |
|---|---|---|
| `cmake` | ok | ca 10 s |
| `ninja -j4` | 1121 steg, ok | 5 min 23 s |
| `ninja -j4 check` (tidszon America/Los_Angeles, som upstreams CI) | **140 av 140 gröna** | 3 min 22 s, varav testerna 7,4 s |

- I testerna: 398 gtest-testfall, 1 298 Scheme-kontroller (`[pass]`) och 313 GLib-testfall (`ok`).
- Kompilatorvarningar: tre "non-literal format string" från Guile-kompileringen och en länkvarning om
  `mktemp` (`gnc-xml-backend.cpp`). Inga fel; C/C++ byggs med `-Werror`.
- Samma resultat som följeslagarrepots baslinje på `5c17baf487` (1121 steg, 140/140).

### Att känna till

- Raderna `not ok … FATAL-WARNING` och `FATAL-CRITICAL` i `Testing/Temporary/LastTest.log` (36 st, i
  `test-engine` och `test-qof`) är förväntade: testerna startar delprocesser som ska avbrytas, och
  huvudprocessen rapporterar sedan `ok`.
- AqBanking-testerna lämnar en transaktionslogg i **källträdet**
  (`gnucash/import-export/aqb/test/file-book-hbcislot.gnucash.<tid>.log`). Ta bort den efter testkörningen;
  committa den inte.
- Testerna skapar tomma kataloger under `~/.local/share/gnucash/` och `~/.config/gnucash/`.
- `ninja install`, `ninja distcheck` och ASAN-bygge är inte körda för forken.
- GUI går inte att prova på servern (ingen skärm). `xvfb-run` finns för rökprov.
