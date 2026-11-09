# Tesla Remote for Pebble

A free, open-source Tesla remote for Pebble watches, styled like Tesla's dark-mode screen.
There is **no server**: the Pebble phone app talks straight to Tesla's official Fleet API and
signs car commands itself, using the same method as Tesla's `tesla-http-proxy`.

**Watch:** lock/unlock, frunk, trunk, climate on/off, temperature, defrost, driver seat heat, flash, honk, battery and range.
**Watches:** Pebble Time / Time Steel (basalt), Time Round (chalk), Pebble 2 (diorite), Pebble 2 Duo (flint), Time 2 (emery), Round 2 (gabbro).

## Using it (each person, once)
Open the app's settings in the Pebble phone app and follow the five steps:
1. The phone makes a car key for you.
2. Paste the public key into [Fleet Key](https://fleetkey.net) (free) and copy the address it gives you.
3. Create a free app at [developer.tesla.com](https://developer.tesla.com/request) with the origin and redirect shown, then paste in the Client ID and secret.
4. Sign in with Tesla, then tap **Save to watch**.
5. Open the `tesla.com/_ak/...` link and approve the key in the Tesla app.

Tesla gives each account a monthly API credit that covers normal personal use. You pay nothing to this project.

## Watch buttons
- Home: Up/Down choose a control, Select runs it, hold Select to wake the car and refresh.
- Climate: Up/Down change the temperature, Select turns climate on or off, hold Select for defrost and seat heat.
- Unlock, frunk, trunk and honk ask you to confirm first.

## For maintainers
- `src/c/main.c`: watch app. `src/pkjs/`: phone code (`tesla.js` = Fleet API + command signing, `crypto.js` = P-256/SHA/HMAC/AES, no dependencies).
- `docs/index.html`: the setup page. It must be on HTTPS because Tesla redirects back to it after sign-in. Turn on GitHub Pages for the `docs/` folder and make sure `CONFIG_URL` in `src/pkjs/index.js` matches.
- Build: `pebble build`. Tests: `node test/protocol_test.js` (needs Python packages `cryptography` and `protobuf`, plus Python classes compiled from Tesla's `.proto` files in `test/pb/`).

## Notes
- The car key is stored inside the Pebble phone app on your phone. Remove it from the car (Controls > Locks) if you lose the phone.
- Fleet Key is a free third-party service. If it goes away, host the same public key anywhere else and pair again.
- Not affiliated with or endorsed by Tesla.

MIT licensed.
