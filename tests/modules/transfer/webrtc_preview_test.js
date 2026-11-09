'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync(process.argv[2], 'utf8');
const script = html.split('<script>')[1].split('</script>')[0];
const buttons = {};
const video = { srcObject: null, videoWidth: 0, play: async () => {}, addEventListener() {} };
const status = { textContent: '' };
const requests = [], pcs = [], intervals = new Map();
let next = 0, ready = true, delayPost = false, pendingPost, activeIds = [];
class Peer {
  constructor() { this.iceGatheringState = 'complete'; this.connectionState = 'new'; pcs.push(this); }
  addTransceiver(kind, options) { assert.equal(kind, 'video'); assert.equal(options.direction, 'recvonly'); }
  async createOffer() { return { type: 'offer', sdp: 'test-offer' }; }
  async setLocalDescription(description) { this.localDescription = description; }
  async setRemoteDescription(description) { this.remoteDescription = description; this.connectionState = 'connected'; this.onconnectionstatechange?.(); }
  addEventListener() {}
  removeEventListener() {}
  close() { this.closed = true; this.connectionState = 'closed'; this.onconnectionstatechange?.(); }
}
const context = {
  console, AbortController, DOMException, RTCPeerConnection: Peer,
  MediaStream: class { constructor(tracks) { this.tracks = tracks; } },
  document: { getElementById(id) { return id === 'video' ? video : id === 'status' ? status : (buttons[id] ||= {}); } },
  window: { addEventListener(name, callback) { this[name] = callback; } },
  setTimeout, clearTimeout,
  setInterval(fn) { const id = ++next; intervals.set(id, fn); return id; },
  clearInterval(id) { intervals.delete(id); },
  fetch: async (path, options = {}) => {
    requests.push({ path, options });
    if (path.endsWith('/status')) return { ok: true, status: 200, json: async () => ({ video_available: ready, sessions: activeIds.map(id => ({ id })) }) };
    if (options.method === 'POST') {
      const body = JSON.parse(options.body);
      assert.equal(body.type, 'offer'); assert.equal(body.sdp, 'test-offer');
      if (delayPost) await new Promise(resolve => { pendingPost = resolve; });
      const id = `session-${++next}`; activeIds.push(id);
      return { ok: true, status: 201, json: async () => ({ id, type: 'answer', sdp: 'test-answer' }) };
    }
    if (options.method === 'DELETE') { activeIds = activeIds.filter(id => !path.endsWith(id)); return { ok: true, status: 204 }; }
    throw new Error('unexpected request ' + path);
  }
};
vm.createContext(context);
vm.runInContext(script + '\n globalThis.preview = { connect, disconnect };', context);
(async () => {
  const preview = context.preview;
  await preview.connect();
  assert.equal(pcs.at(-1).remoteDescription.sdp, 'test-answer');
  assert.equal(activeIds.length, 1);
  await preview.disconnect();
  assert.equal(activeIds.length, 0); assert(pcs.at(-1).closed); assert.equal(intervals.size, 0);
  // Disconnect during POST: stale answer must be explicitly deleted, not applied.
  delayPost = true;
  const attempt = preview.connect();
  for (let i = 0; !pendingPost && i < 100; ++i) await new Promise(resolve => setImmediate(resolve));
  assert(pendingPost);
  await preview.disconnect(); pendingPost(); await attempt;
  assert.equal(activeIds.length, 0); assert(!pcs.at(-1).remoteDescription);
  delayPost = false;
  await Promise.all([preview.connect(), preview.connect()]);
  assert.equal(activeIds.length, 1); assert(pcs.slice(0, -1).every(pc => pc.closed));
  // Stream restart / missing session: status poll releases the local peer.
  activeIds = [];
  for (const poll of [...intervals.values()]) await poll();
  assert(pcs.at(-1).closed); assert.equal(intervals.size, 0);
  ready = false;
  await preview.connect();
  assert(status.textContent.includes('视频尚未就绪')); assert(pcs.at(-1).closed);
  ready = true;
  await preview.connect(); context.window.pagehide();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(activeIds.length, 0); assert.equal(intervals.size, 0);
  assert.equal(video.srcObject, null);
  console.log('Preview connection, cancellation, stale-answer cleanup, reconnect and pagehide passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
