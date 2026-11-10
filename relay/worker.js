// Tesla Remote relay - a Cloudflare Worker.
// Its only job: forward the watch app's requests to Tesla's car server and add the
// "browser code may read this" (CORS) headers that Tesla leaves out.
// It stores nothing and logs nothing.
//
// Request format:  https://<this-worker>/<tesla host>/<path>?<query>
// Example:         https://relay.example.workers.dev/fleet-api.prd.na.vn.cloud.tesla.com/api/1/vehicles

const ALLOWED_HOST = /^fleet-api\.prd\.[a-z]+\.vn\.cloud\.tesla\.com$/; // Tesla car servers only
const PASS_HEADERS = ['authorization', 'content-type', 'accept'];

const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Authorization, Content-Type, Accept',
  'Access-Control-Max-Age': '86400',
};

function reply(status, text) {
  return new Response(text, { status, headers: { ...CORS, 'Content-Type': 'application/json' } });
}

export default {
  async fetch(request) {
    if (request.method === 'OPTIONS') return new Response(null, { status: 204, headers: CORS });
    if (request.method !== 'GET' && request.method !== 'POST') return reply(405, '{"error":"method not allowed"}');

    const url = new URL(request.url);
    const parts = url.pathname.split('/');            // ["", host, ...path]
    const host = parts[1] || '';
    if (!ALLOWED_HOST.test(host)) {
      return reply(host ? 403 : 200, host ? '{"error":"host not allowed"}' : '{"ok":true,"service":"tesla-remote-relay"}');
    }
    const target = 'https://' + host + '/' + parts.slice(2).join('/') + url.search;

    const headers = new Headers();
    for (const h of PASS_HEADERS) {
      const v = request.headers.get(h);
      if (v) headers.set(h, v);
    }
    const init = { method: request.method, headers };
    if (request.method === 'POST') init.body = await request.arrayBuffer();

    let upstream;
    try {
      upstream = await fetch(target, init);
    } catch (e) {
      return reply(502, '{"error":"could not reach Tesla"}');
    }
    const out = new Headers(CORS);
    out.set('Content-Type', upstream.headers.get('content-type') || 'application/json');
    return new Response(upstream.body, { status: upstream.status, headers: out });
  },
};
