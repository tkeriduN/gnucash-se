# Rätta en bokförd transaktion. Kräver en låst bok: kör steps-lock.sh först och
# kopiera .build/gui-test/work.gnucash till locked.gnucash, kör sedan med
# BOOK=locked.gnucash. Engelska menyer.
xdotool search --name "Tip Of The Day" windowactivate; wait 1
key Escape
xdotool search --name "Accounts - GnuCash" windowactivate; wait 1
xdotool mousemove 80 457 click --repeat 2 --delay 120 1; wait 4
xdotool mousemove 250 458 click 1; wait 1
xdotool mousemove 174 307 click 1; wait 1
shot 30-transaction-menu
xdotool mousemove 245 584 click 1; wait 2
shot 31-correct-dialog-empty
type "IA"
key Tab
type "Fel belopp"
wait 1
shot 32-correct-dialog-filled
key alt+e; wait 3
shot 33-after-correction
key ctrl+s; wait 4
