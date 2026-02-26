const { logEvent } = require("../tableLogger");
const { sendAdminAlert } = require("../emailSender");
function normalize(text) {
  return text
    .toLowerCase()
    .replace(/[^a-z\s]/g, "")
    .replace(/\s+/g, " ")
    .trim();
}

module.exports = async function (context, req) {
  const sessionId = req.body?.sessionId || req.query?.sessionId;
  const spokenText = req.body?.spokenText || req.query?.spokenText;

  if (!sessionId || !spokenText) {
    context.res = {
      status: 400,
      body: { ok: false, error: "sessionId and spokenText are required" }
    };
    return;
  }

  const session = global.sessions?.get(sessionId);

  if (!session) {
    context.res = {
      status: 404,
      body: { ok: false, error: "Session not found" }
    };
    return;
  }

  if (session.status === "LOCKED") {
    context.res = {
      status: 403,
      body: { ok: false, error: "Session locked (3 failed attempts)" }
    };
    return;
  }

  const now = Date.now();
  if (now > Date.parse(session.expiresAtUtc)) {
    session.status = "EXPIRED";
    context.res = {
      status: 403,
      body: { ok: false, error: "Session expired" }
    };
    return;
  }

  session.attempts += 1;

  const expected = normalize(session.phrase);
  const received = normalize(spokenText);

  const success = expected === received;

  if (success) {
    session.status = "SUCCESS";
  } else if (session.attempts >= 3) {
    session.status = "LOCKED";
  }
  
  if (session.status === "LOCKED" && !session.lockEmailSent) {
  try {
    await sendAdminAlert({
      nfcId: session.nfcId,
      sessionId,
      phrase: session.phrase,
      attempts: session.attempts
    });

    session.lockEmailSent = true;

    try {
      await logEvent({
        nfcId: session.nfcId,
        sessionId,
        eventType: "EMAIL_SENT",
        phrase: session.phrase,
        attempts: session.attempts,
        status: session.status
      });
    } catch (_) {}
  } catch (e) {
    context.log("FAILED to send ACS email:", e?.message || e);
  }
}

  
  try {
  await logEvent({
    nfcId: session.nfcId,
    sessionId,
    eventType: success ? "VERIFY_SUCCESS" : "VERIFY_FAIL",
    phrase: session.phrase,
    attempts: session.attempts,
    status: session.status
  });

  if (session.status === "LOCKED") {
    await logEvent({
      nfcId: session.nfcId,
      sessionId,
      eventType: "LOCKED",
      phrase: session.phrase,
      attempts: session.attempts,
      status: session.status
    });
  }
} catch (e) {
  context.log("FAILED to log VERIFY event:", e?.message || e);
}


  context.res = {
    headers: { "Content-Type": "application/json" },
    body: {
      ok: true,
      success,
      attempts: session.attempts,
      status: session.status,
      expected,   
      received   
    }
  };
};
