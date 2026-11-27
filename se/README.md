<!--
SPDX-License-Identifier: AGPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Isak Adamssvärd
-->

# se/ – server, API och webbklient

Här ligger allt som GnuCash SE lägger ovanpå bokföringsmotorn. Katalogen finns bara i forken.

| Katalog | Innehåll | Status |
|---|---|---|
| `cmake/` | forkens standardvärden för bygget (`SeDefaults.cmake`) | finns |
| `core/` | kommandolagret `gcse-core`: ett företags bokföring som kommandon och frågor, det enda som anropar motorn ([design 07](../docs/design/07-kommandolager.md)) | finns |
| `server/` | bokvärd och gateway (HTTP, inloggning, behörighet) | planerat |
| `cli/` | kommandoradsklienten `gcse` | planerat |
| `web/` | webbklienten (SvelteKit) | planerat |
| `api/` | `openapi.yaml`, källan för API:t | planerat |
| `test/` | tester för hela kedjan | planerat |
| `tools/` | utvecklingsverktyg: `gcse-bench` genererar en bok och mäter motorn med den | finns |

Planen står i [`../docs/fardplan.md`](../docs/fardplan.md).

## Licens

Allt i den här katalogen är fri programvara under **GNU Affero General Public License, version 3 eller
senare** (`AGPL-3.0-or-later`). Licenstexten finns i [`LICENSES/AGPL-3.0-or-later.txt`](LICENSES/AGPL-3.0-or-later.txt).
Varje fil bär ett SPDX-huvud:

```
SPDX-License-Identifier: AGPL-3.0-or-later
```

Resten av trädet, alltså GnuCash och SE-modulen i `libgnucash/se`, har kvar GnuCash licens (GPL version 2 eller
senare, se `LICENSE` i repots rot). GPL version 3 tillåter att ett sådant program kombineras med kod under AGPL
version 3. Ett program som länkar både motorn och koden här distribueras därför under AGPL version 3 eller
senare. Den som låter andra använda en ändrad version över nätverk ska erbjuda dem källkoden (avsnitt 13 i
licensen).

Beroenden får bara ha licenser som går ihop med detta, och ska vara fri programvara.
