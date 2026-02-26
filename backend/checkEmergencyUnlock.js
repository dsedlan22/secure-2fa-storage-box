const { TableClient } = require("@azure/data-tables");

module.exports = async function (context, req) {
  try {
    const storageConn = process.env.AZURE_STORAGE_CONNECTION_STRING;
    if (!storageConn) {
      context.res = { status: 500, body: { ok: false, error: "Missing storage connection string" } };
      return;
    }

    const stateClient = TableClient.fromConnectionString(storageConn, "SystemState");
    await stateClient.createTable().catch(() => {});

    const pk = "GLOBAL";
    const rk = "EMERGENCY";

    let entity;
    try {
      entity = await stateClient.getEntity(pk, rk);
    } catch (e) {
      
      entity = null;
    }

    const active = entity && entity.active === true;

    const now = new Date().toISOString();

    if (active) {
      await stateClient.upsertEntity(
        {
          partitionKey: pk,
          rowKey: rk,
          active: false,
          updatedAtUtc: now
        },
        "Replace"
      );

      context.bindings.accessLogs = {
        partitionKey: "LOG",
        rowKey: `${now}_EMERGENCY_UNLOCK_CONSUMED`,
        eventType: "EMERGENCY_UNLOCK_CONSUMED",
        createdAtUtc: now,
        status: "CONSUMED"
      };

      context.res = { status: 200, body: { ok: true, active: true } };
      return;
    }

    
    context.res = { status: 200, body: { ok: true, active: false } };
  } catch (err) {
    context.log.error(err);
    context.res = { status: 500, body: { ok: false, error: "Check emergency failed" } };
  }
};
