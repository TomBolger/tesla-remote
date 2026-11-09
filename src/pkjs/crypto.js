// Small, dependency-free crypto for the Tesla Vehicle Command Protocol.
// Written in plain ES5 so it runs in every Pebble phone app.
// Bytes are plain arrays of numbers (0-255).
/* eslint-disable no-bitwise */

// ---------- byte helpers ----------
function hexToBytes(hex) {
  hex = String(hex || '').replace(/[^0-9a-fA-F]/g, '');
  var out = [];
  for (var i = 0; i + 1 < hex.length; i += 2) out.push(parseInt(hex.substr(i, 2), 16));
  return out;
}
function bytesToHex(b) {
  var s = '';
  for (var i = 0; i < b.length; i++) s += (b[i] < 16 ? '0' : '') + b[i].toString(16);
  return s;
}
function strToBytes(str) {
  str = unescape(encodeURIComponent(str));
  var out = [];
  for (var i = 0; i < str.length; i++) out.push(str.charCodeAt(i) & 255);
  return out;
}
function bytesToStr(b) {
  var s = '';
  for (var i = 0; i < b.length; i++) s += String.fromCharCode(b[i]);
  try { return decodeURIComponent(escape(s)); } catch (e) { return s; }
}
var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function b64encode(b) {
  var out = '';
  for (var i = 0; i < b.length; i += 3) {
    var n = (b[i] << 16) | ((b[i + 1] || 0) << 8) | (b[i + 2] || 0);
    out += B64.charAt((n >> 18) & 63) + B64.charAt((n >> 12) & 63) +
      (i + 1 < b.length ? B64.charAt((n >> 6) & 63) : '=') +
      (i + 2 < b.length ? B64.charAt(n & 63) : '=');
  }
  return out;
}
function b64decode(s) {
  s = String(s || '').replace(/-/g, '+').replace(/_/g, '/').replace(/[^A-Za-z0-9+/]/g, '');
  var out = [], buf = 0, bits = 0;
  for (var i = 0; i < s.length; i++) {
    buf = (buf << 6) | B64.indexOf(s.charAt(i));
    bits += 6;
    if (bits >= 8) { bits -= 8; out.push((buf >> bits) & 255); }
  }
  return out;
}
function randomBytes(n) {
  var out = [];
  var c = (typeof crypto !== 'undefined' && crypto && crypto.getRandomValues) ? crypto : null;
  if (c && typeof Uint8Array !== 'undefined') {
    try {
      var a = new Uint8Array(n); c.getRandomValues(a);
      for (var i = 0; i < n; i++) out.push(a[i]);
      return out;
    } catch (e) { out = []; }
  }
  // Only used for message IDs and routing addresses, never for keys.
  for (var j = 0; j < n; j++) out.push(Math.floor(Math.random() * 256));
  return out;
}
function equalBytes(a, b) {
  if (a.length !== b.length) return false;
  var d = 0;
  for (var i = 0; i < a.length; i++) d |= a[i] ^ b[i];
  return d === 0;
}

// ---------- SHA-256 ----------
var K256 = [0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2];

function padMessage(msg) {
  var m = msg.slice(0);
  var bitLen = msg.length * 8;
  m.push(0x80);
  while (m.length % 64 !== 56) m.push(0);
  var hi = Math.floor(bitLen / 0x100000000), lo = bitLen >>> 0;
  m.push((hi >>> 24) & 255, (hi >>> 16) & 255, (hi >>> 8) & 255, hi & 255,
    (lo >>> 24) & 255, (lo >>> 16) & 255, (lo >>> 8) & 255, lo & 255);
  return m;
}
function wordsToBytes(words) {
  var out = [];
  for (var i = 0; i < words.length; i++) {
    out.push((words[i] >>> 24) & 255, (words[i] >>> 16) & 255, (words[i] >>> 8) & 255, words[i] & 255);
  }
  return out;
}
function rotr(x, n) { return (x >>> n) | (x << (32 - n)); }
function rotl(x, n) { return (x << n) | (x >>> (32 - n)); }

function sha256(msg) {
  var m = padMessage(msg);
  var h = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
  var w = new Array(64);
  for (var off = 0; off < m.length; off += 64) {
    for (var t = 0; t < 16; t++) {
      w[t] = (m[off + 4 * t] << 24) | (m[off + 4 * t + 1] << 16) | (m[off + 4 * t + 2] << 8) | m[off + 4 * t + 3];
    }
    for (t = 16; t < 64; t++) {
      var s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >>> 3);
      var s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >>> 10);
      w[t] = (w[t - 16] + s0 + w[t - 7] + s1) | 0;
    }
    var a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (t = 0; t < 64; t++) {
      var S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      var ch = (e & f) ^ (~e & g);
      var t1 = (hh + S1 + ch + K256[t] + w[t]) | 0;
      var S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      var maj = (a & b) ^ (a & c) ^ (b & c);
      var t2 = (S0 + maj) | 0;
      hh = g; g = f; f = e; e = (d + t1) | 0; d = c; c = b; b = a; a = (t1 + t2) | 0;
    }
    h[0] = (h[0] + a) | 0; h[1] = (h[1] + b) | 0; h[2] = (h[2] + c) | 0; h[3] = (h[3] + d) | 0;
    h[4] = (h[4] + e) | 0; h[5] = (h[5] + f) | 0; h[6] = (h[6] + g) | 0; h[7] = (h[7] + hh) | 0;
  }
  return wordsToBytes(h);
}

// ---------- SHA-1 ----------
function sha1(msg) {
  var m = padMessage(msg);
  var h = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0];
  var w = new Array(80);
  for (var off = 0; off < m.length; off += 64) {
    for (var t = 0; t < 16; t++) {
      w[t] = (m[off + 4 * t] << 24) | (m[off + 4 * t + 1] << 16) | (m[off + 4 * t + 2] << 8) | m[off + 4 * t + 3];
    }
    for (t = 16; t < 80; t++) w[t] = rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
    var a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (t = 0; t < 80; t++) {
      var f, k;
      if (t < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
      else if (t < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
      else if (t < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
      else { f = b ^ c ^ d; k = 0xca62c1d6; }
      var tmp = (rotl(a, 5) + f + e + k + w[t]) | 0;
      e = d; d = c; c = rotl(b, 30); b = a; a = tmp;
    }
    h[0] = (h[0] + a) | 0; h[1] = (h[1] + b) | 0; h[2] = (h[2] + c) | 0; h[3] = (h[3] + d) | 0; h[4] = (h[4] + e) | 0;
  }
  return wordsToBytes(h);
}

// ---------- HMAC-SHA256 ----------
function hmacSha256(key, msg) {
  if (key.length > 64) key = sha256(key);
  var ipad = [], opad = [];
  for (var i = 0; i < 64; i++) {
    var k = key[i] || 0;
    ipad.push(k ^ 0x36); opad.push(k ^ 0x5c);
  }
  return sha256(opad.concat(sha256(ipad.concat(msg))));
}

// ---------- AES-128 (encrypt direction only; used for GCM/CTR decryption) ----------
var SBOX = null;
function buildSbox() {
  SBOX = new Array(256);
  var p = 1, q = 1;
  do {
    p = p ^ ((p << 1) & 255) ^ (p & 0x80 ? 0x1b : 0);
    q ^= q << 1; q ^= q << 2; q ^= q << 4; q &= 255;
    if (q & 0x80) q ^= 0x09;
    var x = q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4));
    SBOX[p] = (x ^ 0x63) & 255;
  } while (p !== 1);
  SBOX[0] = 0x63;
}
function xtime(a) { return ((a << 1) ^ (a & 0x80 ? 0x1b : 0)) & 255; }
function aesExpandKey(key) {
  if (!SBOX) buildSbox();
  var w = key.slice(0), rcon = 1;
  for (var i = 16; i < 176; i += 4) {
    var t = w.slice(i - 4, i);
    if (i % 16 === 0) {
      t = [SBOX[t[1]] ^ rcon, SBOX[t[2]], SBOX[t[3]], SBOX[t[0]]];
      rcon = xtime(rcon);
    }
    for (var j = 0; j < 4; j++) w.push(w[i - 16 + j] ^ t[j]);
  }
  return w;
}
function aesEncryptBlock(rk, input) {
  var s = [];
  var i, r, c;
  for (i = 0; i < 16; i++) s.push(input[i] ^ rk[i]);
  for (r = 1; r <= 10; r++) {
    for (i = 0; i < 16; i++) s[i] = SBOX[s[i]];
    var t = s.slice(0);
    for (c = 0; c < 4; c++) for (var row = 0; row < 4; row++) s[c * 4 + row] = t[((c + row) % 4) * 4 + row];
    if (r !== 10) {
      for (c = 0; c < 4; c++) {
        var a0 = s[c * 4], a1 = s[c * 4 + 1], a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
        var all = a0 ^ a1 ^ a2 ^ a3;
        s[c * 4] ^= all ^ xtime(a0 ^ a1);
        s[c * 4 + 1] ^= all ^ xtime(a1 ^ a2);
        s[c * 4 + 2] ^= all ^ xtime(a2 ^ a3);
        s[c * 4 + 3] ^= all ^ xtime(a3 ^ a0);
      }
    }
    for (i = 0; i < 16; i++) s[i] ^= rk[r * 16 + i];
  }
  return s;
}
// AES-GCM decryption without tag check (the connection to Tesla is already TLS-protected).
function aesGcmDecryptNoAuth(key, nonce, ciphertext) {
  var rk = aesExpandKey(key);
  var out = [];
  var counter = 2;
  for (var off = 0; off < ciphertext.length; off += 16) {
    var block = nonce.concat([(counter >>> 24) & 255, (counter >>> 16) & 255, (counter >>> 8) & 255, counter & 255]);
    var ks = aesEncryptBlock(rk, block);
    for (var i = 0; i < 16 && off + i < ciphertext.length; i++) out.push(ciphertext[off + i] ^ ks[i]);
    counter++;
  }
  return out;
}

// ---------- P-256 ECDH (BigInt) ----------
var P256 = null;
function haveBigInt() { return typeof BigInt === 'function'; }
function initP256() {
  if (P256) return P256;
  var B = BigInt;
  P256 = {
    p: B('0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff'),
    n: B('0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551'),
    b: B('0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b'),
    gx: B('0x6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296'),
    gy: B('0x4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5'),
    zero: B(0), one: B(1), two: B(2), three: B(3), four: B(4), eight: B(8)
  };
  return P256;
}
function md(a) { var p = P256.p; var r = a % p; return r < P256.zero ? r + p : r; }
function minv(a) {
  var p = P256.p, lm = P256.one, hm = P256.zero, low = md(a), high = p;
  while (low > P256.one) {
    var r = high / low;
    var nm = hm - lm * r, nw = high - low * r;
    hm = lm; high = low; lm = nm; low = nw;
  }
  return md(lm);
}
function bytesToBig(b) { return BigInt('0x' + (bytesToHex(b) || '0')); }
function bigToBytes(x, len) {
  var h = x.toString(16);
  while (h.length < len * 2) h = '0' + h;
  return hexToBytes(h);
}
// Jacobian point ops for a = -3
function jDouble(P) {
  var C = P256;
  if (P[2] === C.zero) return P;
  var X = P[0], Y = P[1], Z = P[2];
  var d = md(Z * Z);
  var alpha = md(C.three * md((X - d) * (X + d)));
  var yy = md(Y * Y);
  var beta = md(X * yy);
  var X3 = md(alpha * alpha - C.eight * beta);
  var Z3 = md(md((Y + Z) * (Y + Z)) - yy - d);
  var Y3 = md(alpha * (C.four * beta - X3) - C.eight * md(yy * yy));
  return [X3, Y3, Z3];
}
function jAdd(P, Q) {
  var C = P256;
  if (P[2] === C.zero) return Q;
  if (Q[2] === C.zero) return P;
  var Z1Z1 = md(P[2] * P[2]), Z2Z2 = md(Q[2] * Q[2]);
  var U1 = md(P[0] * Z2Z2), U2 = md(Q[0] * Z1Z1);
  var S1 = md(P[1] * Q[2] * Z2Z2), S2 = md(Q[1] * P[2] * Z1Z1);
  if (U1 === U2) {
    if (S1 !== S2) return [C.one, C.one, C.zero];
    return jDouble(P);
  }
  var H = md(U2 - U1), R = md(S2 - S1);
  var HH = md(H * H), HHH = md(H * HH), V = md(U1 * HH);
  var X3 = md(R * R - HHH - C.two * V);
  var Y3 = md(R * (V - X3) - S1 * HHH);
  var Z3 = md(H * P[2] * Q[2]);
  return [X3, Y3, Z3];
}
function scalarMult(k, x, y) {
  var C = initP256();
  var R = [C.one, C.one, C.zero];
  var Pt = [x, y, C.one];
  var bits = k.toString(2);
  for (var i = 0; i < bits.length; i++) {
    R = jDouble(R);
    if (bits.charAt(i) === '1') R = jAdd(R, Pt);
  }
  if (R[2] === C.zero) throw new Error('bad point');
  var zi = minv(R[2]), zi2 = md(zi * zi);
  return [md(R[0] * zi2), md(R[1] * zi2 * zi)];
}
function onCurve(x, y) {
  var C = P256;
  return md(y * y) === md(x * x * x - C.three * x + C.b);
}
// Returns uncompressed public key bytes (65) for a private scalar given as hex
function publicFromPrivate(privHex) {
  var C = initP256();
  var pt = scalarMult(bytesToBig(hexToBytes(privHex)), C.gx, C.gy);
  return [4].concat(bigToBytes(pt[0], 32), bigToBytes(pt[1], 32));
}
// K = SHA1(Sx)[:16]
function deriveSessionKey(privHex, peerPublicBytes) {
  initP256();
  if (peerPublicBytes.length !== 65 || peerPublicBytes[0] !== 4) throw new Error('bad vehicle key');
  var x = bytesToBig(peerPublicBytes.slice(1, 33)), y = bytesToBig(peerPublicBytes.slice(33, 65));
  if (!onCurve(x, y)) throw new Error('vehicle key not on curve');
  var S = scalarMult(bytesToBig(hexToBytes(privHex)), x, y);
  return sha1(bigToBytes(S[0], 32)).slice(0, 16);
}

module.exports = {
  hexToBytes: hexToBytes, bytesToHex: bytesToHex, strToBytes: strToBytes, bytesToStr: bytesToStr,
  b64encode: b64encode, b64decode: b64decode, randomBytes: randomBytes, equalBytes: equalBytes,
  sha256: sha256, sha1: sha1, hmacSha256: hmacSha256, aesGcmDecryptNoAuth: aesGcmDecryptNoAuth,
  haveBigInt: haveBigInt, publicFromPrivate: publicFromPrivate, deriveSessionKey: deriveSessionKey
};
