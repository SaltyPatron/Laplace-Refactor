(() => {
  "use strict";
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => Array.from(root.querySelectorAll(selector));

  class ConnectionState {
    constructor() { this.dot = $("#connection-dot"); this.label = $("#connection-label"); }
    set(state, label) { this.dot.dataset.state = state; this.label.textContent = label; }
  }

  class ReceiptPanel {
    static render(value) {
      if (!value || typeof value !== "object") return "";
      const fields = [
        ["Receipt", value.execution_receipt_id || value.receipt_id],
        ["Checkpoint", value.next_checkpoint_fingerprint],
        ["Program", value.program_id], ["Package", value.package_id],
        ["Discourse", value.discourse_fingerprint], ["Ordinal", value.turn_ordinal ?? value.ordinal],
      ].filter(([, v]) => v !== undefined && v !== null && v !== "");
      if (!fields.length && value.execution && typeof value.execution === "object") return ReceiptPanel.render(value.execution);
      if (!fields.length) return "";
      return `<dl class="receipt-panel">${fields.map(([k, v]) => `<div><dt>${escapeHtml(k)}</dt><dd><code>${escapeHtml(String(v))}</code></dd></div>`).join("")}</dl>`;
    }
  }

  class ResourceTable {
    static rows(value) { if (Array.isArray(value)) return value; if (value && Array.isArray(value.rows)) return value.rows; return []; }
    static render(value, { onRow } = {}) {
      const rows = ResourceTable.rows(value);
      if (!rows.length) return `<pre class="json-view">${escapeHtml(JSON.stringify(value, null, 2))}</pre>`;
      const columns = Array.from(rows.reduce((set, row) => { if (row && typeof row === "object") Object.keys(row).forEach(k => set.add(k)); return set; }, new Set()));
      const html = `<div class="table-wrap"><table class="resource-table"><thead><tr>${columns.map(c => `<th>${escapeHtml(c)}</th>`).join("")}</tr></thead><tbody>${rows.map((row, index) => `<tr data-row-index="${index}" tabindex="0">${columns.map(c => `<td>${renderCell(row[c])}</td>`).join("")}</tr>`).join("")}</tbody></table></div>`;
      const wrapper = document.createElement("div");
      wrapper.innerHTML = html;
      if (onRow) $$(`tbody tr`, wrapper).forEach(tr => {
        const activate = () => onRow(rows[Number(tr.dataset.rowIndex)]);
        tr.addEventListener("click", activate);
        tr.addEventListener("keydown", event => { if (event.key === "Enter" || event.key === " ") { event.preventDefault(); activate(); } });
      });
      return wrapper;
    }
  }

  class RecordDetail {
    static render(value) { return `<article class="record-detail card"><div class="record-detail-head"><h3>Record detail</h3><button class="quiet-button detail-close" type="button">Close</button></div>${ReceiptPanel.render(value)}<pre class="json-view">${escapeHtml(JSON.stringify(value, null, 2))}</pre></article>`; }
  }

  class QuerySpec {
    constructor(collection, limit = 25, id = null) { this.collection = collection; this.limit = limit; if (id) this.id = id; }
    toJSON() { return { collection: this.collection, limit: this.limit, ...(this.id ? { id: this.id } : {}) }; }
  }

  const connection = new ConnectionState();
  const state = { token: sessionStorage.getItem("laplace.apiToken") || "", session: localStorage.getItem("laplace.session") || randomSession(), workspace: "graph", events: null, eventsPaused: false };
  localStorage.setItem("laplace.session", state.session);

  function randomSession() { const bytes = new Uint8Array(10); crypto.getRandomValues(bytes); return "web-" + Array.from(bytes, b => b.toString(16).padStart(2, "0")).join(""); }
  function escapeHtml(value) { return String(value).replace(/[&<>"']/g, char => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[char]); }
  function renderCell(value) { if (value === null || value === undefined) return '<span class="muted">null</span>'; if (typeof value === "object") return `<code>${escapeHtml(JSON.stringify(value))}</code>`; const text = String(value); return text.length > 96 ? `<code title="${escapeHtml(text)}">${escapeHtml(text.slice(0, 92))}…</code>` : `<code>${escapeHtml(text)}</code>`; }
  function toast(message, kind = "info") { const node = document.createElement("div"); node.className = `toast ${kind}`; node.textContent = message; $("#toast-region").append(node); setTimeout(() => node.remove(), 5000); }
  function setBusy(element, busy, text = "") { if (!element) return; element.textContent = text; element.dataset.busy = busy ? "true" : "false"; }
  function authHeaders() { return state.token ? { Authorization: `Bearer ${state.token}` } : {}; }

  async function api(path, options = {}) {
    const headers = { Accept: "application/json", ...authHeaders(), ...(options.body ? { "Content-Type": "application/json" } : {}), ...(options.headers || {}) };
    const response = await fetch(path, { ...options, headers });
    const contentType = response.headers.get("content-type") || "";
    const value = contentType.includes("application/json") ? await response.json() : await response.text();
    if (!response.ok) throw new Error(value?.error?.message || value?.error || value || `HTTP ${response.status}`);
    return value;
  }

  function showWorkspace(name) {
    state.workspace = name;
    $$(".nav-item").forEach(button => button.classList.toggle("active", button.dataset.workspace === name));
    $$(".workspace-panel").forEach(panel => panel.classList.toggle("active", panel.dataset.panel === name));
    $("#workspace").focus({ preventScroll: true });
    if (name === "events") startEvents();
    if (name === "admin") refreshAdmin();
    if (name === "sources") refreshSources();
  }

  function mountResult(target, content) {
    const node = typeof target === "string" ? $(target) : target;
    node.replaceChildren();
    if (typeof content === "string") node.innerHTML = content; else if (content instanceof Node) node.append(content);
  }

  async function refreshHealth() {
    try { const value = await api("/api/v1/health"); connection.set("ready", value.status === "ready" ? "Ready" : String(value.status)); return value; }
    catch (error) { connection.set("error", "Unavailable"); throw error; }
  }

  async function runGraphQuery() {
    const status = $("#graph-status"); const collection = $("#graph-collection").value; const limit = Number($("#graph-limit").value || 25); const id = $("#graph-id").value.trim() || null;
    setBusy(status, true, "Executing canonical query…");
    try {
      const value = await api("/api/v1/query", { method: "POST", body: JSON.stringify(new QuerySpec(collection, limit, id).toJSON()) });
      setBusy(status, false, schemaLabel(value));
      const onRow = collection === "entities" ? row => row.entity_id && loadEntityDetail(row.entity_id) : row => showDetail("#graph-result", row);
      mountResult("#graph-result", ResourceTable.render(value, { onRow }));
    } catch (error) { setBusy(status, false, error.message); toast(error.message, "error"); }
  }

  async function loadEntityDetail(entityId) {
    try { const value = await api(`/api/v1/entities/${encodeURIComponent(entityId)}`); showDetail("#graph-result", value); }
    catch (error) { toast(error.message, "error"); }
  }

  function showDetail(target, row) {
    const wrapper = document.createElement("div"); wrapper.innerHTML = RecordDetail.render(row); const host = typeof target === "string" ? $(target) : target; host.prepend(wrapper);
    $(".detail-close", wrapper)?.addEventListener("click", () => wrapper.remove()); wrapper.scrollIntoView({ behavior: "smooth", block: "start" });
  }
  function schemaLabel(value) { return value?.schema ? `Loaded ${value.schema}` : "Loaded"; }

  async function refreshSources() {
    try { const value = await api("/api/v1/sources?limit=100"); mountResult("#sources-result", ResourceTable.render(value, { onRow: row => showDetail("#sources-result", row) })); }
    catch (error) { mountResult("#sources-result", `<div class="error-panel">${escapeHtml(error.message)}</div>`); }
  }

  async function admitSource(event) {
    event.preventDefault(); const status = $("#source-admit-status"); const result = $("#source-admit-result"); setBusy(status, true, "Compiling and admitting source estate…"); result.hidden = true;
    try {
      const body = { profile: $("#source-profile").value, source_root: $("#source-root").value.trim() }; const unicodeRoot = $("#unicode-root").value.trim(); if (unicodeRoot) body.unicode_root = unicodeRoot;
      const value = await api("/api/v1/sources/admit", { method: "POST", body: JSON.stringify(body) });
      result.textContent = JSON.stringify(value, null, 2); result.hidden = false; setBusy(status, false, "Persisted and read back."); await refreshSources();
    } catch (error) { setBusy(status, false, error.message); toast(error.message, "error"); }
  }

  function renderConversationMessage(role, content, metadata = null) {
    const article = document.createElement("article"); article.className = `message ${role}`; article.innerHTML = `<div class="message-role">${role === "user" ? "You" : "Laplace"}</div><div class="message-content"></div>${metadata ? ReceiptPanel.render(metadata) : ""}`;
    $(".message-content", article).textContent = content; $("#conversation").append(article); article.scrollIntoView({ behavior: "smooth", block: "end" });
  }

  async function executeCognition(event) {
    event.preventDefault(); const prompt = $("#cognition-prompt").value; if (!prompt.trim()) return; const status = $("#cognition-status"); renderConversationMessage("user", prompt); $("#cognition-prompt").value = ""; setBusy(status, true, "Executing native cognition…");
    try {
      const value = await api("/api/v1/cognition", { method: "POST", body: JSON.stringify({ prompt, session: state.session }) });
      const output = value.output_utf8 ?? (value.output_hex ? `[binary ${value.output_hex.length / 2} bytes]` : ""); renderConversationMessage("assistant", output, value); setBusy(status, false, `status=${value.status} ordinal=${value.turn_ordinal ?? "?"}`);
    } catch (error) { setBusy(status, false, error.message); toast(error.message, "error"); }
  }

  function newCognitionSession() { state.session = randomSession(); localStorage.setItem("laplace.session", state.session); $("#session-name").textContent = state.session; $("#conversation").replaceChildren(); toast("Started a new cognition session."); }

  async function runSql() {
    const status = $("#sql-status"); setBusy(status, true, "Executing read-only SQL…");
    try {
      const value = await api("/api/v1/sql", { method: "POST", body: JSON.stringify({ sql: $("#sql-editor").value }) }); setBusy(status, false, `${value.row_count} row${value.row_count === 1 ? "" : "s"}`);
      if (!value.columns?.length) { mountResult("#sql-result", `<div class="empty-state">Query returned no columns.</div>`); return; }
      const rows = value.rows.map(row => Object.fromEntries(value.columns.map((column, index) => [column, row[index]]))); mountResult("#sql-result", ResourceTable.render({ rows }, { onRow: row => showDetail("#sql-result", row) }));
    } catch (error) { setBusy(status, false, error.message); toast(error.message, "error"); }
  }

  function startEvents() {
    if (state.events || state.eventsPaused) return;
    if (state.token) { $("#event-connection").textContent = "Authenticated remote mode uses polling; EventSource cannot attach Authorization headers."; return; }
    const source = new EventSource("/api/v1/stream"); state.events = source; $("#event-connection").textContent = "Connecting live stream…";
    source.addEventListener("product-snapshot", event => { $("#event-connection").textContent = "Live"; try { appendEvent(JSON.parse(event.data)); } catch { appendEvent({ kind: "parse-error", raw: event.data }); } });
    source.addEventListener("error", event => { if (event.data) { try { appendEvent(JSON.parse(event.data)); } catch {} } $("#event-connection").textContent = "Reconnecting…"; });
  }
  function stopEvents() { state.events?.close(); state.events = null; }

  function appendEvent(value) {
    if (state.eventsPaused) return; const node = document.createElement("article"); node.className = "event-item"; const when = value.timestamp ? new Date(value.timestamp * 1000).toLocaleTimeString() : new Date().toLocaleTimeString(); const counts = value.substrate?.counts;
    node.innerHTML = `<div class="event-meta"><span>${escapeHtml(value.kind || "event")}</span><time>${escapeHtml(when)}</time></div>${counts ? `<div class="metric-line"><span>entities <strong>${escapeHtml(counts.entities)}</strong></span><span>physicalities <strong>${escapeHtml(counts.physicalities)}</strong></span><span>attestations <strong>${escapeHtml(counts.attestations)}</strong></span></div>` : ""}<details><summary>Payload</summary><pre class="json-view">${escapeHtml(JSON.stringify(value, null, 2))}</pre></details>`;
    $("#event-feed").prepend(node); while ($("#event-feed").children.length > 100) $("#event-feed").lastElementChild.remove();
  }

  async function refreshAdmin() {
    const panels = ["#admin-health", "#admin-summary", "#admin-descriptors"]; panels.forEach(selector => mountResult(selector, '<div class="skeleton">Loading…</div>'));
    const results = await Promise.allSettled([api("/api/v1/health"), api("/api/v1/summary"), api("/api/v1/descriptors")]);
    results.forEach((result, index) => { if (result.status === "fulfilled") mountResult(panels[index], `<pre class="json-view compact">${escapeHtml(JSON.stringify(result.value, null, 2))}</pre>`); else mountResult(panels[index], `<div class="error-panel">${escapeHtml(result.reason.message)}</div>`); });
  }

  function configureAuth() {
    const current = state.token ? "Token is set for this tab." : "No token is set."; const value = prompt(`${current}\nEnter a bearer token, or leave blank to clear it:`, ""); if (value === null) return;
    state.token = value.trim(); if (state.token) sessionStorage.setItem("laplace.apiToken", state.token); else sessionStorage.removeItem("laplace.apiToken"); stopEvents(); if (state.workspace === "events") startEvents(); refreshHealth().catch(error => toast(error.message, "error"));
  }

  function bind() {
    $$(".nav-item").forEach(button => button.addEventListener("click", () => showWorkspace(button.dataset.workspace)));
    $("#auth-button").addEventListener("click", configureAuth); $("#graph-refresh").addEventListener("click", runGraphQuery);
    $("#graph-collection").addEventListener("change", () => { const detail = $("#graph-collection").value === "entity"; $("#graph-id-label").hidden = !detail; $("#graph-limit").disabled = detail || $("#graph-collection").value === "summary"; });
    $("#sources-refresh").addEventListener("click", refreshSources); $("#source-admit-form").addEventListener("submit", admitSource); $("#cognition-form").addEventListener("submit", executeCognition); $("#new-session").addEventListener("click", newCognitionSession); $("#sql-run").addEventListener("click", runSql);
    $("#events-toggle").addEventListener("click", () => { state.eventsPaused = !state.eventsPaused; $("#events-toggle").textContent = state.eventsPaused ? "Resume" : "Pause"; if (state.eventsPaused) stopEvents(); else startEvents(); $("#event-connection").textContent = state.eventsPaused ? "Paused" : "Connecting…"; });
    $("#events-clear").addEventListener("click", () => $("#event-feed").replaceChildren()); $("#admin-refresh").addEventListener("click", refreshAdmin); $("#session-name").textContent = state.session;
  }

  async function boot() { bind(); connection.set("connecting", "Connecting"); try { await refreshHealth(); await runGraphQuery(); } catch (error) { toast(error.message, "error"); } }
  boot();
})();
