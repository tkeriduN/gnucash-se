# Bokför till och med förra månadens slut, kör kontrollen, öppna
# inställningarna och spara. Engelska menyer (Alt+A = Actions).
xdotool search --name "Tip Of The Day" windowactivate; wait 1
key Escape
xdotool search --name "Accounts - GnuCash" windowactivate; wait 1
key alt+a; key Right
shot 01-menu
key Return; wait 2
shot 02-post-through
key alt+p; wait 3
shot 03-posted
key Return; wait 1
key alt+a; key Right; key Down; key Return; wait 2
shot 04-check
key Return; wait 1
key alt+a; key Right; key Down; key Down; key Return; wait 2
shot 05-settings
key Escape; wait 1
key ctrl+s; wait 4
