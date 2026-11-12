// Backup code round trip, using the settings page's own code
var fs = require('fs');
var html = fs.readFileSync(__dirname + '/../docs/index.html', 'utf8');
var code = html.slice(html.indexOf('// ---------- backup & restore'), html.indexOf('// ---------- save back to the watch'));
var els = {}; function el(id) { return els[id] = els[id] || { value: '', textContent: '', style: {} }; }
var state = { current: { backup: { v: 1, cfg: { clientId: 'abc', privHex: 'deadbeef' }, tokens: { refresh: 'r1' } } }, returnTo: '' };
var location = { href: '' }, localStorage = { removeItem: function () {} };
var $ = el;
eval(code);
el('backupPass').value = 'hunter2';
makeBackup();
setTimeout(function () {
  var c = el('backupCode').value;
  el('restoreCode').value = c; el('restorePass').value = 'hunter2';
  restoreBackup();
  setTimeout(function () {
    var out = JSON.parse(decodeURIComponent(location.href.replace('pebblejs://close#', '')));
    console.log((out.restore.cfg.privHex === 'deadbeef' && out.restore.tokens.refresh === 'r1' ? 'PASS' : 'FAIL') + ' backup restores (' + c.length + ' chars)');
    el('restorePass').value = 'wrong'; location.href = '';
    restoreBackup();
    setTimeout(function () { console.log((!location.href && /Wrong password/.test(el('restoreHint').textContent) ? 'PASS' : 'FAIL') + ' wrong password refused'); }, 1500);
  }, 1500);
}, 1500);
