"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const root = path.resolve(__dirname, "..");
const source = fs.readFileSync(path.join(root, "product/web/app.js"), "utf8");
const index = fs.readFileSync(path.join(root, "product/web/index.html"), "utf8");
const encode = value => new TextEncoder().encode(value);
class Element {
  constructor() { this.dataset = {}; this.handlers = {}; this.children = [];
    this.classList = { toggle() {} }; this.value = ""; }
  addEventListener(name, handler) { this.handlers[name] = handler; }
  append(value) { this.children.push(value); }
  prepend(value) { this.children.unshift(value); }
  replaceChildren() { this.children = []; }
  remove() {}
  focus() {}
  querySelectorAll() { return []; }
}
function storage(initial = {}) {
  const values = new Map(Object.entries(initial));
  return { getItem: key => values.get(key) ?? null,
    setItem: (key, value) => values.set(key, String(value)),
    removeItem: key => values.delete(key) };
}
async function flush() { for (let i = 0; i < 100; ++i) await Promise.resolve(); }
function stream(chunks = [], { end = false, status = 200, media = "text/event-stream" } = {}) {
  let readerReleased;
  const observation = { canceled: 0, released: 0,
    finished: new Promise(resolve => { readerReleased = resolve; }) };
  if (status === 204) return { response: new Response(null, { status }), observation };
  const body = new ReadableStream({
    start(controller) {
      observation.controller = controller;
      for (const chunk of chunks) controller.enqueue(typeof chunk === "string" ? encode(chunk) : chunk);
      if (end) controller.close();
    },
    cancel() { observation.canceled++; },
  });
  const response = new Response(body, { status, headers: { "Content-Type": media } });
  const getReader = response.body.getReader.bind(response.body);
  response.body.getReader = () => {
    const reader = getReader();
    const release = reader.releaseLock.bind(reader);
    reader.releaseLock = () => { observation.released++; release(); readerReleased(); };
    return reader;
  };
  return { response, observation };
}
async function boot(href, { token = "", script = source, responses = [], requiredToken = null } = {}) {
  const elements = new Map(), timers = new Map(), requests = [], streams = [];
  let nextTimer = 0, promptValue = null;
  const document = {
    querySelector(selector) {
      if (!elements.has(selector)) elements.set(selector, new Element());
      return elements.get(selector);
    },
    querySelectorAll() { return []; }, createElement() { return new Element(); },
  };
  const context = { document, window: { location: { href } }, URL, Node: Element,
    TextDecoder, AbortController, sessionStorage: storage(token ? { "laplace.apiToken": token } : {}),
    localStorage: storage(), crypto: { getRandomValues(bytes) { bytes.fill(7); } },
    prompt() { return promptValue; },
    setTimeout(fn, ms) { const id = ++nextTimer; timers.set(id, { fn, ms }); return id; },
    clearTimeout(id) { timers.delete(id); },
    fetch: async (input, options) => {
      const call = { url: new URL(String(input), href), options };
      if (call.url.pathname.endsWith("/api/v1/stream")) {
        streams.push(call);
        const selected = responses.length ? responses.shift() : stream();
        if (selected instanceof Error) throw selected;
        return selected.then ? await selected : selected.response;
      }
      requests.push(call);
      if (requiredToken !== null && options.headers.Authorization !== "Bearer " + requiredToken) {
        return new Response(JSON.stringify({ error: { message: "unauthorized" } }),
          { status: 401, headers: { "Content-Type": "application/json" } });
      }
      return new Response(JSON.stringify({ status: "ready", counts: {}, rows: [] }),
        { headers: { "Content-Type": "application/json" } });
    },
  };
  vm.runInNewContext(script, context, { filename: "product/web/app.js", timeout: 1000 });
  await flush();
  if (requiredToken !== null && token !== requiredToken) {
    assert.equal(requests.length, 1, "unauthenticated boot must stop at rejected health");
    assert.equal(document.querySelector("#connection-label").textContent, "Unavailable");
  } else {
    assert.equal(requests.length, 3, "real boot must finish health, summary and selected facet");
    assert.equal(document.querySelector("#connection-label").textContent, "ready");
  }
  return { requests, streams, timers, node: selector => document.querySelector(selector),
    async click(selector) { document.querySelector(selector).handlers.click(); await flush(); },
    async replaceToken(value) { promptValue = value; await this.click("#auth-button"); },
    async retry() {
      const selected = [...timers.entries()].find(([, value]) => value.ms === 3000);
      assert.ok(selected, "a reconnect must be scheduled");
      timers.delete(selected[0]); selected[1].fn(); await flush();
    } };
}
function assertRoutes(observed, base) {
  assert.deepEqual(observed.requests.slice(0, 3).map(x => x.url.href), [
    new URL("api/v1/health", base).href, new URL("api/v1/summary", base).href,
    new URL("api/v1/entities?limit=100", base).href ]);
}
for (const href of ["http://127.0.0.1:55434/", "http://127.0.0.1:55434/index.html?view=x#y",
  "https://hart-server:8443/refactor/", "https://hart-server:8443/refactor/index.html?view=x#y"]) {
  test("real browser boot, assets and stream preserve served directory: " + href, async () => {
    const active = stream(), base = new URL(".", href);
    const observed = await boot(href, { responses: [active] });
    assertRoutes(observed, base);
    for (const name of ["styles.css", "app.js"]) {
      const match = index.match(new RegExp("(?:href|src)=\"([^\"]*" + name.replace(".", "\\.") + ")\""));
      assert.ok(match); assert.equal(new URL(match[1], href).href, new URL(name, base).href);
    }
    await observed.click("#events-toggle");
    assert.equal(observed.streams[0].url.href, new URL("api/v1/stream", base).href);
    await observed.click("#events-toggle");
    assert.equal(active.observation.canceled, 1); assert.equal(active.observation.released, 1);
    assert.equal(observed.timers.size, 0);
  });
}
test("existing bearer is sent in headers for both APIs and streamed events", async () => {
  const observed = await boot("https://hart-server:8443/refactor/", { token: "fixture-token" });
  await observed.click("#events-toggle");
  for (const call of [...observed.requests, ...observed.streams]) {
    assert.equal(call.options.headers.Authorization, "Bearer fixture-token");
    assert.equal(call.url.origin, "https://hart-server:8443");
    assert.equal(call.url.href.includes("fixture-token"), false);
  }
  assert.equal(observed.streams[0].options.redirect, "error");
  assert.equal(observed.streams[0].options.headers.Accept, "text/event-stream");
  await observed.click("#events-toggle");
});
test("UTF-8 and CRLF split at every byte reconstruct two actual server frames", { timeout: 2000 }, async () => {
  const wire = ':keepalive\r\nid: 7\r\nevent: product-snapshot\r\ndata: {"kind":"♞","timestamp":1}\r\n\r\n' +
    'event: product-snapshot\ndata: {"kind":\ndata: "second","timestamp":2}\n\n';
  const bytes = encode(wire), active = stream(Array.from(bytes, value => Uint8Array.of(value)), { end: true });
  const observed = await boot("https://hart-server:8443/refactor/", { token: "key", responses: [active] });
  await observed.click("#events-toggle");
  // EOF is processed before the production reader releases its lock. Await
  // that real lifecycle boundary instead of guessing a microtask count.
  await active.observation.finished;
  assert.equal(observed.node("#event-feed").children.length, 2);
  assert.match(observed.node("#event-feed").children[1].innerHTML, /♞/);
  assert.match(observed.node("#event-feed").children[0].innerHTML, /second/);
  await observed.click("#events-toggle");
  assert.equal(active.observation.released, 1);
});
test("complete frames are independent of coalesced network chunk size", async () => {
  const frame = 'event: product-snapshot\ndata: ' + JSON.stringify({ kind: "x".repeat(600000) }) + '\n\n';
  const active = stream([frame + frame]);
  const observed = await boot("https://hart-server:8443/refactor/", { responses: [active] });
  await observed.click("#events-toggle");
  assert.equal(observed.node("#event-feed").children.length, 2);
  await observed.click("#events-toggle");
});
test("EOF reconnects, keeps last event ID, discards incomplete frame and can abort retry", async () => {
  const first = stream(['id: 9\nevent: product-snapshot\ndata: {"kind":"first"}\n\n',
    'event: product-snapshot\ndata: {"kind":"incomplete"}'], { end: true });
  const second = stream();
  const observed = await boot("https://hart-server:8443/refactor/", { responses: [first, second], token: "key" });
  await observed.click("#events-toggle");
  assert.equal(observed.node("#event-feed").children.length, 1);
  assert.equal(first.observation.released, 1);
  await observed.retry();
  assert.equal(observed.streams.length, 2);
  assert.equal(observed.streams[1].options.headers["Last-Event-ID"], "9");
  await observed.click("#events-toggle");
  assert.equal(second.observation.canceled, 1);
  const ended = await boot("http://127.0.0.1:55434/", { responses: [stream([], { end: true })] });
  await ended.click("#events-toggle"); await ended.click("#events-toggle");
  assert.equal(ended.timers.size, 0); assert.equal(ended.streams.length, 1);
});
for (const status of [401, 403, 204]) test("terminal HTTP status stops and releases: " + status, async () => {
  const active = stream([], { status });
  const observed = await boot("https://hart-server:8443/refactor/", { responses: [active], token: "bad" });
  await observed.click("#events-toggle");
  assert.equal(observed.node("#events-toggle").textContent, "Start");
  assert.equal(observed.timers.size, 0);
  if (status !== 204) { assert.equal(active.observation.canceled, 1);
    assert.match(observed.node("#event-connection").textContent, /authentication/); }
});
test("network and invalid-content responses retry without retaining the previous body", async () => {
  const wrong = stream([], { media: "text/html" }), active = stream();
  const observed = await boot("https://hart-server:8443/refactor/", {
    responses: [new Error("network failure"), wrong, active] });
  await observed.click("#events-toggle"); await observed.retry();
  assert.equal(wrong.observation.canceled, 1);
  await observed.retry(); await observed.click("#events-toggle");
  assert.equal(active.observation.released, 1); assert.equal(observed.timers.size, 0);
});
test("server error frames remain visible and later snapshots can recover", async () => {
  const active = stream(['event: error\ndata: {"error":"native unavailable"}\n\n']);
  const observed = await boot("http://127.0.0.1:55434/", { responses: [active] });
  await observed.click("#events-toggle");
  assert.equal(observed.node("#event-connection").textContent, "native unavailable");
  active.observation.controller.enqueue(encode('event: product-snapshot\ndata: {"kind":"recovered"}\n\n'));
  await flush(); assert.equal(observed.node("#event-connection").textContent, "Live");
  await observed.click("#events-toggle");
});
test("token replacement aborts old reader and cannot be stopped by its late401 response", async () => {
  let resolve;
  const late = new Promise(value => { resolve = value; }), second = stream();
  const observed = await boot("https://hart-server:8443/refactor/", { responses: [late, second], token: "old" });
  await observed.click("#events-toggle"); await observed.replaceToken("new");
  const oldResponse = stream([], { status: 401 }); resolve(oldResponse.response); await flush();
  assert.equal(observed.streams.length, 2);
  assert.equal(observed.streams[1].options.headers.Authorization, "Bearer new");
  assert.equal(observed.node("#events-toggle").textContent, "Stop");
  assert.equal(oldResponse.observation.canceled, 1);
  await observed.click("#events-toggle");
  assert.equal(second.observation.canceled, 1);
});
test("stop releases a pending read without emitting late frames", async () => {
  const active = stream(), observed = await boot("http://127.0.0.1:55434/", { responses: [active] });
  await observed.click("#events-toggle"); await observed.click("#events-toggle");
  assert.equal(active.observation.canceled, 1); assert.equal(active.observation.released, 1);
  assert.equal(observed.streams[0].options.signal.aborted, true);
  assert.equal(observed.node("#event-feed").children.length, 0);
});
test("original absolute-fetch mutation is rejected by the real boot route assertion", async () => {
  const mutant = source.replace("fetch(surfaceUrl(path),", "fetch(path,");
  assert.notEqual(mutant, source);
  const href = "https://hart-server:8443/refactor/", observed = await boot(href, { script: mutant });
  assert.throws(() => assertRoutes(observed, new URL(href)), assert.AssertionError);
});

test("first credential entry loads health and Explore after an actual401 boot", async () => {
  const href = "https://hart-server:8443/refactor/";
  for (const script of [source, source.replace("refreshHealth().then(refreshExplore)", "refreshHealth()")]) {
    const observed = await boot(href, { script, requiredToken: "first-token" });
    await observed.replaceToken("first-token");
    assert.equal(observed.node("#connection-label").textContent, "ready");
    if (script === source) {
      assert.equal(observed.requests.length, 4);
      assertRoutes({ requests: observed.requests.slice(1) }, new URL(href));
      for (const call of observed.requests.slice(1)) {
        assert.equal(call.options.headers.Authorization, "Bearer first-token");
      }
      assert.match(observed.node("#explore-status").textContent, /entities rows/);
    } else {
      assert.equal(observed.requests.length, 2, "the old action exposes the missing Explore refresh");
      assert.throws(() => assertRoutes({ requests: observed.requests.slice(1) }, new URL(href)), assert.AssertionError);
    }
  }
});
