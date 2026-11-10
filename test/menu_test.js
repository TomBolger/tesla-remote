// index.js + tesla.js + relay-less fake car. Fake Pebble records messages; checks menu pages and follow-up refreshes.
var cp = require('child_process'), path = require('path'), fs = require('fs');
var store = {};
global.localStorage = { getItem: function (k) { return k in store ? store[k] : null; }, setItem: function (k, v) { store[k] = String(v); }, removeItem: function (k) { delete store[k]; } };
global.navigator = {};
var calls = [];
global.XMLHttpRequest = function () { this.h = {}; };
XMLHttpRequest.prototype.open = function (m, u) { this.m = m; this.u = u; };
XMLHttpRequest.prototype.setRequestHeader = function () {};
XMLHttpRequest.prototype.send = function (body) {
  var self = this; calls.push((this.u.split('.com')[1] || this.u).split('?')[0]);
  var res = JSON.parse(cp.execFileSync('python3', ['-I', path.join(__dirname, 'fake_car.py')], { input: JSON.stringify({ method: this.m, url: this.u, body: body }) }).toString());
  setTimeout(function () { self.readyState = 4; self.status = res.status; self.responseText = res.text; self.onreadystatechange(); }, 1);
};
var realST = setTimeout, scheduled = [];
global.setTimeout = function (f, ms) { if (ms >= 3000) scheduled.push(ms); return realST(f, Math.min(ms, 30)); };
global.clearTimeout = function () {};
try { fs.unlinkSync(path.join(__dirname, 'state.json')); } catch (e) {}
var handlers = {}, sent = [];
global.Pebble = { addEventListener: function (e, f) { handlers[e] = f; }, sendAppMessage: function (m, ok) { sent.push(m); realST(ok, 1); }, openURL: function () {} };
var T = require('../src/pkjs/tesla.js'), C = require('../src/pkjs/crypto.js');
var priv = '2538cdc29a97c19c1e99a637d6cf4f8c970c118b56ede1e6323e6d162c4b30db';
T.saveCfg({ clientId: 'cid', clientSecret: 's', privHex: priv, pubHex: C.bytesToHex(C.publicFromPrivate(priv)), domain: 'a.fleetkey.net', region: 'na', vin: '5YJ30123456789ABC' });
localStorage.setItem('tr_tokens', JSON.stringify({ access: 'AT', refresh: 'RT', expiresAt: Date.now() + 1e7 }));
require('../src/pkjs/index.js');
function wait(ms, f) { realST(f, ms); }
handlers.ready();
wait(1500, function () {
  // wake the fake car so data reads work
  handlers.appmessage({ payload: { Type: 4, Value: 1 } });
  wait(3000, function () {
    var st = sent.filter(function (m) { return m.Type === 1; }).pop();
    console.log('STATUS after wake:', JSON.stringify({ CarState: st.CarState, Charging: st.Charging, HasRearC: st.HasRearC, Climate: st.Climate, RearDefrost: st.RearDefrost, Model: st.Model }));
    var pages = ['root', 'charging', 'limit', 'amps', 'chargers', 'climate', 'keeper', 'cop', 'security', 'controls', 'info', 'power'];
    (function next(i) {
      if (i >= pages.length) return actions();
      sent = [];
      handlers.appmessage({ payload: { Type: 7, Text: pages[i] } });
      wait(pages[i] === 'chargers' ? 900 : 200, function () {
        var m = sent.filter(function (x) { return x.Type === 8; })[0];
        var bytes = m ? Buffer.byteLength(m.MenuRows) : 0;
        console.log('\n== ' + pages[i] + ' (' + (m && m.MenuTitle) + ', ' + bytes + ' bytes)');
        if (m) m.MenuRows.split('\n').forEach(function (r) { var f = r.split('\t'); console.log('  ' + f[0] + (f[1] ? '  | ' + f[1] : '') + (f[2] ? '  -> ' + f[2] : '')); });
        next(i + 1);
      });
    })(0);
  });
});
function actions() {
  sent = []; calls = []; scheduled = [];
  handlers.appmessage({ payload: { Type: 7, Text: 'root|charging|limit' } });
  handlers.appmessage({ payload: { Type: 9, Text: 'c:charge_limit:90' } });
  wait(2500, function () {
    var res = sent.filter(function (m) { return m.Type === 3; });
    console.log('\nACTION charge_limit:90 ->', JSON.stringify(res));
    var afterOk = sent.slice(sent.findIndex(function (m) { return m.Type === 3; }));
    var pagesNow = afterOk.filter(function (m) { return m.Type === 8; }).slice(0, 3);
    var ch = pagesNow.filter(function (m) { return m.MenuPage === 'charging'; })[0];
    console.log((pagesNow.length === 3 && ch && /Charge limit\t90%/.test(ch.MenuRows) ? 'PASS' : 'FAIL') +
      ' every open page (' + pagesNow.map(function (m) { return m.MenuPage; }).join(', ') + ') updated at once');
    var lim = sent.filter(function (m) { return m.Type === 8 && m.MenuPage === 'limit'; })[0];
    console.log((lim && lim.MenuSel === 4 && /^100%/.test(lim.MenuRows) ? 'PASS' : 'FAIL') + ' limit list starts at 100% and opens on 80%');
    var firstStatus = sent.filter(function (m) { return m.Type === 1; })[0];
    console.log((firstStatus && firstStatus.ChargeLimit === 90 ? 'PASS' : 'FAIL') + ' watch shows new limit as soon as the car accepts it');
    console.log('follow-up refreshes scheduled at (ms):', JSON.stringify(scheduled));
    console.log('car-server calls:', JSON.stringify(calls.filter(function (c) { return c; })));
    var menus = sent.filter(function (m) { return m.Type === 8; }).map(function (m) { return m.MenuPage; });
    console.log('menu pages re-sent after refreshes:', JSON.stringify(menus));
    var st = JSON.parse(fs.readFileSync(path.join(__dirname, 'state.json')));
    st.log.filter(function (l) { return l.cmd; }).slice(-1).forEach(function (l) { console.log('car received:', l.cmd.slice(0, 120)); });
    ['trunk_close', 'sentry:1', 'keeper:2', 'cop:2', 'cop_temp:3', 'auto_seat:21', 'port_close', 'charge_stop', 'charge_amps:16', 'auto_secure', 'guest:1', 'low_power:1', 'accessory:1', 'bioweapon:1'].reduce(function (p, a) {
      return function () { sent = []; handlers.appmessage({ payload: { Type: 9, Text: 'c:' + a } }); wait(1200, function () {
        var r = sent.filter(function (m) { return m.Type === 3; })[0];
        var st2 = JSON.parse(fs.readFileSync(path.join(__dirname, 'state.json'))), l = st2.log.filter(function (x) { return x.cmd && x.cmd.indexOf('getVehicleData') < 0; }).pop();
        console.log((r && r.Value === 1 ? 'PASS ' : 'FAIL ') + a + '  -> car: ' + (l ? l.cmd.replace(/\s+/g, ' ').slice(0, 110) : '?'));
        p(); }); };
    }, function () { process.exit(0); })();
  });
}
