const $ = (id) => document.getElementById(id);

function fmt(ms) {
  if (!ms || ms < 0) return "0:00";
  const s = Math.floor(ms / 1000);
  return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
}

function applyStatus(s) {
  const t = [s.artist, s.album, s.title].filter(Boolean).join(" — ");
  $("track").textContent = t || "—";
  $("bitrate").textContent = (s.bitrate ? s.bitrate : "—") + " kbps";
  $("srate").textContent = (s.sampleRateHz ? Math.round(s.sampleRateHz / 1000) : "—") + " kHz";
  $("chan").textContent = s.channels === 1 ? "mono" : (s.channels === 2 ? "stereo" : "—");
  $("dur").textContent = fmt(s.durationMs || 0);
  $("pos").textContent = fmt(s.positionMs || 0);
  $("state").textContent = s.state || "stopped";
  $("vol").value = s.volume || 0;
  $("volv").textContent = s.volume || 0;
  $("balv").textContent = s.balance === 0 ? "center"
    : (s.balance < 0 ? Math.abs(s.balance) + "% L" : s.balance + "% R");
  $("source").textContent = s.source || "—";
}

function token() {
  return new URLSearchParams(location.search).get("token")
      || localStorage.getItem("linamp_token") || "";
}

function connect() {
  const t = token();
  const url = "/api/events" + (t ? "?token=" + encodeURIComponent(t) : "");
  const es = new EventSource(url);
  es.addEventListener("open", () => {
    $("conn").textContent = "connected";
    $("conn").className = "conn on";
  });
  es.addEventListener("error", () => {
    $("conn").textContent = "reconnecting…";
    $("conn").className = "conn off";
  });
  es.addEventListener("status", (e) => applyStatus(JSON.parse(e.data)));
  es.addEventListener("position", (e) => {
    $("pos").textContent = fmt(JSON.parse(e.data).positionMs);
  });
}

connect();
