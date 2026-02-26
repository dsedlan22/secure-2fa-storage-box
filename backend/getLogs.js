const { TableClient } = require("@azure/data-tables");

const connectionString = process.env.STORAGE_CONNECTION;
const tableName = "AccessLogs";

function toInt(value, def) {
  const n = parseInt(value, 10);
  return Number.isFinite(n) ? n : def;
}

module.exports = async function (context, req) {
  try {
    const tableClient = TableClient.fromConnectionString(connectionString, tableName);

    const nfcId = (req.query.nfcId || "").trim();
    const limit = Math.min(toInt(req.query.limit, 50), 200);

    const filter = nfcId ? `PartitionKey eq '${nfcId.replace(/'/g, "''")}'` : undefined;

    const items = [];
    const entities = tableClient.listEntities({
      queryOptions: filter ? { filter } : undefined
    });

    for await (const entity of entities) {
      items.push(entity);
      if (items.length >= limit) break;
    }

    items.sort((a, b) => (a.rowKey < b.rowKey ? 1 : -1));

    context.res = {
      headers: { "Content-Type": "application/json" },
      body: {
        ok: true,
        count: items.length,
        items: items.map(e => ({
          nfcId: e.partitionKey,
          rowKey: e.rowKey,
          sessionId: e.sessionId,
          eventType: e.eventType,
          phrase: e.phrase,
          attempts: e.attempts,
          status: e.status,
          createdAtUtc: e.createdAtUtc || e.timestamp
        }))
      }
    };
  } catch (e) {
    context.log("getLogs error:", e);
    context.res = {
      status: 500,
      body: { ok: false, error: e?.message || String(e) }
    };
  }
};
