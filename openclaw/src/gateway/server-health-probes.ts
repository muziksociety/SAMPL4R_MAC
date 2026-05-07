/**
 * HTTP Health Probe Endpoints
 *
 * Provides /health/live and /health/ready endpoints for orchestration platforms
 * (Kubernetes, systemd, load balancers, etc.).
 *
 * - /health/live: Returns 200 if the process is alive and not deadlocked.
 *   If this fails (503), the process should be restarted.
 *
 * - /health/ready: Returns 200 if the service is ready to accept traffic.
 *   If this fails (503), traffic should be routed elsewhere but don't restart.
 */
import type { IncomingMessage, ServerResponse } from "node:http";
import type { WatchdogController } from "../infra/watchdog/index.js";

export type HealthProbeRequestHandler = (
  req: IncomingMessage,
  res: ServerResponse,
) => Promise<boolean>;

export type HealthProbeHandlerOptions = {
  /** The watchdog controller instance */
  watchdog: WatchdogController | null;
  /** Optional callback when liveness fails */
  onLivenessFailed?: () => void;
};

/**
 * Create the health probe HTTP request handler.
 */
export function createHealthProbeRequestHandler(
  opts: HealthProbeHandlerOptions,
): HealthProbeRequestHandler {
  const { watchdog, onLivenessFailed } = opts;

  return async (req, res) => {
    const url = new URL(req.url ?? "/", "http://localhost");
    const path = url.pathname;

    // Only handle GET requests
    if (req.method !== "GET" && req.method !== "HEAD") {
      return false;
    }

    // /health/live - Liveness probe
    if (path === "/health/live") {
      return handleLivenessProbe(res, watchdog, onLivenessFailed);
    }

    // /health/ready - Readiness probe
    if (path === "/health/ready") {
      return handleReadinessProbe(res, watchdog);
    }

    // /health - Combined health status (convenience endpoint)
    if (path === "/health") {
      return handleCombinedHealthProbe(res, watchdog);
    }

    return false;
  };
}

function handleLivenessProbe(
  res: ServerResponse,
  watchdog: WatchdogController | null,
  onLivenessFailed?: () => void,
): boolean {
  if (!watchdog) {
    // No watchdog, assume live
    sendJsonResponse(res, 200, { status: "ok", live: true });
    return true;
  }

  const isLive = watchdog.isLive();
  const state = watchdog.getLivenessState();

  if (!isLive) {
    onLivenessFailed?.();
    sendJsonResponse(res, 503, {
      status: "unhealthy",
      live: false,
      details: state,
    });
    return true;
  }

  sendJsonResponse(res, 200, {
    status: "ok",
    live: true,
    details: state,
  });
  return true;
}

function handleReadinessProbe(
  res: ServerResponse,
  watchdog: WatchdogController | null,
): boolean {
  if (!watchdog) {
    // No watchdog, assume not ready
    sendJsonResponse(res, 503, { status: "unavailable", ready: false });
    return true;
  }

  const isReady = watchdog.isReady();
  const state = watchdog.getReadinessState();

  if (!isReady) {
    sendJsonResponse(res, 503, {
      status: "unavailable",
      ready: false,
      details: state,
    });
    return true;
  }

  sendJsonResponse(res, 200, {
    status: "ok",
    ready: true,
    details: state,
  });
  return true;
}

function handleCombinedHealthProbe(
  res: ServerResponse,
  watchdog: WatchdogController | null,
): boolean {
  if (!watchdog) {
    sendJsonResponse(res, 200, {
      status: "ok",
      live: true,
      ready: false,
      uptimeMs: Math.round(process.uptime() * 1000),
    });
    return true;
  }

  const state = watchdog.getState();
  const statusCode = state.healthy ? 200 : 503;

  sendJsonResponse(res, statusCode, {
    status: state.healthy ? "ok" : "unhealthy",
    live: state.liveness.live,
    ready: state.readiness.ready,
    uptimeMs: state.uptimeMs,
    eventLoop: state.eventLoop,
    liveness: state.liveness,
    readiness: state.readiness,
  });
  return true;
}

function sendJsonResponse(res: ServerResponse, status: number, body: unknown): void {
  res.statusCode = status;
  res.setHeader("Content-Type", "application/json; charset=utf-8");
  res.setHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  res.end(JSON.stringify(body));
}
