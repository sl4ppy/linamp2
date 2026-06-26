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
  $("ss-on").onclick = () => call("/api/screensaver/on");
  $("ss-off").onclick = () => call("/api/screensaver/off");
  if (clocksLoaded) return;
  let data;
  try { data = await (await fetch("/api/clock/list" + tokenQS())).json(); } catch { return; }
  const grid = $("clock-grid");
  grid.innerHTML = "";
  for (const face of data.faces || []) {
    const cell = document.createElement("div");
    cell.className = "clock-cell";
    const cv = document.createElement("canvas");
    cv.className = "clock-thumb"; cv.width = 132; cv.height = 80;
    drawClockThumb(cv.getContext("2d"), 132, 80, face);
    const nm = document.createElement("div");
    nm.className = "clock-name"; nm.textContent = face;
    cell.append(cv, nm);
    cell.onclick = () => call("/api/clock?face=" + encodeURIComponent(face));
    grid.appendChild(cell);
  }
  clocksLoaded = true;
}

// ---- clock-face thumbnails (rendered client-side; zero device cost) ----
const FACE_CFG = {
  "Luxury":       { t:"analog", dial:"#0f193c", hand:"#c8aa50", tick:"#c8aa50", acc:"#b4b4b4" },
  "Aviator":      { t:"analog", dial:"#232323", hand:"#e6e6e6", tick:"#d2d2d2", acc:"#ff8c1e" },
  "Diver":        { t:"analog", dial:"#0a0a0a", hand:"#dcdcdc", tick:"#c8c8c8", acc:"#e6321e" },
  "Minimalist":   { t:"analog", dial:"#070707", hand:"#c8c8c8", tick:"#7a7a7a", acc:"#b4b4b4", minimal:1 },
  "Chronograph":  { t:"analog", dial:"#1e1e1e", hand:"#dcdcdc", tick:"#bebebe", acc:"#f0d228" },
  "Neon Retro":   { t:"analog", dial:"#05050f", hand:"#00ffc8", tick:"#00ffc8", acc:"#ff50c8", glow:1 },
  "Bauhaus":      { t:"analog", dial:"#111317", hand:"#f2f4f6", tick:"#eef0f2", acc:"#ffc400" },
  "Mondaine":     { t:"analog", dial:"#f6f6f3", hand:"#15171a", tick:"#15171a", acc:"#e2231a", lolly:1 },
  "Orbital":      { t:"orbital" },
  "Guilloche":    { t:"guilloche" },
  "Digital":      { t:"text", c:"#33ff88" },
  "Seven Segment":{ t:"seg",  c:"#1fe3ff" },
  "Split Flap":   { t:"flap" },
  "Nixie":        { t:"nixie", c:"#ff9a45" },
  "Terminal":     { t:"term",  c:"#36ff74" },
  "VFD":             { t:"vfd",    c:"#34e7c8" },
  "Wandering Hours": { t:"wander" },
  "Regulator":       { t:"regulator" },
  "Word Clock":      { t:"word" },
  "Berlin Uhr":      { t:"berlin" },
};

function drawClockThumb(ctx, w, h, face) {
  ctx.clearRect(0, 0, w, h);
  const cfg = FACE_CFG[face] || { t:"analog", dial:"#15151c", hand:"#ccc", tick:"#888", acc:"#0c8" };
  if (cfg.t === "analog")    return thumbAnalog(ctx, w, h, cfg);
  if (cfg.t === "orbital")   return thumbOrbital(ctx, w, h);
  if (cfg.t === "guilloche") return thumbGuilloche(ctx, w, h);
  if (cfg.t === "text")      return thumbText(ctx, w, h, "10:10", cfg.c);
  if (cfg.t === "seg")       return thumbSeg(ctx, w, h, cfg.c);
  if (cfg.t === "flap")      return thumbFlap(ctx, w, h);
  if (cfg.t === "nixie")     return thumbNixie(ctx, w, h, cfg.c);
  if (cfg.t === "term")      return thumbTerm(ctx, w, h, cfg.c);
  if (cfg.t === "vfd")       return thumbVFD(ctx, w, h, cfg.c);
  if (cfg.t === "wander")    return thumbWander(ctx, w, h);
  if (cfg.t === "regulator") return thumbRegulator(ctx, w, h);
  if (cfg.t === "word")      return thumbWord(ctx, w, h);
  if (cfg.t === "berlin")    return thumbBerlin(ctx, w, h);
}

function thumbAnalog(ctx, w, h, c) {
  const cx = w / 2, cy = h / 2, r = Math.min(w, h) * 0.42;
  ctx.fillStyle = c.dial; ctx.beginPath(); ctx.arc(cx, cy, r, 0, 7); ctx.fill();
  ctx.strokeStyle = c.acc; ctx.lineWidth = 1.5; ctx.stroke();
  ctx.strokeStyle = c.tick;
  for (let i = 0; i < 12; i++) {
    if (c.minimal && i % 3) continue;
    const a = i * Math.PI / 6, ca = Math.cos(a), sa = Math.sin(a);
    ctx.lineWidth = (i % 3 === 0) ? 2 : 1;
    ctx.beginPath();
    ctx.moveTo(cx + ca * r * 0.78, cy + sa * r * 0.78);
    ctx.lineTo(cx + ca * r * 0.92, cy + sa * r * 0.92);
    ctx.stroke();
  }
  if (c.glow) { ctx.shadowColor = c.hand; ctx.shadowBlur = 6; }
  const hand = (deg, len, wid, col) => {
    const a = (deg - 90) * Math.PI / 180;
    ctx.strokeStyle = col; ctx.lineWidth = wid; ctx.lineCap = "round";
    ctx.beginPath(); ctx.moveTo(cx, cy);
    ctx.lineTo(cx + Math.cos(a) * r * len, cy + Math.sin(a) * r * len); ctx.stroke();
  };
  hand(305, 0.5, 3, c.hand);  // 10:10
  hand(60, 0.72, 2, c.hand);
  hand(210, 0.8, 1, c.acc);
  if (c.lolly) {
    const a = (210 - 90) * Math.PI / 180;
    ctx.fillStyle = c.acc; ctx.beginPath();
    ctx.arc(cx + Math.cos(a) * r * 0.58, cy + Math.sin(a) * r * 0.58, 3, 0, 7); ctx.fill();
  }
  ctx.shadowBlur = 0;
  ctx.fillStyle = c.acc; ctx.beginPath(); ctx.arc(cx, cy, 2, 0, 7); ctx.fill();
}

function thumbOrbital(ctx, w, h) {
  const cx = w / 2, cy = h / 2, R = Math.min(w, h) * 0.42;
  ctx.fillStyle = "#0c0f15"; ctx.beginPath(); ctx.arc(cx, cy, R, 0, 7); ctx.fill();
  const arc = (rad, deg, col, lw) => {
    ctx.strokeStyle = col; ctx.lineWidth = lw; ctx.lineCap = "round";
    ctx.beginPath(); ctx.arc(cx, cy, rad, -Math.PI / 2, (deg - 90) * Math.PI / 180); ctx.stroke();
  };
  arc(R * 0.82, 204, "#ffb02e", 2);
  arc(R * 0.58, 63, "#2ee6c4", 3);
  arc(R * 0.34, 305, "#5b8cff", 4);
  ctx.fillStyle = "#e7eaf0"; ctx.font = "bold 13px monospace";
  ctx.textAlign = "center"; ctx.textBaseline = "middle";
  ctx.fillText("10:10", cx, cy);
}

function thumbGuilloche(ctx, w, h) {
  const cx = w / 2, cy = h / 2, r = Math.min(w, h) * 0.42;
  const g = ctx.createRadialGradient(cx, cy - r * 0.2, r * 0.1, cx, cy, r);
  g.addColorStop(0, "#1f7a5a"); g.addColorStop(0.6, "#0e4d39"); g.addColorStop(1, "#062a20");
  ctx.fillStyle = g; ctx.beginPath(); ctx.arc(cx, cy, r, 0, 7); ctx.fill();
  ctx.save(); ctx.beginPath(); ctx.arc(cx, cy, r * 0.97, 0, 7); ctx.clip();
  for (let i = 0; i < 60; i++) {
    ctx.strokeStyle = i % 2 ? "rgba(255,255,255,0.06)" : "rgba(0,0,0,0.12)";
    ctx.lineWidth = 0.6; const a = i * Math.PI / 30;
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(cx + Math.cos(a) * r, cy + Math.sin(a) * r); ctx.stroke();
  }
  ctx.restore();
  ctx.strokeStyle = "#c9a85a"; ctx.lineWidth = 2; ctx.beginPath(); ctx.arc(cx, cy, r, 0, 7); ctx.stroke();
  const hand = (deg, len, wid) => {
    const a = (deg - 90) * Math.PI / 180; ctx.strokeStyle = "#edd699";
    ctx.lineWidth = wid; ctx.lineCap = "round";
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(cx + Math.cos(a) * r * len, cy + Math.sin(a) * r * len); ctx.stroke();
  };
  hand(305, 0.5, 3); hand(60, 0.72, 2);
  ctx.fillStyle = "#c9a85a"; ctx.beginPath(); ctx.arc(cx, cy, 2, 0, 7); ctx.fill();
}

function thumbText(ctx, w, h, txt, col) {
  ctx.fillStyle = "#04060a"; ctx.fillRect(0, 0, w, h);
  ctx.shadowColor = col; ctx.shadowBlur = 8; ctx.fillStyle = col;
  ctx.font = "bold 26px 'DejaVu Sans Mono', monospace";
  ctx.textAlign = "center"; ctx.textBaseline = "middle";
  ctx.fillText(txt, w / 2, h / 2); ctx.shadowBlur = 0;
}

function thumbSeg(ctx, w, h, col) {
  ctx.fillStyle = "#04060a"; ctx.fillRect(0, 0, w, h);
  const MAP = { "1":[0,1,1,0,0,0,0], "0":[1,1,1,1,1,1,0] };
  const t = 3, dw = 18, dh = 40, y = (h - dh) / 2;
  const seg = (x, on, lit) => {
    const m = MAP[on], vH = dh / 2 - 1.5 * t;
    const R = [[x+t,y,dw-2*t,t],[x+dw-t,y+t,t,vH],[x+dw-t,y+dh/2+t/2,t,vH],
              [x+t,y+dh-t,dw-2*t,t],[x,y+dh/2+t/2,t,vH],[x,y+t,t,vH],[x+t,y+dh/2-t/2,dw-2*t,t]];
    for (let i = 0; i < 7; i++) {
      ctx.fillStyle = m[i] ? lit : "rgba(255,255,255,0.06)";
      ctx.fillRect(R[i][0], R[i][1], R[i][2], R[i][3]);
    }
  };
  ctx.shadowColor = col; ctx.shadowBlur = 5;
  let x = w / 2 - (dw * 4 + 18) / 2;
  seg(x, "1", col); x += dw + 4; seg(x, "0", col); x += dw + 8;
  ctx.fillStyle = col; ctx.beginPath(); ctx.arc(x - 4, y + dh * 0.35, 2, 0, 7); ctx.fill();
  ctx.beginPath(); ctx.arc(x - 4, y + dh * 0.65, 2, 0, 7); ctx.fill();
  seg(x, "1", col); x += dw + 4; seg(x, "0", col);
  ctx.shadowBlur = 0;
}

function thumbFlap(ctx, w, h) {
  ctx.fillStyle = "#0a0a0c"; ctx.fillRect(0, 0, w, h);
  const tw = 40, th = 50, gap = 8, y = (h - th) / 2;
  const x0 = (w - (tw * 2 + gap)) / 2;
  const tile = (x, s) => {
    const g = ctx.createLinearGradient(0, y, 0, y + th);
    g.addColorStop(0, "#34343c"); g.addColorStop(0.49, "#26262c");
    g.addColorStop(0.51, "#191920"); g.addColorStop(1, "#202028");
    ctx.fillStyle = g; roundRect(ctx, x, y, tw, th, 5); ctx.fill();
    ctx.strokeStyle = "#050507"; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(x, y + th / 2); ctx.lineTo(x + tw, y + th / 2); ctx.stroke();
    ctx.fillStyle = "#f4f4f2"; ctx.font = "bold 28px 'Arial Narrow', sans-serif";
    ctx.textAlign = "center"; ctx.textBaseline = "middle"; ctx.fillText(s, x + tw / 2, y + th / 2);
  };
  tile(x0, "10"); tile(x0 + tw + gap, "10");
}

function thumbNixie(ctx, w, h, col) {
  const g = ctx.createLinearGradient(0, 0, 0, h);
  g.addColorStop(0, "#0c0a09"); g.addColorStop(1, "#15100c");
  ctx.fillStyle = g; ctx.fillRect(0, 0, w, h);
  const digs = ["1", "0", "1", "0"], tw = 22, tht = 52, gap = 4, y = (h - tht) / 2;
  let x = (w - (tw * 4 + gap * 3)) / 2;
  ctx.textAlign = "center"; ctx.textBaseline = "middle";
  for (let i = 0; i < 4; i++) {
    ctx.fillStyle = "rgba(60,55,48,0.45)"; roundRect(ctx, x, y, tw, tht, tw * 0.45); ctx.fill();
    ctx.font = "bold 30px 'DejaVu Sans Mono', monospace";
    ctx.shadowColor = col; ctx.shadowBlur = 8; ctx.fillStyle = col;
    ctx.fillText(digs[i], x + tw / 2, y + tht / 2); ctx.shadowBlur = 0;
    x += tw + gap;
  }
}

function thumbTerm(ctx, w, h, col) {
  ctx.fillStyle = "#020604"; ctx.fillRect(0, 0, w, h);
  ctx.strokeStyle = "rgba(54,255,116,0.4)"; ctx.lineWidth = 1;
  roundRect(ctx, 4, 4, w - 8, h - 8, 6); ctx.stroke();
  ctx.fillStyle = col; ctx.textAlign = "left"; ctx.textBaseline = "alphabetic";
  ctx.font = "9px 'DejaVu Sans Mono', monospace"; ctx.fillText("linamp:~$ date", 12, 26);
  ctx.font = "bold 20px 'DejaVu Sans Mono', monospace"; ctx.fillText("10:10", 12, 52);
  ctx.fillRect(64, 40, 8, 12);
}

function thumbVFD(ctx, w, h, col) {
  ctx.fillStyle = "#070f10"; roundRect(ctx, 2, 8, w - 4, h - 16, 8); ctx.fill();
  ctx.strokeStyle = "rgba(80,120,120,0.4)"; ctx.lineWidth = 1; ctx.stroke();
  ctx.shadowColor = col; ctx.shadowBlur = 8; ctx.fillStyle = col;
  ctx.font = "bold 30px 'DejaVu Sans Mono', monospace";
  ctx.textAlign = "center"; ctx.textBaseline = "middle";
  ctx.fillText("10:08", w / 2, h / 2); ctx.shadowBlur = 0;
  ctx.strokeStyle = "rgba(120,180,170,0.10)"; ctx.lineWidth = 1;
  for (let x = 6; x < w - 6; x += 5) { ctx.beginPath(); ctx.moveTo(x, 10); ctx.lineTo(x, h - 10); ctx.stroke(); }
}

function thumbWander(ctx, w, h) {
  ctx.fillStyle = "#0c0d12"; ctx.fillRect(0, 0, w, h);
  const cx = w / 2, cy = h - 14, aR = h * 0.78;
  ctx.strokeStyle = "rgba(200,210,230,0.4)"; ctx.lineWidth = 1.2;
  ctx.beginPath(); ctx.arc(cx, cy, aR, Math.PI * 1.15, Math.PI * 1.85); ctx.stroke();
  ctx.fillStyle = "#1c1f27"; ctx.beginPath(); ctx.arc(cx, cy, 8, 0, 7); ctx.fill();
  const aAct = Math.PI * 1.5, dr = h * 0.52;
  const discs = [[aAct - 0.5, false, "9"], [aAct, true, "10"], [aAct + 0.5, false, "11"]];
  ctx.textAlign = "center"; ctx.textBaseline = "middle";
  for (const [a, on, t] of discs) {
    const x = cx + Math.cos(a) * dr, y = cy + Math.sin(a) * dr;
    ctx.strokeStyle = on ? "rgba(127,212,255,0.5)" : "rgba(150,160,180,0.15)"; ctx.lineWidth = 1.5;
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(x, y); ctx.stroke();
    if (on) { ctx.shadowColor = "#7fd4ff"; ctx.shadowBlur = 8; }
    ctx.fillStyle = on ? "#10202b" : "#15171d"; ctx.beginPath(); ctx.arc(x, y, 13, 0, 7); ctx.fill(); ctx.shadowBlur = 0;
    ctx.strokeStyle = on ? "rgba(127,212,255,0.85)" : "rgba(140,150,170,0.25)"; ctx.lineWidth = 1.5; ctx.stroke();
    ctx.fillStyle = on ? "#eaf6ff" : "rgba(160,170,190,0.4)"; ctx.font = "bold 12px 'DejaVu Sans', sans-serif";
    ctx.fillText(t, x, y);
  }
}

function thumbRegulator(ctx, w, h) {
  ctx.fillStyle = "#0d1016"; ctx.fillRect(0, 0, w, h);
  const cy = h / 2;
  const dial = (cx, R, deg, gold) => {
    ctx.fillStyle = "#141821"; ctx.beginPath(); ctx.arc(cx, cy, R, 0, 7); ctx.fill();
    ctx.strokeStyle = "rgba(190,200,220,0.25)"; ctx.lineWidth = 1; ctx.stroke();
    for (let i = 0; i < 12; i++) {
      const a = i * Math.PI / 6; ctx.strokeStyle = "rgba(210,220,235,0.4)"; ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(cx + Math.cos(a) * R * 0.78, cy + Math.sin(a) * R * 0.78);
      ctx.lineTo(cx + Math.cos(a) * R * 0.92, cy + Math.sin(a) * R * 0.92); ctx.stroke();
    }
    const a = (deg - 90) * Math.PI / 180; ctx.strokeStyle = gold ? "#ffd34d" : "#e8eef8";
    ctx.lineWidth = gold ? 2.5 : 1.5; ctx.lineCap = "round";
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(cx + Math.cos(a) * R * 0.7, cy + Math.sin(a) * R * 0.7); ctx.stroke();
    ctx.fillStyle = "#cfd6e2"; ctx.beginPath(); ctx.arc(cx, cy, 2, 0, 7); ctx.fill();
  };
  dial(w * 0.22, 16, 300, false); dial(w * 0.5, 22, 120, true); dial(w * 0.78, 16, 240, false);
}

function thumbWord(ctx, w, h) {
  ctx.fillStyle = "#0c0e12"; ctx.fillRect(0, 0, w, h);
  const G = ["ITLISASAMPM", "ACQUARTERDC", "TWENTYFIVEX", "HALFBTENFTO", "PASTERUNINE",
             "ONESIXTHREE", "FOURFIVETWO", "EIGHTELEVEN", "SEVENTWELVE", "TENSEOCLOCK"];
  const lit = new Set(["0,0", "0,1", "0,3", "0,4", "3,5", "3,6", "3,7", "4,0", "4,1", "4,2", "4,3", "9,0", "9,1", "9,2"]);
  const cw = 12, ch = 8;
  ctx.font = "bold 7px 'DejaVu Sans Mono', monospace"; ctx.textAlign = "center"; ctx.textBaseline = "middle";
  for (let r = 0; r < 10; r++) for (let c = 0; c < 11; c++) {
    const on = lit.has(r + "," + c);
    if (on) { ctx.shadowColor = "rgba(120,200,255,0.9)"; ctx.shadowBlur = 4; ctx.fillStyle = "#f1f7ff"; }
    else { ctx.shadowBlur = 0; ctx.fillStyle = "rgba(150,162,182,0.3)"; }
    ctx.fillText(G[r][c], c * cw + cw / 2, r * ch + ch / 2);
  }
  ctx.shadowBlur = 0;
}

function thumbBerlin(ctx, w, h) {
  ctx.fillStyle = "#101216"; ctx.fillRect(0, 0, w, h);
  const red = "#ff3b30", redOff = "#3a1412", yel = "#ffd60a", yelOff = "#3a3410";
  ctx.fillStyle = yel; ctx.beginPath(); ctx.arc(w / 2, 7, 3, 0, 7); ctx.fill();
  const rowY = [14, 28, 42, 56], lh = 10, iw = w - 16, x0 = 8;
  const row = (items, litN, y, colOf) => {
    const gap = 3, lw = (iw - gap * (items - 1)) / items;
    for (let i = 0; i < items; i++) {
      const cc = colOf(i); ctx.fillStyle = i < litN ? cc[0] : cc[1];
      roundRect(ctx, x0 + i * (lw + gap), y, lw, lh, 2); ctx.fill();
    }
  };
  row(4, 2, rowY[0], () => [red, redOff]);
  row(4, 0, rowY[1], () => [red, redOff]);
  row(11, 1, rowY[2], i => ((i + 1) % 3 === 0) ? [red, redOff] : [yel, yelOff]);
  row(4, 3, rowY[3], () => [yel, yelOff]);
}

function roundRect(ctx, x, y, w, h, r) {
  ctx.beginPath(); ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r); ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r); ctx.arcTo(x, y, x + w, y, r); ctx.closePath();
}
