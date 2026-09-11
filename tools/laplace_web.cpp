#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <libpq-fe.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::size_t kMaximumRequestBytes = 32768U;
constexpr int kMaximumConcurrentRequests = 8;
constexpr int kDefaultPort = 8080;
constexpr int kMaximumPageSize = 200;

struct Config {
    std::string listen_address = "0.0.0.0";
    int port = kDefaultPort;
    std::string socket_directory = "/opt/laplace/runtime/postgresql/refactor";
    std::string database = "laplace_refactor";
    std::string role = "laplace_admin";
};

struct Column {
    std::string name;
    std::string type;
    bool nullable = false;
    bool primary = false;
};

struct Collection {
    std::string name;
    long long estimated_rows = 0;
    std::vector<Column> columns;
};

struct PgConnectionDeleter {
    void operator()(PGconn* connection) const noexcept {
        if (connection != nullptr) {
            PQfinish(connection);
        }
    }
};
using PgConnection = std::unique_ptr<PGconn, PgConnectionDeleter>;

struct PgResultDeleter {
    void operator()(PGresult* result) const noexcept {
        if (result != nullptr) {
            PQclear(result);
        }
    }
};
using PgResult = std::unique_ptr<PGresult, PgResultDeleter>;

std::atomic<bool> g_running{true};
std::atomic<int> g_active_requests{0};

void signal_handler(int) {
    g_running.store(false);
}

std::string json_escape(std::string_view value) {
    std::string output;
    output.reserve(value.size() + 16U);
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (ch < 0x20U) {
                    constexpr char hex[] = "0123456789abcdef";
                    output += "\\u00";
                    output.push_back(hex[(ch >> 4U) & 0x0fU]);
                    output.push_back(hex[ch & 0x0fU]);
                } else {
                    output.push_back(static_cast<char>(ch));
                }
        }
    }
    return output;
}

std::string error_json(std::string_view message) {
    return "{\"error\":\"" + json_escape(message) + "\"}";
}

bool send_all(int descriptor, std::string_view bytes) {
    std::size_t sent = 0U;
    while (sent < bytes.size()) {
        const ssize_t count = ::send(
            descriptor,
            bytes.data() + static_cast<std::ptrdiff_t>(sent),
            bytes.size() - sent,
            MSG_NOSIGNAL);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (count == 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

void respond(
    int descriptor,
    int status,
    std::string_view reason,
    std::string_view content_type,
    std::string_view body) {
    std::ostringstream header;
    header << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
           << "Content-Type: " << content_type << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Cache-Control: no-store\r\n"
           << "X-Content-Type-Options: nosniff\r\n"
           << "Connection: close\r\n\r\n";
    const std::string header_text = header.str();
    (void)send_all(descriptor, header_text);
    (void)send_all(descriptor, body);
}

std::optional<int> parse_int(std::string_view value, int minimum, int maximum) {
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    if (parsed < minimum || parsed > maximum) {
        return std::nullopt;
    }
    return parsed;
}

std::string url_decode(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '+') {
            output.push_back(' ');
            continue;
        }
        if (ch == '%' && index + 2U < value.size()) {
            const auto digit = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int high = digit(value[index + 1U]);
            const int low = digit(value[index + 2U]);
            if (high >= 0 && low >= 0) {
                output.push_back(static_cast<char>((high << 4) | low));
                index += 2U;
                continue;
            }
        }
        output.push_back(ch);
    }
    return output;
}

std::map<std::string, std::string> parse_query(std::string_view query) {
    std::map<std::string, std::string> output;
    std::size_t start = 0U;
    while (start <= query.size()) {
        const std::size_t ampersand = query.find('&', start);
        const std::size_t end = ampersand == std::string_view::npos ? query.size() : ampersand;
        const std::string_view item = query.substr(start, end - start);
        const std::size_t equals = item.find('=');
        const std::string key = url_decode(item.substr(0U, equals));
        const std::string value = equals == std::string_view::npos
            ? std::string{}
            : url_decode(item.substr(equals + 1U));
        if (!key.empty()) {
            output[key] = value;
        }
        if (ampersand == std::string_view::npos) {
            break;
        }
        start = ampersand + 1U;
    }
    return output;
}

PgConnection connect_database(const Config& config, std::string& error) {
    const std::string port = std::to_string(config.port == kDefaultPort ? 55433 : 55433);
    const char* keywords[] = {
        "host", "port", "dbname", "user", "application_name", "connect_timeout", nullptr
    };
    const char* values[] = {
        config.socket_directory.c_str(),
        port.c_str(),
        config.database.c_str(),
        config.role.c_str(),
        "laplace-web",
        "3",
        nullptr
    };
    PgConnection connection(PQconnectdbParams(keywords, values, 0));
    if (!connection || PQstatus(connection.get()) != CONNECTION_OK) {
        error = connection ? PQerrorMessage(connection.get()) : "libpq allocation failed";
        return PgConnection{};
    }
    PGresult* raw = PQexec(connection.get(),
        "SET statement_timeout='5s'; SET lock_timeout='1s'; SET idle_in_transaction_session_timeout='5s';");
    PgResult setup(raw);
    if (!setup || PQresultStatus(setup.get()) != PGRES_COMMAND_OK) {
        error = setup ? PQresultErrorMessage(setup.get()) : "session setup failed";
        return PgConnection{};
    }
    return connection;
}

std::optional<std::string> query_single_json(
    PGconn* connection,
    const std::string& sql,
    const std::vector<std::string>& parameters,
    std::string& error) {
    std::vector<const char*> values;
    values.reserve(parameters.size());
    for (const std::string& parameter : parameters) {
        values.push_back(parameter.c_str());
    }
    PgResult result(PQexecParams(
        connection,
        sql.c_str(),
        static_cast<int>(values.size()),
        nullptr,
        values.empty() ? nullptr : values.data(),
        nullptr,
        nullptr,
        0));
    if (!result || PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
        error = result ? PQresultErrorMessage(result.get()) : "query allocation failed";
        return std::nullopt;
    }
    if (PQntuples(result.get()) != 1 || PQnfields(result.get()) != 1 || PQgetisnull(result.get(), 0, 0) != 0) {
        error = "query did not return one JSON value";
        return std::nullopt;
    }
    return std::string(PQgetvalue(result.get(), 0, 0));
}

std::optional<Collection> load_collection(PGconn* connection, std::string_view name, std::string& error) {
    static const char sql[] = R"SQL(
SELECT c.relname,
       GREATEST(c.reltuples, 0)::bigint,
       a.attname,
       pg_catalog.format_type(a.atttypid, a.atttypmod),
       NOT a.attnotnull,
       EXISTS (
           SELECT 1
           FROM pg_catalog.pg_index i
           WHERE i.indrelid = c.oid
             AND i.indisprimary
             AND a.attnum = ANY(i.indkey)
       )
FROM pg_catalog.pg_class c
JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
JOIN pg_catalog.pg_attribute a ON a.attrelid = c.oid
WHERE n.nspname = 'laplace'
  AND c.relkind IN ('r','p')
  AND c.relname = $1
  AND a.attnum > 0
  AND NOT a.attisdropped
ORDER BY a.attnum
)SQL";
    const std::string name_text(name);
    const char* values[] = {name_text.c_str()};
    PgResult result(PQexecParams(connection, sql, 1, nullptr, values, nullptr, nullptr, 0));
    if (!result || PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
        error = result ? PQresultErrorMessage(result.get()) : "collection discovery failed";
        return std::nullopt;
    }
    if (PQntuples(result.get()) == 0) {
        error = "collection does not exist";
        return std::nullopt;
    }
    Collection collection;
    collection.name = PQgetvalue(result.get(), 0, 0);
    const std::string estimate = PQgetvalue(result.get(), 0, 1);
    const auto estimate_result = std::from_chars(
        estimate.data(), estimate.data() + estimate.size(), collection.estimated_rows);
    if (estimate_result.ec != std::errc{}) {
        collection.estimated_rows = 0;
    }
    const int rows = PQntuples(result.get());
    collection.columns.reserve(static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        collection.columns.push_back(Column{
            PQgetvalue(result.get(), row, 2),
            PQgetvalue(result.get(), row, 3),
            std::string_view(PQgetvalue(result.get(), row, 4)) == "t",
            std::string_view(PQgetvalue(result.get(), row, 5)) == "t"
        });
    }
    return collection;
}

bool has_column(const Collection& collection, std::string_view name) {
    return std::any_of(collection.columns.begin(), collection.columns.end(),
        [name](const Column& column) { return column.name == name; });
}

std::string quote_identifier(std::string_view identifier) {
    std::string output = "\"";
    for (const char ch : identifier) {
        if (ch == '"') {
            output += "\"\"";
        } else {
            output.push_back(ch);
        }
    }
    output.push_back('"');
    return output;
}

std::string collections_json(PGconn* connection, std::string& error) {
    static const char sql[] = R"SQL(
WITH collection AS (
    SELECT c.oid,
           c.relname AS name,
           GREATEST(c.reltuples, 0)::bigint AS estimated_rows
    FROM pg_catalog.pg_class c
    JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
    WHERE n.nspname = 'laplace'
      AND c.relkind IN ('r','p')
), descriptor AS (
    SELECT collection.name,
           collection.estimated_rows,
           jsonb_agg(
               jsonb_build_object(
                   'name', a.attname,
                   'type', pg_catalog.format_type(a.atttypid, a.atttypmod),
                   'nullable', NOT a.attnotnull,
                   'primary', EXISTS (
                       SELECT 1
                       FROM pg_catalog.pg_index i
                       WHERE i.indrelid = collection.oid
                         AND i.indisprimary
                         AND a.attnum = ANY(i.indkey)
                   )
               ) ORDER BY a.attnum
           ) AS columns
    FROM collection
    JOIN pg_catalog.pg_attribute a ON a.attrelid = collection.oid
    WHERE a.attnum > 0 AND NOT a.attisdropped
    GROUP BY collection.oid, collection.name, collection.estimated_rows
)
SELECT jsonb_build_object(
    'schema', 'laplace',
    'collections', COALESCE(
        jsonb_agg(
            jsonb_build_object(
                'name', name,
                'estimated_rows', estimated_rows,
                'columns', columns
            ) ORDER BY name
        ),
        '[]'::jsonb
    )
)::text
FROM descriptor
)SQL";
    auto value = query_single_json(connection, sql, {}, error);
    return value.value_or(error_json(error));
}

std::string collection_rows_json(
    PGconn* connection,
    const Collection& collection,
    const std::map<std::string, std::string>& query,
    std::string& error) {
    int limit = 50;
    int offset = 0;
    if (const auto it = query.find("limit"); it != query.end()) {
        const auto parsed = parse_int(it->second, 1, kMaximumPageSize);
        if (!parsed) {
            error = "limit must be between 1 and 200";
            return error_json(error);
        }
        limit = *parsed;
    }
    if (const auto it = query.find("offset"); it != query.end()) {
        const auto parsed = parse_int(it->second, 0, 1000000000);
        if (!parsed) {
            error = "offset is invalid";
            return error_json(error);
        }
        offset = *parsed;
    }

    std::string sort = collection.columns.front().name;
    for (const Column& column : collection.columns) {
        if (column.primary) {
            sort = column.name;
            break;
        }
    }
    if (const auto it = query.find("sort"); it != query.end() && !it->second.empty()) {
        if (!has_column(collection, it->second)) {
            error = "sort column is not in the selected collection";
            return error_json(error);
        }
        sort = it->second;
    }
    const bool descending = query.contains("direction") && query.at("direction") == "desc";

    std::optional<std::string> filter_column;
    std::optional<std::string> filter_value;
    if (const auto column = query.find("filter_column"); column != query.end() && !column->second.empty()) {
        if (!has_column(collection, column->second)) {
            error = "filter column is not in the selected collection";
            return error_json(error);
        }
        const auto value = query.find("filter");
        if (value != query.end() && !value->second.empty()) {
            filter_column = column->second;
            filter_value = value->second;
        }
    }

    const std::string table = quote_identifier(collection.name);
    const std::string sort_identifier = quote_identifier(sort);
    std::ostringstream sql;
    sql << "SELECT jsonb_build_object("
        << "'collection', '" << json_escape(collection.name) << "',"
        << "'offset', " << offset << ","
        << "'limit', " << limit << ","
        << "'sort', '" << json_escape(sort) << "',"
        << "'direction', '" << (descending ? "desc" : "asc") << "',"
        << "'rows', COALESCE(jsonb_agg(to_jsonb(page)), '[]'::jsonb))::text "
        << "FROM (SELECT * FROM laplace." << table;
    std::vector<std::string> parameters;
    if (filter_column && filter_value) {
        sql << " WHERE " << quote_identifier(*filter_column) << "::text ILIKE $1";
        parameters.push_back("%" + *filter_value + "%");
    }
    sql << " ORDER BY " << sort_identifier << (descending ? " DESC" : " ASC")
        << " LIMIT " << limit << " OFFSET " << offset << ") AS page";

    auto value = query_single_json(connection, sql.str(), parameters, error);
    return value.value_or(error_json(error));
}

std::string health_json(PGconn* connection, std::string& error) {
    static const char sql[] = R"SQL(
SELECT jsonb_build_object(
    'status', 'ready',
    'database', current_database(),
    'role', current_user,
    'extension_version', (SELECT extversion FROM pg_catalog.pg_extension WHERE extname='laplace'),
    'entity_estimate', GREATEST((SELECT reltuples FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE n.nspname='laplace' AND c.relname='entity'),0)::bigint,
    'physicality_estimate', GREATEST((SELECT reltuples FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE n.nspname='laplace' AND c.relname='physicality'),0)::bigint
)::text
)SQL";
    auto value = query_single_json(connection, sql, {}, error);
    return value.value_or(error_json(error));
}

constexpr std::string_view kIndexHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Laplace Explore</title>
<style>
:root{font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;color:#e8eef7;background:#07101d;--panel:#0d1929;--panel2:#111f31;--line:#26384f;--muted:#8ea3ba;--accent:#69a7ff;--ok:#6bd19c}*{box-sizing:border-box}body{margin:0;height:100vh;overflow:hidden}.app{display:grid;grid-template-rows:52px 1fr;height:100vh}.top{display:flex;align-items:center;gap:18px;padding:0 18px;border-bottom:1px solid var(--line);background:#091422}.brand{font-weight:750;letter-spacing:.08em}.status{font-size:12px;color:var(--muted)}.status strong{color:var(--ok)}.layout{display:grid;grid-template-columns:260px minmax(0,1fr) 330px;min-height:0}.sidebar,.inspector{background:var(--panel);min-height:0;overflow:auto}.sidebar{border-right:1px solid var(--line);padding:12px}.inspector{border-left:1px solid var(--line);padding:14px}.workspace{display:grid;grid-template-rows:auto auto 1fr;min-width:0;min-height:0;background:#08121f}.search,input,select,button{background:#0a1726;border:1px solid var(--line);color:#e8eef7;border-radius:6px;padding:7px 9px}.search{width:100%;margin-bottom:10px}.collection{display:flex;justify-content:space-between;gap:8px;width:100%;text-align:left;border:0;background:transparent;color:#cdd9e7;padding:7px 8px;cursor:pointer;border-radius:5px}.collection:hover,.collection.active{background:#17283c;color:white}.count{color:var(--muted);font-variant-numeric:tabular-nums}.heading{padding:14px 16px 8px;display:flex;align-items:flex-end;justify-content:space-between;gap:12px}.heading h1{font-size:20px;margin:0}.heading small{color:var(--muted)}.controls{display:flex;flex-wrap:wrap;gap:8px;padding:8px 16px 12px;border-bottom:1px solid var(--line)}button{cursor:pointer}button:hover{border-color:#5279a8}.tabs{display:flex;gap:4px;margin-left:auto}.tab.active{background:#1b3b63;border-color:#3b70ac}.content{min-height:0;overflow:auto}.tablewrap{min-width:100%;overflow:auto}table{border-collapse:collapse;width:max-content;min-width:100%;font-size:12px}th{position:sticky;top:0;background:#0d1929;z-index:1;color:#b9c9da;text-align:left}th,td{padding:7px 9px;border-bottom:1px solid #17273a;max-width:360px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}tr:hover td{background:#0e1d2e}.hex{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;color:#9fc4ff}.muted{color:var(--muted)}dl{display:grid;grid-template-columns:110px minmax(0,1fr);gap:7px 10px;font-size:12px;margin:0}dt{color:var(--muted)}dd{margin:0;overflow-wrap:anywhere}.selected{margin:0 0 14px;padding:10px;background:var(--panel2);border:1px solid var(--line);border-radius:7px}.selected strong{display:block;margin-bottom:5px}.graph,.geometry{width:100%;height:100%;min-height:560px}.empty{padding:34px;color:var(--muted)}pre{margin:0;padding:16px;white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px;color:#c6d4e3}.pill{display:inline-block;padding:2px 6px;border-radius:12px;background:#172a40;color:#a9c7ea;font-size:11px}.legend{display:flex;gap:12px;color:var(--muted);font-size:11px;padding:8px 16px}.dot{width:8px;height:8px;border-radius:50%;display:inline-block;background:var(--accent);margin-right:4px}@media(max-width:1000px){.layout{grid-template-columns:220px minmax(0,1fr)}.inspector{display:none}}
</style>
</head>
<body>
<div class="app">
  <header class="top"><div class="brand">LAPLACE</div><div>Explore / Collections</div><div id="health" class="status">Connecting to active product…</div></header>
  <div class="layout">
    <aside class="sidebar"><input id="collectionSearch" class="search" placeholder="Filter collections"><div id="collections"></div></aside>
    <main class="workspace">
      <div class="heading"><div><h1 id="title">Collections</h1><small id="subtitle">Select a persisted Laplace collection</small></div><span id="selectionCount" class="pill">0 selected</span></div>
      <div class="controls">
        <select id="filterColumn"></select><input id="filter" placeholder="Filter value"><select id="sort"></select><select id="direction"><option value="asc">ascending</option><option value="desc">descending</option></select><select id="limit"><option>25</option><option selected>50</option><option>100</option><option>200</option></select><button id="apply">Apply</button><button id="prev">Previous</button><button id="next">Next</button>
        <div class="tabs"><button class="tab active" data-view="table">Table</button><button class="tab" data-view="graph">Graph</button><button class="tab" data-view="geometry">Geometry</button><button class="tab" data-view="json">JSON</button></div>
      </div>
      <section id="content" class="content"><div class="empty">This surface reads the installed <code>laplace</code> schema directly. Choose a collection to browse persisted entities, physicalities, evidence, standing, admissions, receipts, and other installed state.</div></section>
    </main>
    <aside class="inspector"><div class="selected"><strong>Persistent selection</strong><span id="selectedSummary" class="muted">No rows selected</span><br><button id="clearSelection" style="margin-top:8px">Clear</button></div><div id="inspector"><span class="muted">Focus a row to inspect exact values.</span></div></aside>
  </div>
</div>
<script>
const state={collections:[],current:null,rows:[],offset:0,view:'table',focus:null,selected:new Map(JSON.parse(localStorage.getItem('laplace-selection')||'[]'))};
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function rowKey(row){const c=state.current?.columns.find(x=>x.primary)||state.current?.columns.find(x=>x.name.endsWith('_id'))||state.current?.columns[0];return state.current.name+':'+String(row[c?.name]??JSON.stringify(row));}
function saveSelection(){localStorage.setItem('laplace-selection',JSON.stringify([...state.selected]));$('selectionCount').textContent=`${state.selected.size} selected`;$('selectedSummary').textContent=state.selected.size?[...state.selected.keys()].slice(0,4).join('\n')+(state.selected.size>4?` +${state.selected.size-4} more`:''):'No rows selected';}
async function getJson(url){const r=await fetch(url,{cache:'no-store'});const j=await r.json();if(!r.ok)throw new Error(j.error||`${r.status}`);return j;}
function format(v){if(v===null)return '<span class="muted">null</span>';if(typeof v==='object')return `<span class="hex">${esc(JSON.stringify(v))}</span>`;const s=String(v);const hex=/^(\\x)?[0-9a-f]{24,}$/i.test(s);return `<span class="${hex?'hex':''}" title="${esc(s)}">${esc(s)}</span>`;}
function inspect(row){state.focus=row;if(!row){$('inspector').innerHTML='<span class="muted">Focus a row to inspect exact values.</span>';return;}$('inspector').innerHTML='<dl>'+Object.entries(row).map(([k,v])=>`<dt>${esc(k)}</dt><dd>${format(v)}</dd>`).join('')+'</dl>';}
function fillControls(){const cols=state.current?.columns||[];$('filterColumn').innerHTML='<option value="">filter column</option>'+cols.map(c=>`<option value="${esc(c.name)}">${esc(c.name)}</option>`).join('');$('sort').innerHTML=cols.map(c=>`<option value="${esc(c.name)}" ${c.primary?'selected':''}>sort: ${esc(c.name)}</option>`).join('');}
function renderCollections(){const needle=$('collectionSearch').value.toLowerCase();$('collections').innerHTML=state.collections.filter(c=>c.name.includes(needle)).map(c=>`<button class="collection ${state.current?.name===c.name?'active':''}" data-name="${esc(c.name)}"><span>${esc(c.name)}</span><span class="count">~${Number(c.estimated_rows||0).toLocaleString()}</span></button>`).join('');document.querySelectorAll('.collection').forEach(b=>b.onclick=()=>selectCollection(b.dataset.name));}
async function selectCollection(name){state.current=state.collections.find(c=>c.name===name);state.offset=0;state.focus=null;fillControls();renderCollections();await loadRows();}
async function loadRows(){if(!state.current)return;const q=new URLSearchParams({offset:String(state.offset),limit:$('limit').value,sort:$('sort').value,direction:$('direction').value});if($('filterColumn').value&&$('filter').value){q.set('filter_column',$('filterColumn').value);q.set('filter',$('filter').value);}try{const data=await getJson(`/api/collections/${encodeURIComponent(state.current.name)}?${q}`);state.rows=data.rows||[];$('title').textContent=state.current.name;$('subtitle').textContent=`~${Number(state.current.estimated_rows||0).toLocaleString()} rows · ${state.current.columns.length} fields · offset ${state.offset}`;render();}catch(e){$('content').innerHTML=`<div class="empty">${esc(e.message)}</div>`;}}
function renderTable(){if(!state.rows.length)return '<div class="empty">No rows in this page/filter.</div>';const cols=state.current.columns;return `<div class="tablewrap"><table><thead><tr><th></th>${cols.map(c=>`<th>${esc(c.name)}<br><span class="muted">${esc(c.type)}</span></th>`).join('')}</tr></thead><tbody>${state.rows.map((r,i)=>{const key=rowKey(r);return `<tr data-row="${i}"><td><input type="checkbox" data-select="${i}" ${state.selected.has(key)?'checked':''}></td>${cols.map(c=>`<td>${format(r[c.name])}</td>`).join('')}</tr>`}).join('')}</tbody></table></div>`;}
function idFields(row){return Object.entries(row).filter(([k,v])=>k.endsWith('_id')&&typeof v==='string'&&/^(\\x)?[0-9a-f]{24,}$/i.test(v));}
function renderGraph(){const nodes=new Map(),edges=[];state.rows.forEach((r,ri)=>{const ids=idFields(r);if(!ids.length)return;const [pkName,pk]=ids[0];nodes.set(pk,{id:pk,label:`${ri+1}: ${pkName}`,row:r});ids.slice(1).forEach(([name,id])=>{nodes.set(id,nodes.get(id)||{id,label:name,row:null});edges.push({a:pk,b:id,label:name});});});if(!nodes.size)return '<div class="empty">This page has no ID-shaped fields to project as a graph.</div>';const list=[...nodes.values()],w=1100,h=650,cx=w/2,cy=h/2,r=Math.min(w,h)*.37;const pos=new Map(list.map((n,i)=>[n.id,{x:cx+Math.cos(i/list.length*Math.PI*2)*r,y:cy+Math.sin(i/list.length*Math.PI*2)*r}]));return `<svg class="graph" viewBox="0 0 ${w} ${h}">${edges.map(e=>{const a=pos.get(e.a),b=pos.get(e.b);return `<g><line x1="${a.x}" y1="${a.y}" x2="${b.x}" y2="${b.y}" stroke="#35506f"/><text x="${(a.x+b.x)/2}" y="${(a.y+b.y)/2}" fill="#7893ad" font-size="9">${esc(e.label)}</text></g>`}).join('')}${list.map(n=>{const p=pos.get(n.id);return `<g data-node="${esc(n.id)}"><circle cx="${p.x}" cy="${p.y}" r="9" fill="#69a7ff"/><text x="${p.x+13}" y="${p.y+4}" fill="#dce9f7" font-size="10">${esc(n.label)}</text></g>`}).join('')}</svg>`;}
function renderGeometry(){const points=state.rows.filter(r=>['centroid_x','centroid_y','centroid_z','centroid_m'].every(k=>typeof r[k]==='number'));if(!points.length)return '<div class="empty">This page has no four-component physicality coordinates.</div>';const w=1100,h=650,pad=50,scale=Math.min(w,h)/2-pad;return `<div class="legend"><span><i class="dot"></i>x/y projection</span><span>radius controls point size; z/m remain in the inspector</span></div><svg class="geometry" viewBox="0 0 ${w} ${h}"><line x1="${w/2}" y1="20" x2="${w/2}" y2="${h-20}" stroke="#233950"/><line x1="20" y1="${h/2}" x2="${w-20}" y2="${h/2}" stroke="#233950"/>${points.map((p,i)=>{const x=w/2+p.centroid_x*scale,y=h/2-p.centroid_y*scale,rr=Math.max(4,Math.min(22,4+Number(p.radius||0)*18));return `<circle data-geo="${state.rows.indexOf(p)}" cx="${x}" cy="${y}" r="${rr}" fill="#69a7ff" fill-opacity=".68"><title>${esc(`x=${p.centroid_x} y=${p.centroid_y} z=${p.centroid_z} m=${p.centroid_m}`)}</title></circle>`}).join('')}</svg>`;}
function render(){document.querySelectorAll('.tab').forEach(b=>b.classList.toggle('active',b.dataset.view===state.view));let html='';if(state.view==='table')html=renderTable();else if(state.view==='graph')html=renderGraph();else if(state.view==='geometry')html=renderGeometry();else html=`<pre>${esc(JSON.stringify(state.rows,null,2))}</pre>`;$('content').innerHTML=html;document.querySelectorAll('tr[data-row]').forEach(tr=>tr.onclick=e=>{if(e.target.matches('input'))return;inspect(state.rows[Number(tr.dataset.row)]);});document.querySelectorAll('input[data-select]').forEach(box=>box.onchange=()=>{const row=state.rows[Number(box.dataset.select)],key=rowKey(row);if(box.checked)state.selected.set(key,row);else state.selected.delete(key);saveSelection();});document.querySelectorAll('[data-geo]').forEach(node=>node.onclick=()=>inspect(state.rows[Number(node.dataset.geo)]));saveSelection();}
$('collectionSearch').oninput=renderCollections;$('apply').onclick=()=>{state.offset=0;loadRows();};$('prev').onclick=()=>{state.offset=Math.max(0,state.offset-Number($('limit').value));loadRows();};$('next').onclick=()=>{state.offset+=Number($('limit').value);loadRows();};$('clearSelection').onclick=()=>{state.selected.clear();saveSelection();render();};document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>{state.view=b.dataset.view;render();});
(async()=>{saveSelection();try{const [health,catalog]=await Promise.all([getJson('/api/health'),getJson('/api/collections')]);$('health').innerHTML=`<strong>READY</strong> · ${esc(health.database)} · laplace ${esc(health.extension_version||'?')} · ~${Number(health.entity_estimate||0).toLocaleString()} entities`;state.collections=catalog.collections||[];renderCollections();}catch(e){$('health').textContent=`Unavailable: ${e.message}`;}})();
</script>
</body></html>)HTML";

void handle_request(int descriptor, const Config& config) {
    std::string request;
    request.reserve(4096U);
    char buffer[4096];
    while (request.size() < kMaximumRequestBytes) {
        const ssize_t count = ::recv(descriptor, buffer, sizeof(buffer), 0);
        if (count < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (count == 0) break;
        request.append(buffer, static_cast<std::size_t>(count));
        if (request.find("\r\n\r\n") != std::string::npos) break;
    }
    const std::size_t line_end = request.find("\r\n");
    if (line_end == std::string::npos) {
        respond(descriptor, 400, "Bad Request", "application/json", error_json("malformed request"));
        return;
    }
    const std::string_view line(request.data(), line_end);
    const std::size_t first_space = line.find(' ');
    const std::size_t second_space = first_space == std::string_view::npos
        ? std::string_view::npos
        : line.find(' ', first_space + 1U);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos) {
        respond(descriptor, 400, "Bad Request", "application/json", error_json("malformed request line"));
        return;
    }
    if (line.substr(0U, first_space) != "GET") {
        respond(descriptor, 405, "Method Not Allowed", "application/json", error_json("read-only GET surface"));
        return;
    }
    const std::string target(line.substr(first_space + 1U, second_space - first_space - 1U));
    const std::size_t question = target.find('?');
    const std::string path = url_decode(target.substr(0U, question));
    const auto query = question == std::string::npos
        ? std::map<std::string, std::string>{}
        : parse_query(std::string_view(target).substr(question + 1U));

    if (path == "/" || path == "/index.html") {
        respond(descriptor, 200, "OK", "text/html; charset=utf-8", kIndexHtml);
        return;
    }

    std::string error;
    PgConnection connection = connect_database(config, error);
    if (!connection) {
        respond(descriptor, 503, "Service Unavailable", "application/json", error_json(error));
        return;
    }
    if (path == "/api/health") {
        const std::string body = health_json(connection.get(), error);
        respond(descriptor, error.empty() ? 200 : 503, error.empty() ? "OK" : "Service Unavailable", "application/json", body);
        return;
    }
    if (path == "/api/collections") {
        const std::string body = collections_json(connection.get(), error);
        respond(descriptor, error.empty() ? 200 : 500, error.empty() ? "OK" : "Internal Server Error", "application/json", body);
        return;
    }
    constexpr std::string_view prefix = "/api/collections/";
    if (path.rfind(prefix, 0U) == 0U) {
        const std::string name = path.substr(prefix.size());
        const auto collection = load_collection(connection.get(), name, error);
        if (!collection) {
            respond(descriptor, 404, "Not Found", "application/json", error_json(error));
            return;
        }
        error.clear();
        const std::string body = collection_rows_json(connection.get(), *collection, query, error);
        respond(descriptor, error.empty() ? 200 : 400, error.empty() ? "OK" : "Bad Request", "application/json", body);
        return;
    }
    respond(descriptor, 404, "Not Found", "application/json", error_json("route not found"));
}

Config parse_args(int argc, char** argv) {
    Config config;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        auto require_value = [&](std::string_view option) -> std::string {
            if (index + 1 >= argc) {
                throw std::runtime_error(std::string(option) + " requires a value");
            }
            ++index;
            return argv[index];
        };
        if (argument == "--listen") {
            config.listen_address = require_value(argument);
        } else if (argument == "--port") {
            const std::string raw = require_value(argument);
            const auto parsed = parse_int(raw, 1, 65535);
            if (!parsed) throw std::runtime_error("--port is invalid");
            config.port = *parsed;
        } else if (argument == "--socket") {
            config.socket_directory = require_value(argument);
        } else if (argument == "--database") {
            config.database = require_value(argument);
        } else if (argument == "--role") {
            config.role = require_value(argument);
        } else if (argument == "--help") {
            std::cout << "usage: laplace-web [--listen ADDRESS] [--port PORT] [--socket DIRECTORY] [--database NAME] [--role NAME]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + argument);
        }
    }
    return config;
}

int create_listener(const Config& config) {
    const int descriptor = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (descriptor < 0) {
        throw std::runtime_error(std::string("socket failed: ") + std::strerror(errno));
    }
    const int reuse = 1;
    if (::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        const std::string message = std::string("setsockopt failed: ") + std::strerror(errno);
        ::close(descriptor);
        throw std::runtime_error(message);
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(config.port));
    if (::inet_pton(AF_INET, config.listen_address.c_str(), &address.sin_addr) != 1) {
        ::close(descriptor);
        throw std::runtime_error("--listen must be an IPv4 address");
    }
    if (::bind(descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        const std::string message = std::string("bind failed: ") + std::strerror(errno);
        ::close(descriptor);
        throw std::runtime_error(message);
    }
    if (::listen(descriptor, 64) != 0) {
        const std::string message = std::string("listen failed: ") + std::strerror(errno);
        ::close(descriptor);
        throw std::runtime_error(message);
    }
    return descriptor;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Config config = parse_args(argc, argv);
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        const int listener = create_listener(config);
        std::cerr << "laplace-web listening on " << config.listen_address << ':' << config.port << '\n';
        while (g_running.load()) {
            sockaddr_in peer{};
            socklen_t peer_size = sizeof(peer);
            const int descriptor = ::accept4(
                listener,
                reinterpret_cast<sockaddr*>(&peer),
                &peer_size,
                SOCK_CLOEXEC);
            if (descriptor < 0) {
                if (errno == EINTR) continue;
                if (!g_running.load()) break;
                std::cerr << "accept failed: " << std::strerror(errno) << '\n';
                continue;
            }
            const int before = g_active_requests.fetch_add(1);
            if (before >= kMaximumConcurrentRequests) {
                g_active_requests.fetch_sub(1);
                respond(descriptor, 503, "Service Unavailable", "application/json", error_json("request concurrency limit reached"));
                ::close(descriptor);
                continue;
            }
            std::thread([descriptor, config]() {
                struct ActiveGuard {
                    ~ActiveGuard() { g_active_requests.fetch_sub(1); }
                } guard;
                handle_request(descriptor, config);
                ::shutdown(descriptor, SHUT_RDWR);
                ::close(descriptor);
            }).detach();
        }
        ::close(listener);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "laplace-web: " << error.what() << '\n';
        return 1;
    }
}
