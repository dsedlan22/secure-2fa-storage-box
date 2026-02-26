const { logEvent } = require("../tableLogger");

const WORDS = [
  "purple", "lion", "jump",
  "green", "river", "open",
  "silver", "window", "paper",
  "sun", "blue", "table",
  "coffee", "banana", "stone",
  "safe", "box", "unlock"
];

global.sessions = global.sessions || new Map();

function pick3Words() {
  // uzmi 3 riječi bez ponavljanja
  const pool = [...WORDS];
  const chosen = [];
  for (let i = 0; i < 3; i++) {
    const idx = Math.floor(Math.random() * pool.length);
    chosen.push(pool[idx]);
    pool.splice(idx, 1);
  }
  return chosen;
}

module.exports = async function (context, req) {
    context.log("sessionStart: entered");
    context.log("STORAGE_CONNECTION present:", !!process.env.STORAGE_CONNECTION);

  const nfcId = (req.query.nfcId || req.body?.nfcId || "demo-card-001").toString();

  const phraseWords = pick3Words();
  const sessionId = `sess_${Date.now()}_${Math.floor(Math.random() * 10000)}`;

  const now = Date.now();
  const expiresInSec = 90; // fraza odnosno sesija vrijedi 90 sekundi 
  const session = {
    sessionId,
    nfcId,
    phrase: phraseWords.join(" "),
    phraseWords,
    createdAtUtc: new Date(now).toISOString(),
    expiresAtUtc: new Date(now + expiresInSec * 1000).toISOString(),
    attempts: 0,
    status: "PENDING"
  };

  global.sessions.set(sessionId, session);
try {
  await logEvent({
    nfcId,
    sessionId,
    eventType: "SESSION_STARTED",
    phrase: session.phrase,
    attempts: 0,
    status: session.status
  });
} catch (e) {
  context.log("FAILED to log SESSION_STARTED:", e?.message || e);
}

  context.res = {
    headers: { "Content-Type": "application/json" },
    body: {
      ok: true,
      sessionId,
      nfcId,
      phrase: session.phrase,
      phraseWords: session.phraseWords,
      expiresAtUtc: session.expiresAtUtc
    }
  };
};
