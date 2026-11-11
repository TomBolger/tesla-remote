// Movement estimate from two GPS fixes
var store = {};
global.localStorage = { getItem: function (k) { return k in store ? store[k] : null; }, setItem: function (k, v) { store[k] = String(v); }, removeItem: function (k) { delete store[k]; } };
var T = require('../src/pkjs/tesla.js');
T.setJSON('tr_last', { at: 0, d: { drive_state: { latitude: 47.6588, longitude: -117.426, gps_as_of: 1000 } } });
var ds = { latitude: 47.6688, longitude: -117.426, gps_as_of: 1060 };
T.estimateMotion(ds);
console.log((ds.moving && ds.est_speed > 38 && ds.est_speed < 45 ? 'PASS' : 'FAIL') + ' 1.1 km in 60 s -> ' + ds.est_speed + ' mph');
var still = { latitude: 47.65881, longitude: -117.426, gps_as_of: 1060 };
T.estimateMotion(still);
console.log((!still.moving ? 'PASS' : 'FAIL') + ' parked car reads as not moving');
