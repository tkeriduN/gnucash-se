#!/bin/bash
# Provkör GnuCash SE:s gränssnitt på en server utan skärm.
#
# Körs i GUI-imagen (util/se/containers/future-gui), efter `ninja install`:
#
#   FUTURE_IMAGE=localhost/gnucash-future-gui:ubuntu26.04 \
#       util/se/future-env "$PWD/util/se/gui-test/drive.sh" steg.sh
#
# Skriptet startar en virtuell skärm och en fönsterhanterare, skapar en liten
# testbok (make-book.py) om ingen finns, startar GnuCash SE med en kopia av
# boken och kör sedan stegen i filen som ges som argument. Stegfilen är
# bash och har funktionerna
#   shot NAMN       skärmbild till .build/gui-test/NAMN.png
#   key TANGENT...  tangenttryckningar (xdotool), t.ex. key alt+a
#   type TEXT       skriv text
#   wait SEKUNDER
# Exempel på steg finns i steps-lock.sh.
#
# Miljövariabler:
#   BOOK          bok att kopiera, relativt .build/gui-test (standard: bok.gnucash)
#   GUI_LANG      LANG i programmet (standard: en_US.UTF-8)
#   GUI_LANGUAGE  LANGUAGE i programmet (standard: en; sv ger svenska)
#   STARTUP       sekunder att vänta på att programmet startar (standard: 14)
#
# `xvfb-run` hänger sig i containern; därför startas Xvfb direkt här.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
I=$root/.build/inst-future
D=$root/.build/gui-test
mkdir -p "$D"
[ -x "$I/bin/gnucash" ] || { echo "drive.sh: kör 'ninja install' först ($I saknas)" >&2; exit 1; }

export GSETTINGS_BACKEND=memory
export GSETTINGS_SCHEMA_DIR=$I/share/glib-2.0/schemas
export XDG_DATA_DIRS=$I/share:/usr/share
export NO_AT_BRIDGE=1

if [ ! -f "$D/bok.gnucash" ]; then
    PYTHONPATH=$(echo "$I"/lib/python3*/site-packages) \
    LD_LIBRARY_PATH=$I/lib:$I/lib/gnucash \
        python3 "$here/make-book.py" "$D/bok.gnucash" || exit 1
fi

rm -f "$D"/work.gnucash* "$D"/*.png
cp "$D/${BOOK:-bok.gnucash}" "$D/work.gnucash"

Xvfb :99 -screen 0 1280x900x24 -nolisten tcp >/dev/null 2>&1 &
xvfb=$!
export DISPLAY=:99
sleep 2
openbox >/dev/null 2>&1 &
sleep 1

LANG=${GUI_LANG:-en_US.UTF-8} LANGUAGE=${GUI_LANGUAGE:-en} \
    "$I/bin/gnucash" --logto "$D/gnucash.log" "$D/work.gnucash" >"$D/stdout.log" 2>&1 &
pid=$!

shot() { import -window root "$D/$1.png"; }
key()  { xdotool key --delay 150 "$@"; sleep "${PAUSE:-1.2}"; }
type() { xdotool type --delay 60 "$1"; sleep 0.5; }
wait() { sleep "$1"; }

sleep "${STARTUP:-14}"
. "$1"

kill $pid 2>/dev/null
sleep 1
kill $xvfb 2>/dev/null
