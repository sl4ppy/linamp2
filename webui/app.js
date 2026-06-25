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

// ---- spectrum canvas ----
const canvas = $("spectrum");
const ctx = canvas.getContext("2d");
let bars = new Array(19).fill(0);
let smooth = new Array(19).fill(0);
function drawSpectrum() {
  const w = canvas.width, h = canvas.height, n = bars.length;
  ctx.clearRect(0, 0, w, h);
  const bw = w / n;
  for (let i = 0; i < n; i++) {
    smooth[i] = Math.max(bars[i], smooth[i] - 2); // falloff
    const bh = (smooth[i] / 40) * h;
    const hue = 120 - (smooth[i] / 40) * 120;     // green→red
    ctx.fillStyle = `hsl(${hue} 90% 50%)`;
    ctx.fillRect(i * bw + 1, h - bh, bw - 1, bh);
  }
  requestAnimationFrame(drawSpectrum);
}
requestAnimationFrame(drawSpectrum);

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
  es.addEventListener("spectrum", (e) => { bars = JSON.parse(e.data); });
}
connect();
