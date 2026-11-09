var c = require('../src/pkjs/crypto.js');
var crypto = require('crypto');
function eq(name, a, b){ console.log((a===b?'PASS ':'FAIL ')+name+(a===b?'':'\n  got  '+a+'\n  want '+b)); if(a!==b) process.exitCode=1; }
// hashes vs node
['', 'abc', 'x'.repeat(200)].forEach(function(s){
  var b = c.strToBytes(s);
  eq('sha256 len'+s.length, c.bytesToHex(c.sha256(b)), crypto.createHash('sha256').update(s).digest('hex'));
  eq('sha1 len'+s.length, c.bytesToHex(c.sha1(b)), crypto.createHash('sha1').update(s).digest('hex'));
  eq('hmac len'+s.length, c.bytesToHex(c.hmacSha256(c.strToBytes('key'), b)), crypto.createHmac('sha256','key').update(s).digest('hex'));
});
// Tesla vectors
var clientPriv = '2538CDC29A97C19C1E99A637D6CF4F8C970C118B56EDE1E6323E6D162C4B30DB';
eq('client pub', c.bytesToHex(c.publicFromPrivate(clientPriv)), '04b2b6bc68c2da0665ce656815594996c62394edd8bea905fe781a754fe6a845a714330902f225e9269d466e05b349981fda9d85cc23c6fb444aa73b629105dc6e');
var V = c.hexToBytes('04c7a1f47138486aa4729971494878d33b1a24e39571f748a6e16c5955b3d877d3a6aaa0e955166474af5d32c410f439a2234137ad1bb085fd4e8813c958f11d97');
var t0=Date.now(); var K = c.deriveSessionKey(clientPriv, V); var dt=Date.now()-t0;
eq('K', c.bytesToHex(K), '1b2fce19967b79db696f909cff89ea9a'); console.log('  ecdh ms', dt);
var sik = c.hmacSha256(K, c.strToBytes('session info'));
eq('session info key', c.bytesToHex(sik), 'fceb679ee7bca756fcd441bf238bf2f338629b41d9eb9c67be1b32c9672ce300');
var meta = c.hexToBytes('000106021135594a333031323334353637383941424306101588d5a30eabc6f8fc9a951b11f6fd11ff');
var info = c.hexToBytes('0806124104c7a1f47138486aa4729971494878d33b1a24e39571f748a6e16c5955b3d877d3a6aaa0e955166474af5d32c410f439a2234137ad1bb085fd4e8813c958f11d971a104c463f9cc0d3d26906e982ed224adde6255a0a0000');
eq('session info tag', c.bytesToHex(c.hmacSha256(sik, meta.concat(info))), '996c1fe38331be138f8039c194b14db2198846ed7d8251e6749284d7b32ea002');
eq('aes-gcm ctr decrypt', c.bytesToHex(c.aesGcmDecryptNoAuth(K, c.hexToBytes('dbf79447fa156674dae1caed'), c.hexToBytes('38038e8c0f2e'))), '120452020801');
// longer AES vs node
var key=crypto.randomBytes(16), iv=crypto.randomBytes(12), pt=crypto.randomBytes(77);
var ci=crypto.createCipheriv('aes-128-gcm',key,iv); var ct=Buffer.concat([ci.update(pt),ci.final()]);
eq('aes-gcm 77 bytes', c.bytesToHex(c.aesGcmDecryptNoAuth(Array.from(key),Array.from(iv),Array.from(ct))), pt.toString('hex'));
// random ECDH vs node
for (var i=0;i<3;i++){ var a=crypto.createECDH('prime256v1'); a.generateKeys(); var b=crypto.createECDH('prime256v1'); b.generateKeys();
 var s=a.computeSecret(b.getPublicKey()); eq('ecdh random '+i, c.bytesToHex(c.deriveSessionKey(a.getPrivateKey('hex'), Array.from(b.getPublicKey()))), crypto.createHash('sha1').update(s).digest('hex').slice(0,32)); }
eq('b64 roundtrip', c.bytesToHex(c.b64decode(c.b64encode(c.hexToBytes('00ff10abcd')))), '00ff10abcd');
