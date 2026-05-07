import type { GatewayRequestHandlers } from "./types.js";
import {
  formatDoctorNonInteractiveHint,
  type RestartSentinelPayload,
  writeRestartSentinel,
} from "../../infra/restart-sentinel.js";
import { scheduleGatewaySigusr1Restart } from "../../infra/restart.js";
import { ErrorCodes, errorShape } from "../protocol/index.js";

export const gatewayHandlers: GatewayRequestHandlers = {
  /**
   * Trigger a graceful gateway restart via SIGUSR1.
   * This is the method the UI should call for the restart button.
   */
  "gateway.restart": async ({ params, respond, context }) => {
    // Check if restart is allowed by config
    const config = context.getConfig?.();
    if (config?.commands?.restart === false) {
      respond(
        false,
        undefined,
        errorShape(
          ErrorCodes.FORBIDDEN,
          "Gateway restart is disabled. Set commands.restart=true in config to enable.",
        ),
      );
      return;
    }

    const sessionKey =
      typeof (params as { sessionKey?: unknown }).sessionKey === "string"
        ? (params as { sessionKey?: string }).sessionKey?.trim() || undefined
        : undefined;

    const reason =
      typeof (params as { reason?: unknown }).reason === "string"
        ? (params as { reason?: string }).reason?.trim() || "UI restart button"
        : "UI restart button";

    const delayMsRaw = (params as { delayMs?: unknown }).delayMs;
    const delayMs =
      typeof delayMsRaw === "number" && Number.isFinite(delayMsRaw)
        ? Math.max(0, Math.min(Math.floor(delayMsRaw), 60000))
        : 2000;

    // Write restart sentinel for recovery after restart
    const payload: RestartSentinelPayload = {
      kind: "restart",
      status: "ok",
      ts: Date.now(),
      sessionKey,
      message: reason,
      doctorHint: formatDoctorNonInteractiveHint(),
      stats: {
        mode: "gateway.restart",
        reason,
      },
    };

    let sentinelPath: string | null = null;
    try {
      sentinelPath = await writeRestartSentinel(payload);
    } catch {
      sentinelPath = null;
    }

    // Schedule the restart
    const restart = scheduleGatewaySigusr1Restart({
      delayMs,
      reason,
    });

    respond(
      true,
      {
        ok: true,
        restart,
        sentinel: {
          path: sentinelPath,
          payload,
        },
      },
      undefined,
    );
  },
};
