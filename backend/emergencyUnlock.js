const { TableClient } = require("@azure/data-tables");

module.exports = async function (context, req) {
  try {
    const storageConn = process.env.AZURE_STORAGE_CONNECTION_STRING;
    if (!storageConn) {
      context.res = { status: 500, body: { ok: false, error: "Missing storage connection string" } };
      return;
    }

    //upisan flag u SystemState
    const stateTable = "SystemState";
    const stateClient = TableClient.fromConnectionString(storageConn, stateTable);

    // createTable ako jos ne postoji
    await stateClient.createTable().catch(() => {});

    const now = new Date().toISOString();

    await stateClient.upsertEntity(
      {
        partitionKey: "GLOBAL",
        rowKey: "EMERGENCY",
        active: true,
        updatedAtUtc: now
      },
      "Replace"
    );

    //upisan log u AccessLogs
    context.bindings.accessLogs = {
      partitionKey: "LOG",
      rowKey: `${now}_EMERGENCY_UNLOCK_REQUESTED`,
      eventType: "EMERGENCY_UNLOCK_REQUESTED",
      createdAtUtc: now,
      status: "REQUESTED"
    };

    //vracanje odgovora webu
    context.res = {
      status: 200,
      body: { ok: true, active: true, updatedAtUtc: now }
    };
  } catch (err) {
    context.log.error(err);
    context.res = { status: 500, body: { ok: false, error: "Emergency unlock failed" } };
  }
};
