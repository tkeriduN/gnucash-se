# Avsluta ett räkenskapsår och visa räkenskapsåren i inställningarna.
# Kräver en bok med transaktioner i ett år som är slut:
#   python3 make-book.py .build/gui-test/bok2025.gnucash 2025   (i GUI-imagen)
# och kör sedan med BOOK=bok2025.gnucash. Engelska menyer.
xdotool search --name "Tip Of The Day" windowactivate; wait 1
key Escape
xdotool search --name "Accounts - GnuCash" windowactivate; wait 1
key alt+a; key Right; key Down; key Return; wait 2
shot 01-close-year
type "IA"
wait 1
key alt+y; wait 3
shot 02-year-closed
key Return; wait 1
key alt+a; key Right; key Down; key Down; key Down; key Down; key Return; wait 2
shot 03-settings-years
key alt+d; wait 2
shot 04-define-year
key Escape; wait 1
key Escape; wait 1
key ctrl+s; wait 4
