const BASE_URL =
  "https://rus-sigurna-kutija-api-abcugrfhdnc2acc2.francecentral-01.azurewebsites.net";

function qs(id) {
  return document.getElementById(id);
}

function formatSessionDate(isoString) {
  try {
    const d = new Date(isoString);
    return d.toLocaleString("hr-HR", {
      day: "numeric",
      month: "short",
      year: "numeric",
      hour: "2-digit",
      minute: "2-digit"
    });
  } catch (_) {
    return isoString;
  }
}

async function fetchJsonNoCache(url) {
  // cache-buster + no-store (browser/CDN)
  const sep = url.includes("?") ? "&" : "?";
  const bustUrl = `${url}${sep}_=${Date.now()}`;

  const res = await fetch(bustUrl, {
    method: "GET",
    cache: "no-store",
    headers: {
      "Accept": "application/json",
      "Cache-Control": "no-cache"
    }
  });

  return res.json();
}

// ==========================
// CURRENT SESSION (from logs)
// ==========================
async function loadCurrentSessionFromLogs() {
  try {
    const data = await fetchJsonNoCache(`${BASE_URL}/api/getLogs?limit=200`);

    if (!data.ok || !Array.isArray(data.items)) return;

    // 🔑 UZMI SAMO SESSION_STARTED
    const sessions = data.items.filter(
      item => item.eventType === "SESSION_STARTED"
    );

    if (sessions.length === 0) return;

    // 🔑 NAJNOVIJI SESSION_STARTED
    sessions.sort(
      (a, b) => new Date(b.createdAtUtc) - new Date(a.createdAtUtc)
    );

    const latest = sessions[0];

    qs("sessionId").textContent = latest.sessionId || "-";
    qs("phrase").textContent = latest.phrase || "-";
    const created = latest.createdAtUtc ? formatSessionDate(latest.createdAtUtc) : "-";
    qs("expires").textContent = "Kreirano: " + created;

  } catch (e) {
    console.warn("Cannot load current session");
  }
}


// ==========================
// ACCESS LOGS (cards)
// ==========================
const EVENT_TYPE_TITLES = {
  SESSION_STARTED: "Nova sesija",
  VERIFY_SUCCESS: "Kutija otključana",
  VERIFY_FAIL: "Verifikacija",
  EMERGENCY_UNLOCK_REQUESTED: "Zahtjev za hitno otključavanje",
  EMERGENCY_UNLOCK_CONSUMED: "Hitno otključavanje izvršeno",
  EMAIL_SENT: "Poslan e-mail administratoru",
  LOCKED: "Kutija zaključana"
};

function getEventTitle(eventType) {
  return EVENT_TYPE_TITLES[eventType] ?? eventType;
}

function getStatusBadgeClass(status) {
  if (!status) return "log-badge--neutral";
  const s = String(status).toUpperCase();
  if (s === "SUCCESS") return "log-badge--success";
  if (s === "PENDING" || s === "REQUESTED") return "log-badge--pending";
  if (s === "LOCKED" || s === "CONSUMED") return "log-badge--locked";
  return "log-badge--neutral";
}

function isEmergencyLog(item) {
  const t = item.eventType;
  return t === "EMERGENCY_UNLOCK_REQUESTED" || t === "EMERGENCY_UNLOCK_CONSUMED";
}

function formatRelativeTime(createdAtUtc) {
  if (!createdAtUtc) return "";
  const date = new Date(createdAtUtc);
  const now = new Date();
  const diffMs = now - date;
  const diffMin = Math.floor(diffMs / 60000);
  const diffH = Math.floor(diffMs / 3600000);
  const diffD = Math.floor(diffMs / 86400000);
  if (diffMin < 1) return "Upravo sada";
  if (diffMin < 60) return `Prije ${diffMin} min`;
  if (diffH < 24) return diffH === 1 ? "Prije 1 h" : `Prije ${diffH} h`;
  if (diffD === 1) return "Prije 1 dan";
  if (diffD < 7) return `Prije ${diffD} dana`;
  return date.toLocaleDateString("hr-HR");
}

function renderLogs(items) {
  const container = qs("logs");
  container.innerHTML = "";
  if (!items || items.length === 0) return;
  items.forEach(item => {
    const card = document.createElement("div");
    card.className = "log-card";
    const title = getEventTitle(item.eventType);
    const timeStr = formatRelativeTime(item.createdAtUtc);
    const status = item.status ?? "";
    const badgeClass = getStatusBadgeClass(status);
    const showPhrase = !isEmergencyLog(item) && item.phrase != null && item.phrase !== "";
    const showAttempts = item.attempts != null && item.attempts !== "";
    card.innerHTML =
      `<div class="log-card__header">
        <span class="log-card__time">${escapeHtml(timeStr)}</span>
        <span class="log-badge ${badgeClass}">${escapeHtml(status || "—")}</span>
      </div>
      <h3 class="log-card__title">${escapeHtml(title)}</h3>` +
      (showPhrase ? `<p class="log-card__meta"><span class="log-card__label">Fraza:</span> ${escapeHtml(item.phrase)}</p>` : "") +
      (showAttempts ? `<p class="log-card__meta"><span class="log-card__label">Pokušaj:</span> ${escapeHtml(String(item.attempts))}</p>` : "");
    container.appendChild(card);
  });
  container.scrollTop = 0;
}

function escapeHtml(text) {
  const div = document.createElement("div");
  div.textContent = text;
  return div.innerHTML;
}

qs("btnLoadLogs").addEventListener("click", async () => {
  try {
    const data = await fetchJsonNoCache(`${BASE_URL}/api/getLogs?limit=200`);

    if (!data.ok || !Array.isArray(data.items)) {
      qs("logs").textContent = "Greška pri dohvaćanju logova";
      return;
    }

    // KRONOLOŠKI: najnoviji -> najstariji (za timeline)
    const items = [...data.items].sort((a, b) => {
      const ta = Date.parse(a.createdAtUtc || 0);
      const tb = Date.parse(b.createdAtUtc || 0);
      return tb - ta;
    });

    renderLogs(items);
  } catch (err) {
    qs("logs").textContent = "Greška u komunikaciji s backendom";
  }
});


// ==========================
// EMERGENCY UNLOCK
// ==========================
qs("btnEmergency").addEventListener("click", async () => {
  qs("emergencyStatus").textContent = "Slanje zahtjeva...";

  try {
    const res = await fetch(`${BASE_URL}/api/emergencyUnlock`, {
      method: "POST"
    });
    const data = await res.json();

    if (data.ok) {
      qs("emergencyStatus").textContent =
        "Emergency unlock aktiviran. Uređaj će se otključati.";
    } else {
      qs("emergencyStatus").textContent =
        "Greška pri aktivaciji emergency unlocka.";
    }
  } catch (e) {
    qs("emergencyStatus").textContent =
      "Greška u komunikaciji s backendom.";
  }
});


// auto refresh
setInterval(loadCurrentSessionFromLogs, 5000);
loadCurrentSessionFromLogs();
