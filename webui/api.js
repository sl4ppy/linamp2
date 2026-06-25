// Thin control-call wrapper. All controls are GET; token appended if present.
export function apiToken() {
  return new URLSearchParams(location.search).get("token")
      || localStorage.getItem("linamp_token") || "";
}

export function call(path) {
  const t = apiToken();
  const url = path + (path.includes("?") ? "&" : "?") + (t ? "token=" + encodeURIComponent(t) : "_=1");
  return fetch(url, { method: "GET" }).catch(() => {});
}

export function eventsUrl() {
  const t = apiToken();
  return "/api/events" + (t ? "?token=" + encodeURIComponent(t) : "");
}
