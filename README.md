# Tesla Remote for Pebble

A free, open-source Tesla remote for Pebble watches, styled like Tesla's dark-mode screen.
All the work happens on your phone: the Pebble phone app signs car commands itself, using the same method as
Tesla's `tesla-http-proxy`. The only extra piece is a tiny, stateless relay (`relay/worker.js`, free on Cloudflare),
needed because the Pebble app blocks direct calls to Tesla's car server. It stores nothing; you can run your own.

**Version 1.0.** Works with Model 3, Model S, Model X (6 or 7 seats), Model Y (5 or 7 seats) and Cybertruck.

**Watch:** lock/unlock, frunk, trunk, climate and temperature (optionally split driver/passenger), seat heat and cooling
for every seat including the third row, heated wheel, defrost, battery, range, charging, distance and direction to the car,
and whether it's parked or being driven. **More** menu: charging (start/stop, limit, amps, port, nearby chargers),
climate extras (keep climate, dog/camp mode, cabin overheat, auto seats, bioweapon defense), security (Sentry, guest
mode, auto-secure), controls (lights, horn, windows, lids) and car info.
**Cybertruck:** open the tailgate (it closes by hand) and open, close or stop the powered tonneau; the watch shows
whether the tonneau is open.
The watch always shows what the car reports. A change you make shows everywhere as soon as the car accepts it, then
the app checks the car again 3, 13 and 43 seconds later. While the car is being driven it updates once a minute.
**Watches:** Pebble Time / Time Steel (basalt), Time Round (chalk), Pebble 2 (diorite), Pebble 2 Duo (flint), Time 2 (emery), Round 2 (gabbro).

## Setting it up (about 10 minutes, once)
Install the app, then open its settings in the Pebble phone app. The settings page walks you through four steps.
Tesla's own pages don't open inside the Pebble settings screen, so you copy links into Chrome or Safari and copy a
couple of things back. The page remembers your progress while you switch apps.

1. **Put your car key online.** Copy your key, paste it into [Fleet Key](https://fleetkey.net) (free), and paste back the address it gives you.
2. **Make your free Tesla app** at developer.tesla.com. The page shows exactly what to type in each box, with Copy buttons.
3. **Sign in with Tesla** in your browser, copy the code it shows, paste it back, and tap **Finish**.
4. **Add the key to your car:** near your car, open the link the page gives you and approve the key in the Tesla app.

Tesla gives every account a monthly API allowance that covers normal personal use, and this project charges nothing.

## Watch buttons
- Home: Up/Down choose a control, Select runs it, hold Select to wake the car and refresh.
- Climate: Up/Down change the temperature, Select turns climate on or off, hold Select for defrost and seat heat.
- Unlock, frunk, trunk and honk ask you to confirm first.

## For maintainers
- `src/c/main.c`: watch app. `src/pkjs/`: phone code (`tesla.js` = Fleet API + command signing, `crypto.js` = P-256/SHA/HMAC/AES, no dependencies).
- `docs/index.html`: the setup page. It must be on HTTPS because Tesla redirects back to it after sign-in. Turn on GitHub Pages for the `docs/` folder and make sure `CONFIG_URL` in `src/pkjs/index.js` matches.
- `relay/worker.js`: the relay. Deploy it as a Cloudflare Worker and put its address in `RELAY_DEFAULT` in `docs/index.html`.
- Art: source images live in `art/`. `python3 tools/make_assets.py` rebuilds the watch images and `src/c/art_geo.h`. A Cybertruck sheet with a third row (1600 x 288, row 3 = tonneau opening) is picked up automatically.
- Build: `pebble build`. Tests: `node test/protocol_test.js` and `node test/menu_test.js` (needs Python packages `cryptography` and `protobuf`, plus Python classes compiled from Tesla's `.proto` files in `test/pb/`).

## Notes
- The car key is stored inside the Pebble phone app on your phone. Remove it from the car (Controls > Locks) if you lose the phone.
- Fleet Key is a free third-party service. If it goes away, host the same public key anywhere else and pair again.
- Not affiliated with or endorsed by Tesla.

MIT licensed.
