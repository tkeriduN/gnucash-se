# Bokför till och med förra månadens slut (med signatur), kör kontrollen,
# visa behandlingshistoriken, öppna inställningarna och spara.
# Engelska menyer (Alt+A = Actions).
xdotool search --name "Tip Of The Day" windowactivate; wait 1
key Escape
xdotool search --name "Accounts - GnuCash" windowactivate; wait 1
key alt+a; key Right
shot 01-menu
key Return; wait 2
shot 02-post-through
key Tab; key Tab
type "IA"
wait 1
shot 03-signed
key alt+p; wait 3
shot 04-posted
key Return; wait 1
key alt+a; key Right; key Down; key Return; wait 2
shot 05-check
key Return; wait 1
key alt+a; key Right; key Down; key Down; key Return; wait 3
shot 06-history
key Escape; wait 1
key alt+a; key Right; key Down; key Down; key Down; key Return; wait 2
shot 07-settings
key Escape; wait 1
key ctrl+s; wait 4
