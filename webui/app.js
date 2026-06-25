import { call, eventsUrl } from "/api.js";

const $ = (id) => document.getElementById(id);
const fmt = (ms) => {
  if (!ms || ms < 0) ms = 0;
  const s = Math.floor(ms / 1000);
  return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
};

let durationMs = 0;
let seeking = false;     // user dragging the seek bar
let volTouching = false; // user dragging volume
let balTouching = false; // user dragging balance

// ---- status rendering ----
function setFlag(id, on) { $(id).classList.toggle("on", !!on); }
function applyStatus(s) {
  const t = [s.artist, s.album, s.title].filter(Boolean).join(" — ");
  $("track").textContent = t || "—";
  $("bitrate").textContent = s.bitrate || "—";
  $("srate").textContent = s.sampleRateHz ? Math.round(s.sampleRateHz / 1000) : "—";
  $("chan").textContent = s.channels === 1 ? "mono" : (s.channels === 2 ? "stereo" : "—");
  setFlag("f-eq", s.eq); setFlag("f-pl", s.pl);
  setFlag("f-shuffle", s.shuffle); setFlag("f-repeat", s.repeat);
  $("shuffle").classList.toggle("on", !!s.shuffle);
  $("repeat").classList.toggle("on", !!s.repeat);
  durationMs = s.durationMs || 0;
  $("dur").textContent = fmt(durationMs);
  if (!seeking) {
    $("pos").textContent = fmt(s.positionMs || 0);
    $("seek").value = durationMs ? Math.round((s.positionMs || 0) / durationMs * 1000) : 0;
  }
  if (!volTouching) { $("vol").value = s.volume || 0; $("volv").textContent = s.volume || 0; }
  if (!balTouching) {
    $("bal").value = s.balance || 0;
    $("balv").textContent = s.balance === 0 ? "C" : (s.balance < 0 ? Math.abs(s.balance) + "L" : s.balance + "R");
  }
  $("source").textContent = s.source || "—";
  $("state").textContent = s.state || "stopped";
  window.dispatchEvent(new CustomEvent("linamp-status"));
}
function applyPosition(ms) {
  if (seeking) return;
  $("pos").textContent = fmt(ms);
  $("seek").value = durationMs ? Math.round(ms / durationMs * 1000) : 0;
}

// ---- controls ----
$("prev").onclick  = () => call("/api/previous");
$("play").onclick  = () => call("/api/play");
$("pause").onclick = () => call("/api/pause");
$("stop").onclick  = () => call("/api/stop");
$("next").onclick  = () => call("/api/next");
$("shuffle").onclick = () => call("/api/shuffle");
$("repeat").onclick  = () => call("/api/repeat");

const seek = $("seek");
seek.addEventListener("input", () => { seeking = true;
  if (durationMs) $("pos").textContent = fmt(seek.value / 1000 * durationMs); });
seek.addEventListener("change", () => {
  if (durationMs) call("/api/seek?ms=" + Math.round(seek.value / 1000 * durationMs));
  seeking = false;
});

let volTimer = null;
const vol = $("vol");
vol.addEventListener("input", () => { volTouching = true; $("volv").textContent = vol.value;
  clearTimeout(volTimer); volTimer = setTimeout(() => call("/api/volume?level=" + vol.value), 80); });
vol.addEventListener("change", () => { call("/api/volume?level=" + vol.value);
  setTimeout(() => volTouching = false, 300); });

let balTimer = null;
const bal = $("bal");
bal.addEventListener("input", () => { balTouching = true;
  const v = parseInt(bal.value, 10);
  $("balv").textContent = v === 0 ? "C" : (v < 0 ? Math.abs(v) + "L" : v + "R");
  clearTimeout(balTimer); balTimer = setTimeout(() => call("/api/balance?value=" + v), 80); });
bal.addEventListener("change", () => { call("/api/balance?value=" + bal.value);
  setTimeout(() => balTouching = false, 300); });

// ---- SSE ----
function connect() {
  const es = new EventSource(eventsUrl());
  es.addEventListener("open",  () => { $("conn").textContent = "connected"; $("conn").className = "conn on"; });
  es.addEventListener("error", () => { $("conn").textContent = "reconnecting…"; $("conn").className = "conn off"; });
  es.addEventListener("status",   (e) => applyStatus(JSON.parse(e.data)));
  es.addEventListener("position", (e) => applyPosition(JSON.parse(e.data).positionMs));
}
connect();

// ---- tabs ----
const panels = { player: $("tab-player"), playlist: $("tab-playlist"), files: $("tab-files"),
  sources: $("tab-sources"), clocks: $("tab-clocks") };
let activeTab = "player";
document.querySelectorAll(".tab").forEach((btn) => {
  btn.onclick = () => {
    activeTab = btn.dataset.tab;
    document.querySelectorAll(".tab").forEach((b) => b.classList.toggle("on", b === btn));
    for (const k in panels) panels[k].classList.toggle("hidden", k !== activeTab);
    if (activeTab === "playlist") loadPlaylist();
    if (activeTab === "files") browse(fbPath);
    if (activeTab === "sources") loadSources();
    if (activeTab === "clocks") loadClocks();
  };
});

// ---- playlist ----
async function loadPlaylist() {
  let data;
  try { data = await (await fetch("/api/playlist" + tokenQS())).json(); } catch { return; }
  const items = data.items || [];
  $("pl-count").textContent = items.length + (items.length === 1 ? " track" : " tracks");
  const ul = $("pl-list");
  ul.innerHTML = "";
  for (const it of items) {
    const li = document.createElement("li");
    if (it.current) li.classList.add("cur");
    const row = document.createElement("div");
    row.className = "pl-row";
    const t = document.createElement("div"); t.className = "t"; t.textContent = it.title || "—";
    const a = document.createElement("div"); a.className = "a"; a.textContent = it.artist || "";
    row.append(t, a);
    row.onclick = () => call("/api/playlist/play?index=" + it.index).then(() => setTimeout(loadPlaylist, 250));
    const dur = document.createElement("span"); dur.className = "pl-dur"; dur.textContent = it.duration || "";
    const x = document.createElement("button"); x.className = "pl-x"; x.textContent = "✕";
    x.onclick = (e) => { e.stopPropagation();
      call("/api/playlist/remove?index=" + it.index).then(() => setTimeout(loadPlaylist, 150)); };
    li.append(row, dur, x);
    ul.appendChild(li);
  }
}
$("pl-clear").onclick = () => call("/api/playlist/clear").then(() => setTimeout(loadPlaylist, 150));

// ---- file browser ----
let fbPath = "";
function tokenQS() {
  const t = new URLSearchParams(location.search).get("token") || localStorage.getItem("linamp_token") || "";
  return t ? "?token=" + encodeURIComponent(t) : "";
}
async function browse(path) {
  let data;
  try {
    const q = "/api/browse?path=" + encodeURIComponent(path);
    const t = new URLSearchParams(location.search).get("token") || localStorage.getItem("linamp_token") || "";
    data = await (await fetch(q + (t ? "&token=" + encodeURIComponent(t) : "")).then((r) => r)).json();
  } catch { return; }
  if (!data.ok) return;
  fbPath = data.path || "";
  $("fb-path").textContent = "/" + fbPath;
  const ul = $("fb-list");
  ul.innerHTML = "";
  for (const e of data.entries || []) {
    const li = document.createElement("li");
    const ico = document.createElement("span"); ico.className = "fb-ico";
    ico.textContent = e.type === "dir" ? "📁" : "🎵";
    const name = document.createElement("span"); name.textContent = e.name;
    name.style.cssText = "flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap";
    li.append(ico, name);
    if (e.type === "dir") {
      li.onclick = () => browse(e.path);
    } else {
      const add = document.createElement("span"); add.className = "add"; add.textContent = "+ add";
      add.onclick = (ev) => { ev.stopPropagation();
        call("/api/add?path=" + encodeURIComponent(e.path)).then(() => { add.textContent = "added"; }); };
      li.appendChild(add);
    }
    ul.appendChild(li);
  }
}
$("fb-up").onclick = () => {
  if (!fbPath) return;
  const parent = fbPath.includes("/") ? fbPath.slice(0, fbPath.lastIndexOf("/")) : "";
  browse(parent);
};

// refresh the playlist highlight when the track changes (status events)
window.addEventListener("linamp-status", () => { if (activeTab === "playlist") loadPlaylist(); });

// ---- sources + vban ----
async function loadSources() {
  let data;
  try { data = await (await fetch("/api/sources" + tokenQS())).json(); } catch { return; }
  const wrap = $("src-list");
  wrap.innerHTML = "";
  for (const label of data.sources || []) {
    const b = document.createElement("button");
    b.className = "src-btn" + (label === data.current ? " on" : "");
    b.textContent = label;
    b.onclick = () => call("/api/source?name=" + encodeURIComponent(label)).then(() => setTimeout(loadSources, 200));
    wrap.appendChild(b);
  }
  const vb = $("vban-btn");
  vb.textContent = "VBAN: " + (data.vban ? "ON" : "OFF");
  vb.classList.toggle("on", !!data.vban);
  vb.onclick = () => call("/api/vban?on=" + (data.vban ? 0 : 1)).then(() => setTimeout(loadSources, 200));
}

// ---- clocks ----
let clocksLoaded = false;
async function loadClocks() {
  if (clocksLoaded) return;
  let data;
  try { data = await (await fetch("/api/clock/list" + tokenQS())).json(); } catch { return; }
  const grid = $("clock-grid");
  grid.innerHTML = "";
  for (const face of data.faces || []) {
    const cell = document.createElement("div");
    cell.className = "clock-cell";
    cell.textContent = face;
    cell.onclick = () => call("/api/clock?face=" + encodeURIComponent(face));
    grid.appendChild(cell);
  }
  clocksLoaded = true;
}
$("ss-on").onclick = () => call("/api/screensaver/on");
$("ss-off").onclick = () => call("/api/screensaver/off");
