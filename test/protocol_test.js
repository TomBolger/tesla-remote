// Runs the real phone-side Tesla code in Node against the Python fake car.
var cp = require('child_process'), fs = require('fs'), path = require('path');
var store = {};
global.localStorage = { getItem: function (k) { return k in store ? store[k] : null; }, setItem: function (k, v) { store[k] = String(v); }, removeItem: function (k) { delete store[k]; } };
global.XMLHttpRequest = function () { this.h = {}; };
XMLHttpRequest.prototype.open = function (m, u) { this.m = m; this.u = u; };
XMLHttpRequest.prototype.setRequestHeader = function (k, v) { this.h[k] = v; };
XMLHttpRequest.prototype.send = function (body) {
  var self = this;
  var res = JSON.parse(cp.execFileSync('python3', ['-I', path.join(__dirname, 'fake_car.py')], { input: JSON.stringify({ method: this.m, url: this.u, body: body }) }).toString());
  setTimeout(function () { self.readyState = 4; self.status = res.status; self.responseText = res.text; self.onreadystatechange(); }, 1);
};
// speed up wake polling
var realST = setTimeout; global.setTimeout = function (f, ms) { return realST(f, Math.min(ms, 20)); };

try { fs.unlinkSync(path.join(__dirname, 'state.json')); } catch (e) {}
var T = require('../src/pkjs/tesla.js');
var C = require('../src/pkjs/crypto.js');
var priv = '2538cdc29a97c19c1e99a637d6cf4f8c970c118b56ede1e6323e6d162c4b30db';
T.saveCfg({ clientId: 'cid', clientSecret: 'sec', privHex: priv, pubHex: C.bytesToHex(C.publicFromPrivate(priv)), domain: 'abc.fleetkey.net', region: 'na', redirectUri: 'https://x/cb' });
function prog(t) { console.log('   progress:', t); }
var steps = [
  ['lock'], ['unlock'], ['frunk'], ['trunk'], ['climate_on'], ['set_temp', 21.5], ['seat', 2],
  ['defrost_on'], ['defrost_off'], ['climate_off'], ['flash'],
  ['seat', 0x13], ['seat', 0x41], ['cool', 0x12], ['cool', 0x00], ['wheel_on'], ['wheel_off'],
  ['vent'], ['close_windows'], ['set_temps', [21.5, 19]], ['seat', 0x52], ['seat', 0x61],
  ['tonneau_open'], ['tonneau_close'], ['tonneau_stop'], ['honk', null, true]
];
T.completeSignIn('CODE', prog, function (e) {
  console.log('signin:', e ? e.text : 'ok', 'vin=', T.cfg().vin);
  T.vehicleData(function (de, d) {
    console.log('vehicle_data while asleep:', de ? de.code : 'ok');
    var i = 0;
    (function next() {
      if (i >= steps.length) return finish();
      var s = steps[i++];
      T.command(s[0], s[1], function () {}, function (ce) {
        var ok = !ce;
        console.log((ok !== !!s[2] ? 'PASS ' : 'FAIL ') + s[0] + (ce ? ' -> ' + ce.text : ' -> ok'));
        next();
      });
    })();
  });
});
function finish() {
  process.env.FAKE_GEAR = 'D';
  T.vehicleData(function (e, d) {
    var ds = (d || {}).drive_state || {};
    console.log((ds.shift_state === 'D' && ds.speed === 37 ? 'PASS' : 'FAIL') + ' gear from signed query -> ' + ds.shift_state + ' ' + ds.speed);
    process.env.FAKE_CAR = 'cybertruck';
    T.vehicleData(function (e2, d2) {
      var vs = (d2 || {}).vehicle_state || {};
      console.log((vs.tonneau_state === 1 ? 'PASS' : 'FAIL') + ' tonneau from signed query -> ' + vs.tonneau_state);
      delete process.env.FAKE_CAR;
      finish2();
    });
  });
}
function finish2() {
  // Simulate the car rebooting (new epoch): the code should re-handshake and recover.
  var st = JSON.parse(fs.readFileSync(path.join(__dirname, 'state.json')));
  st.epoch = 'ffeeddccbbaa99887766554433221100'; fs.writeFileSync(path.join(__dirname, 'state.json'), JSON.stringify(st));
  T.command('lock', null, function () {}, function (ce) {
    console.log((ce ? 'FAIL' : 'PASS') + ' recover after car reboot' + (ce ? ' -> ' + ce.text : ''));
    st = JSON.parse(fs.readFileSync(path.join(__dirname, 'state.json')));
    st.paired = '04' + '00'.repeat(64); st.epoch = '00112233445566778899aabbccddeeff';
    fs.writeFileSync(path.join(__dirname, 'state.json'), JSON.stringify(st));
    localStorage.removeItem('tr_sess_' + T.cfg().vin + '_2');
    T.command('lock', null, function () {}, function (pe) {
      console.log((pe && /Pair/.test(pe.text) ? 'PASS' : 'FAIL') + ' unpaired key detected -> ' + (pe ? pe.text : 'ok'));
      st = JSON.parse(fs.readFileSync(path.join(__dirname, 'state.json')));
      console.log('\nCar-side log:');
      st.log.forEach(function (l) { console.log('  ' + JSON.stringify(l)); });
    });
  });
}
