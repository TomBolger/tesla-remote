// Minimal protobuf writer/reader (only what the Tesla protocol needs). ES5.
/* eslint-disable no-bitwise */

function varint(n) {
  var out = [];
  n = n >>> 0;
  while (n > 127) { out.push((n & 127) | 128); n = n >>> 7; }
  out.push(n);
  return out;
}
function key(field, wire) { return varint((field << 3) | wire); }

// Each writer returns bytes for one field.
function fVarint(field, n) { return key(field, 0).concat(varint(n)); }
function fBool(field, b) { return fVarint(field, b ? 1 : 0); }
function fBytes(field, bytes) { return key(field, 2).concat(varint(bytes.length), bytes); }
function fMsg(field) { // fMsg(field, part1, part2...) -> embedded message
  var body = [];
  for (var i = 1; i < arguments.length; i++) body = body.concat(arguments[i] || []);
  return fBytes(field, body);
}
function fFixed32(field, n) {
  n = n >>> 0;
  return key(field, 5).concat([n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255]);
}
function fFloat(field, f) {
  var buf = new ArrayBuffer(4);
  new DataView(buf).setFloat32(0, f, true);
  var u = new Uint8Array(buf);
  return key(field, 5).concat([u[0], u[1], u[2], u[3]]);
}
function cat() {
  var out = [];
  for (var i = 0; i < arguments.length; i++) out = out.concat(arguments[i] || []);
  return out;
}

// Decode into { fieldNumber: [values...] }. Length-delimited values stay as byte arrays.
function decode(bytes) {
  var out = {}, i = 0;
  function readVarint() {
    var result = 0, mult = 1, b;
    do {
      if (i >= bytes.length) throw new Error('truncated');
      b = bytes[i++];
      result += (b & 127) * mult;
      mult *= 128;
    } while (b & 128);
    return result;
  }
  while (i < bytes.length) {
    var k = readVarint(), field = Math.floor(k / 8), wire = k & 7, val;
    if (wire === 0) val = readVarint();
    else if (wire === 2) { var len = readVarint(); val = bytes.slice(i, i + len); i += len; }
    else if (wire === 5) { val = (bytes[i] | (bytes[i + 1] << 8) | (bytes[i + 2] << 16) | (bytes[i + 3] << 24)) >>> 0; i += 4; }
    else if (wire === 1) { val = bytes.slice(i, i + 8); i += 8; }
    else throw new Error('bad wire type ' + wire);
    (out[field] = out[field] || []).push(val);
  }
  return out;
}
function first(msg, field) { return msg && msg[field] ? msg[field][0] : undefined; }
function sub(msg, field) { var v = first(msg, field); return v ? decode(v) : null; }

module.exports = {
  fVarint: fVarint, fBool: fBool, fBytes: fBytes, fMsg: fMsg, fFixed32: fFixed32, fFloat: fFloat,
  cat: cat, decode: decode, first: first, sub: sub
};
