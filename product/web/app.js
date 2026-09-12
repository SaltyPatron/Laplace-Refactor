(() => {
  "use strict";
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => Array.from(root.querySelectorAll(selector));
  const HEX128 = /^[0-9a-f]{32}$/i;

  const state = {
    token: sessionStorage.getItem("laplace.apiToken") || "",
    session: localStorage.getItem("laplace.session") || randomToken("web"),
    facet: localStorage.getItem("laplace.exploreFacet") || "entities",
    exploreRows: [],
    sourcePlan: null,
    sourceJobId: localStorage.getItem("laplace.sourceJobId") || null,
    sourcePoll: null,
    events: null,
  };
  localStorage.setItem("laplace.session", state.session);

  const FACETS = {
    entities: "/api/v1/entities?limit=100",
    consensus: "/api/v1/consensus?limit=100",
    standings: "/api/v1/standings?limit=100",
    evidence: "/api/v1/evidence?limit=100",
    "source-profiles": "/api/v1/sources?limit=100",
    physicalities: "/api/v1/physicalities?limit=100",
    attestations: "/api/v1/attestations?limit=100",
  };

  function randomToken(prefix) {
    const bytes = new Uint8Array(10);
    crypto.getRandomValues(bytes);
    return `${prefix}-` + Array.from(bytes, byte => byte.toString(16).padStart(2, "0")).join("");
  }

  function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, character => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
    })[character]);
  }

  function authHeaders() {
    return state.token ? { Authorization: `Bearer ${state.token}` } : {};
  }

  async function api(path, options = {}) {
    const headers = {
      Accept: "application/json",
      ...authHeaders(),
      ...(options.body ? { "Content-Type": "application/json" } : {}),
      ...(options.headers || {}),
    };
    const response = await fetch(path, { ...options, headers });
    const type = response.headers.get("content-type") || "";
    const value = type.includes("application/json") ? await response.json() : await response.text();
    if (!response.ok) {
      throw new Error(value?.error?.message || value?.error || value || `HTTP ${response.status}`);
    }
    return value;
  }

  function setStatus(selector, text, busy = false) {
    const node = $(selector);
    if (!node) return;
    node.textContent = text;
    node.dataset.busy = busy ? "true" : "false";
  }

  function toast(message, kind = "info") {
    const node = document.createElement("div");
    node.className = `toast ${kind}`;
    node.textContent = message;
    $("#toast-region").append(node);
    setTimeout(() => node.remove(), 5000);
  }

  function mount(selector, content) {
    const host = $(selector);
    host.replaceChildren();
    if (typeof content === "string") host.innerHTML = content;
    else if (content instanceof Node) host.append(content);
  }

  function renderCell(value) {
    if (value === null || value === undefined) return '<span class="muted">null</span>';
    const text = typeof value === "object" ? JSON.stringify(value) : String(value);
    const shown = text.length > 96 ? text.slice(0, 92) + "…" : text;
    return `<code title="${escapeHtml(text)}">${escapeHtml(shown)}</code>`;
  }

  function rows(value) {
    return Array.isArray(value) ? value : (Array.isArray(value?.rows) ? value.rows : []);
  }

  function table(value, onRow = null) {
    const data = rows(value);
    const wrap = document.createElement("div");
    if (!data.length) {
      wrap.className = "empty-state";
      wrap.textContent = "No rows in this scope.";
      return wrap;
    }
    const columns = Array.from(data.reduce((set, row) => {
      Object.keys(row || {}).forEach(key => set.add(key));
      return set;
    }, new Set()));
    wrap.className = "table-wrap";
    wrap.innerHTML = `<table class="resource-table"><thead><tr>${
      columns.map(column => `<th>${escapeHtml(column)}</th>`).join("")
    }</tr></thead><tbody>${
      data.map((row, index) => `<tr tabindex="0" data-i="${index}">${
        columns.map(column => `<td>${renderCell(row[column])}</td>`).join("")
      }</tr>`).join("")
    }</tbody></table>`;
    if (onRow) {
      $$('tbody tr', wrap).forEach(rowNode => {
        const activate = () => onRow(data[Number(rowNode.dataset.i)]);
        rowNode.addEventListener("click", activate);
        rowNode.addEventListener("keydown", event => {
          if (event.key === "Enter" || event.key === " ") {
            event.preventDefault();
            activate();
          }
        });
      });
    }
    return wrap;
  }

  function rowEntityId(row) {
    for (const key of ["entity_id", "proposition_entity_id", "proposition_id"]) {
      const value = row?.[key];
      if (typeof value === "string" && HEX128.test(value)) return value.toLowerCase();
    }
    return null;
  }

  function showWorkspace(name) {
    $$(".nav-item").forEach(button => button.classList.toggle("active", button.dataset.workspace === name));
    $$(".workspace-panel").forEach(panel => panel.classList.toggle("active", panel.dataset.panel === name));
    $("#workspace").focus({ preventScroll: true });
    if (name === "explore") refreshExplore();
    if (name === "operator") {
      refreshSources();
      refreshAdmin();
    }
  }

  async function refreshHealth() {
    try {
      const value = await api("/api/v1/health");
      $("#connection-dot").dataset.state = "ready";
      $("#connection-label").textContent = value.status || "Ready";
      return value;
    } catch (error) {
      $("#connection-dot").dataset.state = "error";
      $("#connection-label").textContent = "Unavailable";
      throw error;
    }
  }

  function metric(label, value) {
    return `<article class="metric-card"><span>${escapeHtml(label)}</span><strong>${escapeHtml(value ?? 0)}</strong></article>`;
  }

  async function refreshExplore() {
    const summary = await api("/api/v1/summary");
    const counts = summary.counts || {};
    $("#explore-summary").innerHTML = [
      metric("Entities", counts.entities),
      metric("Physicalities", counts.physicalities),
      metric("Occurrences", counts.attestations),
      metric("Consensus", counts.consensus),
      metric("Evidence", counts.evidence_nodes),
      metric("Ranking states", counts.standing_states),
      metric("Arenas", counts.standing_arenas),
      metric("Sources", counts.source_profiles),
    ].join("");
    await loadFacet(state.facet);
  }

  async function loadFacet(facet) {
    if (!FACETS[facet]) return;
    state.facet = facet;
    localStorage.setItem("laplace.exploreFacet", facet);
    $$(".facet-button").forEach(button => button.classList.toggle("active", button.dataset.facet === facet));
    setStatus("#explore-status", `Loading ${facet}…`, true);
    try {
      const value = await api(FACETS[facet]);
      state.exploreRows = rows(value);
      mount("#explore-result", table(value, row => {
        const entityId = rowEntityId(row);
        if (entityId) openEntity(entityId);
        else showRaw("#explore-result", row);
      }));
      setStatus("#explore-status", `${state.exploreRows.length} ${facet} row${state.exploreRows.length === 1 ? "" : "s"}`);
    } catch (error) {
      setStatus("#explore-status", error.message);
      toast(error.message, "error");
    }
  }

  function showRaw(selector, row) {
    const box = document.createElement("article");
    box.className = "card record-detail";
    box.innerHTML = `<div class="record-detail-head"><h3>Record</h3><button class="quiet-button" type="button">Close</button></div><pre class="json-view">${escapeHtml(JSON.stringify(row, null, 2))}</pre>`;
    $("button", box).addEventListener("click", () => box.remove());
    $(selector).prepend(box);
  }

  function entityFacet(title, data) {
    const section = document.createElement("section");
    section.className = "entity-facet";
    section.innerHTML = `<h4>${escapeHtml(title)}</h4>`;
    section.append(table({ rows: Array.isArray(data) ? data : [] }));
    return section;
  }

  async function openEntity(entityId) {
    setStatus("#explore-status", `Opening entity ${entityId}…`, true);
    try {
      const value = await api(`/api/v1/entities/${encodeURIComponent(entityId)}`);
      const entity = value.entity;
      const article = document.createElement("article");
      article.className = "entity-world card";
      if (!entity) {
        article.innerHTML = '<div class="empty-state">That canonical entity is not present in the active world.</div>';
      } else {
        article.innerHTML = `<div class="record-detail-head"><div><p class="eyebrow">Canonical entity</p><h3>${escapeHtml(entity.entity_id)}</h3></div><button class="quiet-button entity-close" type="button">Close</button></div><dl class="entity-summary"><div><dt>Identity witness</dt><dd><code>${escapeHtml(entity.identity_witness)}</code></dd></div><div><dt>Physicalities</dt><dd>${value.physicalities?.length || 0}</dd></div><div><dt>Occurrences</dt><dd>${value.attestations?.length || 0}</dd></div><div><dt>Consensus</dt><dd>${value.consensus?.length || 0}</dd></div><div><dt>Evidence</dt><dd>${value.evidence?.length || 0}</dd></div></dl>`;
        const facets = document.createElement("div");
        facets.className = "entity-facets";
        facets.append(
          entityFacet("Consensus", value.consensus),
          entityFacet("Evidence", value.evidence),
          entityFacet("Occurrences / testimony", value.attestations),
          entityFacet("Physical structure", value.physicalities),
        );
        article.append(facets);
        $(".entity-close", article).addEventListener("click", () => article.remove());
      }
      mount("#explore-entity", article);
      setStatus("#explore-status", `Entity ${entityId}`);
    } catch (error) {
      setStatus("#explore-status", error.message);
      toast(error.message, "error");
    }
  }

  function filterExplore(event) {
    event.preventDefault();
    const query = $("#explore-search").value.trim();
    if (!query) {
      loadFacet(state.facet);
      return;
    }
    if (HEX128.test(query)) {
      openEntity(query.toLowerCase());
      return;
    }
    const needle = query.toLowerCase();
    const filtered = state.exploreRows.filter(row =>
      Object.values(row || {}).some(value => String(value ?? "").toLowerCase().includes(needle))
    );
    mount("#explore-result", table({ rows: filtered }, row => {
      const entityId = rowEntityId(row);
      if (entityId) openEntity(entityId);
      else showRaw("#explore-result", row);
    }));
    setStatus(
      "#explore-status",
      filtered.length
        ? `${filtered.length} matching ${state.facet} rows`
        : `No witnessed ${state.facet} row matches “${query}”. No identity was invented.`,
    );
  }

  async function runGraphQuery() {
    const collection = $("#graph-collection").value;
    const limit = Number($("#graph-limit").value || 25);
    const id = $("#graph-id").value.trim() || null;
    setStatus("#graph-status", "Executing canonical query…", true);
    try {
      const value = await api("/api/v1/query", {
        method: "POST",
        body: JSON.stringify({ collection, limit, ...(id ? { id } : {}) }),
      });
      mount("#graph-result", table(value, row => {
        const entityId = rowEntityId(row);
        if (entityId) {
          showWorkspace("explore");
          openEntity(entityId);
        } else {
          showRaw("#graph-result", row);
        }
      }));
      setStatus("#graph-status", value.schema || "Loaded");
    } catch (error) {
      setStatus("#graph-status", error.message);
      toast(error.message, "error");
    }
  }

  function populateSourceSelection(catalog) {
    const select = $("#source-selection");
    const previous = select.value;
    select.replaceChildren(new Option("Select a configured source", ""));
    for (const row of catalog.rows || []) {
      const option = new Option(
        `${row.source_id} — ${row.state}${row.preflight_available === true ? " — ready" : ""}`,
        row.source_id,
      );
      option.disabled = row.preflight_available !== true;
      select.add(option);
    }
    if ([...select.options].some(option => option.value === previous && !option.disabled)) {
      select.value = previous;
    }
  }

  async function refreshAdmittedSources() {
    try {
      const value = await api("/api/v1/sources?limit=100");
      mount("#sources-result", table(value, row => showRaw("#sources-result", row)));
    } catch (error) {
      mount("#sources-result", `<div class="error-panel">${escapeHtml(error.message)}</div>`);
    }
  }

  async function refreshSources() {
    setStatus("#sources-status", "Loading configured source boundary…", true);
    try {
      const catalog = await api("/api/v1/source-catalog");
      mount("#source-catalog", table(catalog, row => showRaw("#source-catalog", row)));
      populateSourceSelection(catalog);
      setStatus("#sources-status", `${catalog.row_count} configured source obligations`);
    } catch (error) {
      setStatus("#sources-status", error.message);
    }
    await refreshAdmittedSources();
    if (state.sourceJobId) pollSourceJob();
  }

  async function preflightSource() {
    const sourceId = $("#source-selection").value;
    state.sourcePlan = null;
    $("#source-submit").disabled = true;
    $("#source-plan-result").hidden = true;
    if (!sourceId) {
      setStatus("#source-admit-status", "Select a ready source.");
      return;
    }
    setStatus("#source-admit-status", "Binding exact artifact graph…", true);
    try {
      const plan = await api("/api/v1/sources/preflight", {
        method: "POST",
        body: JSON.stringify({ source_id: sourceId }),
      });
      state.sourcePlan = plan;
      $("#source-plan-result").textContent = JSON.stringify(plan, null, 2);
      $("#source-plan-result").hidden = false;
      $("#source-submit").disabled = false;
      setStatus("#source-admit-status", `Reviewed ${plan.plan_id.slice(0, 12)}…`);
    } catch (error) {
      setStatus("#source-admit-status", error.message);
      toast(error.message, "error");
    }
  }

  function sourceKey(planId) {
    const key = `laplace.sourceIdempotency.${planId}`;
    let value = localStorage.getItem(key);
    if (!value) {
      value = randomToken("browser-ingest");
      localStorage.setItem(key, value);
    }
    return value;
  }

  function renderSourceJob(job) {
    $("#source-job-result").textContent = JSON.stringify(job, null, 2);
    $("#source-job-result").hidden = false;
    $("#source-job-status").textContent = `job=${job.job_id} state=${job.state || "unknown"}${job.result_sha256 ? ` result=${job.result_sha256.slice(0, 12)}…` : ""}`;
  }

  async function pollSourceJob() {
    if (!state.sourceJobId) return;
    try {
      const job = await api(`/api/v1/source-jobs/${encodeURIComponent(state.sourceJobId)}`);
      renderSourceJob(job);
      if (["queued", "running", "interrupted"].includes(job.state)) {
        clearTimeout(state.sourcePoll);
        state.sourcePoll = setTimeout(pollSourceJob, 2000);
      } else if (job.state === "succeeded") {
        await refreshAdmittedSources();
      }
    } catch (error) {
      $("#source-job-status").textContent = error.message;
    }
  }

  async function submitSource() {
    if (!state.sourcePlan) return;
    const plan = state.sourcePlan;
    setStatus("#source-admit-status", "Persisting durable ingestion job…", true);
    try {
      const job = await api("/api/v1/sources/admit", {
        method: "POST",
        body: JSON.stringify({
          source_id: plan.source_id,
          plan_id: plan.plan_id,
          idempotency_key: sourceKey(plan.plan_id),
        }),
      });
      state.sourceJobId = job.job_id;
      localStorage.setItem("laplace.sourceJobId", job.job_id);
      renderSourceJob(job);
      setStatus("#source-admit-status", `Durable job ${job.job_id.slice(0, 12)}… submitted.`);
      state.sourcePoll = setTimeout(pollSourceJob, 2000);
    } catch (error) {
      $("#source-submit").disabled = false;
      setStatus("#source-admit-status", error.message);
      toast(error.message, "error");
    }
  }

  function chatMessage(role, content, metadata = null) {
    const article = document.createElement("article");
    article.className = `message ${role}`;
    article.innerHTML = `<div class="message-role">${role === "user" ? "You" : "Laplace"}</div><div class="message-content"></div>${metadata ? `<details><summary>Receipt</summary><pre class="json-view compact">${escapeHtml(JSON.stringify(metadata, null, 2))}</pre></details>` : ""}`;
    $(".message-content", article).textContent = content;
    $("#conversation").append(article);
    article.scrollIntoView({ behavior: "smooth", block: "end" });
  }

  async function executeCognition(event) {
    event.preventDefault();
    const prompt = $("#cognition-prompt").value;
    if (!prompt.trim()) return;
    chatMessage("user", prompt);
    $("#cognition-prompt").value = "";
    setStatus("#cognition-status", "Executing native cognition…", true);
    try {
      const value = await api("/api/v1/cognition", {
        method: "POST",
        body: JSON.stringify({ prompt, session: state.session }),
      });
      chatMessage(
        "assistant",
        value.output_utf8 ?? (value.output_hex ? `[binary ${value.output_hex.length / 2} bytes]` : ""),
        value,
      );
      setStatus("#cognition-status", `status=${value.status} ordinal=${value.turn_ordinal ?? "?"}`);
    } catch (error) {
      setStatus("#cognition-status", error.message);
      toast(error.message, "error");
    }
  }

  function newSession() {
    state.session = randomToken("web");
    localStorage.setItem("laplace.session", state.session);
    $("#session-name").textContent = state.session;
    $("#conversation").replaceChildren();
  }

  async function runSql() {
    setStatus("#sql-status", "Executing read-only SQL…", true);
    try {
      const value = await api("/api/v1/sql", {
        method: "POST",
        body: JSON.stringify({ sql: $("#sql-editor").value }),
      });
      const data = value.rows.map(row =>
        Object.fromEntries(value.columns.map((column, index) => [column, row[index]]))
      );
      mount("#sql-result", table({ rows: data }, row => showRaw("#sql-result", row)));
      setStatus("#sql-status", `${value.row_count} rows`);
    } catch (error) {
      setStatus("#sql-status", error.message);
      toast(error.message, "error");
    }
  }

  function startEvents() {
    if (state.events) return;
    if (state.token) {
      setStatus("#event-connection", "EventSource cannot attach bearer headers in remote mode.");
      return;
    }
    const source = new EventSource("/api/v1/stream");
    state.events = source;
    $("#events-toggle").textContent = "Stop";
    source.addEventListener("product-snapshot", event => {
      setStatus("#event-connection", "Live");
      const value = JSON.parse(event.data);
      const article = document.createElement("article");
      article.className = "event-item";
      article.innerHTML = `<div class="event-meta"><span>${escapeHtml(value.kind)}</span><time>${new Date((value.timestamp || 0) * 1000).toLocaleTimeString()}</time></div><pre class="json-view compact">${escapeHtml(JSON.stringify(value, null, 2))}</pre>`;
      $("#event-feed").prepend(article);
    });
  }

  function stopEvents() {
    state.events?.close();
    state.events = null;
    $("#events-toggle").textContent = "Start";
    setStatus("#event-connection", "Stopped");
  }

  async function refreshAdmin() {
    const selectors = ["#admin-health", "#admin-summary", "#admin-descriptors"];
    const results = await Promise.allSettled([
      api("/api/v1/health"),
      api("/api/v1/summary"),
      api("/api/v1/descriptors"),
    ]);
    results.forEach((result, index) => {
      mount(
        selectors[index],
        result.status === "fulfilled"
          ? `<pre class="json-view compact">${escapeHtml(JSON.stringify(result.value, null, 2))}</pre>`
          : `<div class="error-panel">${escapeHtml(result.reason.message)}</div>`,
      );
    });
  }

  function configureAuth() {
    const value = prompt(
      state.token ? "Token is set. Enter replacement or leave blank to clear:" : "Enter bearer token or leave blank:",
      "",
    );
    if (value === null) return;
    state.token = value.trim();
    if (state.token) sessionStorage.setItem("laplace.apiToken", state.token);
    else sessionStorage.removeItem("laplace.apiToken");
    refreshHealth().catch(error => toast(error.message, "error"));
  }

  function bind() {
    $$(".nav-item").forEach(button => button.addEventListener("click", () => showWorkspace(button.dataset.workspace)));
    $$(".facet-button").forEach(button => button.addEventListener("click", () => loadFacet(button.dataset.facet)));
    $("#explore-refresh").addEventListener("click", refreshExplore);
    $("#explore-search-form").addEventListener("submit", filterExplore);
    $("#auth-button").addEventListener("click", configureAuth);
    $("#graph-refresh").addEventListener("click", runGraphQuery);
    $("#graph-collection").addEventListener("change", () => {
      const collection = $("#graph-collection").value;
      $("#graph-id-label").hidden = collection !== "entity";
      $("#graph-limit").disabled = collection === "entity" || collection === "summary";
    });
    $("#sources-refresh").addEventListener("click", refreshSources);
    $("#source-preflight").addEventListener("click", preflightSource);
    $("#source-submit").addEventListener("click", submitSource);
    $("#source-selection").addEventListener("change", () => {
      state.sourcePlan = null;
      $("#source-submit").disabled = true;
      $("#source-plan-result").hidden = true;
    });
    $("#cognition-form").addEventListener("submit", executeCognition);
    $("#new-session").addEventListener("click", newSession);
    $("#session-name").textContent = state.session;
    $("#sql-run").addEventListener("click", runSql);
    $("#events-toggle").addEventListener("click", () => state.events ? stopEvents() : startEvents());
    $("#events-clear").addEventListener("click", () => $("#event-feed").replaceChildren());
    $("#admin-refresh").addEventListener("click", refreshAdmin);
  }

  async function boot() {
    bind();
    $$(".facet-button").forEach(button => button.classList.toggle("active", button.dataset.facet === state.facet));
    try {
      await refreshHealth();
      await refreshExplore();
    } catch (error) {
      toast(error.message, "error");
    }
  }

  boot();
})();
