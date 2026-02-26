const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "POST,OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type"
};

module.exports = async function (context, req) {
  
  if (req.method === "OPTIONS") {
    context.res = { status: 204, headers: CORS, body: "" };
    return;
  }

  try {
    const key = process.env.SPEECH_KEY;
    const region = process.env.SPEECH_REGION;
    const lang = process.env.SPEECH_LANG || "en-US";

    if (!key || !region) {
      context.res = {
        status: 500,
        headers: CORS,
        body: { ok: false, error: "Missing SPEECH_KEY/SPEECH_REGION" }
      };
      return;
    }

    
    const url =
      `https://${region}.stt.speech.microsoft.com/speech/recognition/conversation/cognitiveservices/v1` +
      `?language=${encodeURIComponent(lang)}&format=simple`;

    
    let audioBuffer = null;

    if (Buffer.isBuffer(req.body)) {
      audioBuffer = req.body;
    } else if (Buffer.isBuffer(req.rawBody)) {
      audioBuffer = req.rawBody;
    } else {
      context.log("Invalid audio body type:", typeof req.body, "rawBody type:", typeof req.rawBody);
      audioBuffer = Buffer.alloc(0);
    }

    if (!audioBuffer || audioBuffer.length < 1000) {
      context.res = {
        status: 400,
        headers: CORS,
        body: { ok: false, error: "Send WAV (PCM 16kHz mono) audio bytes in request body" }
      };
      return;
    }

    const resp = await fetch(url, {
      method: "POST",
      headers: {
        "Ocp-Apim-Subscription-Key": key,
        "Accept": "application/json",
        
        "Content-Type": "audio/wav; codecs=audio/pcm; samplerate=16000"
      },
      body: audioBuffer
    });

    
    const rawText = await resp.text();
    let data = null;
    try {
      data = JSON.parse(rawText);
    } catch {
      data = { raw: rawText };
    }

    const text = data?.DisplayText || data?.displayText || data?.Text || "";

    context.res = {
      status: resp.ok ? 200 : 500,
      headers: { ...CORS, "Content-Type": "application/json" },
      body: { ok: resp.ok, text, raw: data }
    };
  } catch (e) {
    context.log("speechToText error:", e);
    context.res = {
      status: 500,
      headers: CORS,
      body: { ok: false, error: e?.message || String(e) }
    };
  }
};
