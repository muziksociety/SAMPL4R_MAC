import type { GatewayBrowserClient } from "../gateway";

export type GatewayHealthStatus = {
  live: boolean;
  ready: boolean;
  liveDetails: {
    status: string;
    uptime?: number;
    heap?: { used: number; total: number; percent: number };
    eventLoop?: { lag: number };
  } | null;
  readyDetails: {
    status: string;
    checks?: Record<string, { status: string; message?: string }>;
  } | null;
  lastFetchedAt: number;
};

export type GatewayControlState = {
  client: GatewayBrowserClient | null;
  connected: boolean;
  gatewayHealth: GatewayHealthStatus | null;
  gatewayHealthLoading: boolean;
  gatewayHealthError: string | null;
  gatewayRestarting: boolean;
  gatewayRestartError: string | null;
};

export async function fetchGatewayHealth(state: GatewayControlState): Promise<void> {
  if (!state.client || !state.connected) {
    return;
  }
  if (state.gatewayHealthLoading) {
    return;
  }
  
  state.gatewayHealthLoading = true;
  state.gatewayHealthError = null;
  
  try {
    // Use the gateway health RPC method
    const healthResult = await state.client.request<{
      live?: { ok?: boolean; status?: string; uptime?: number; heap?: unknown; eventLoop?: unknown };
      ready?: { ok?: boolean; status?: string; checks?: Record<string, unknown> };
    }>("health", {});
    
    const liveOk = healthResult?.live?.ok ?? true;
    const readyOk = healthResult?.ready?.ok ?? true;
    
    const heap = healthResult?.live?.heap as { used?: number; total?: number; percent?: number } | undefined;
    const eventLoop = healthResult?.live?.eventLoop as { lag?: number } | undefined;
    
    state.gatewayHealth = {
      live: liveOk,
      ready: readyOk,
      liveDetails: {
        status: healthResult?.live?.status ?? (liveOk ? "healthy" : "unhealthy"),
        uptime: healthResult?.live?.uptime,
        heap: heap?.used !== undefined ? {
          used: heap.used,
          total: heap.total ?? 0,
          percent: heap.percent ?? 0,
        } : undefined,
        eventLoop: eventLoop?.lag !== undefined ? { lag: eventLoop.lag } : undefined,
      },
      readyDetails: {
        status: healthResult?.ready?.status ?? (readyOk ? "ready" : "not ready"),
        checks: healthResult?.ready?.checks as Record<string, { status: string; message?: string }> | undefined,
      },
      lastFetchedAt: Date.now(),
    };
  } catch (err) {
    state.gatewayHealthError = String(err);
    // Set partial health state on error
    state.gatewayHealth = {
      live: false,
      ready: false,
      liveDetails: null,
      readyDetails: null,
      lastFetchedAt: Date.now(),
    };
  } finally {
    state.gatewayHealthLoading = false;
  }
}

export async function restartGateway(state: GatewayControlState): Promise<boolean> {
  if (!state.client || !state.connected) {
    state.gatewayRestartError = "Not connected to gateway";
    return false;
  }
  if (state.gatewayRestarting) {
    return false;
  }
  
  state.gatewayRestarting = true;
  state.gatewayRestartError = null;
  
  try {
    await state.client.request("gateway.restart", {});
    return true;
  } catch (err) {
    state.gatewayRestartError = String(err);
    return false;
  } finally {
    state.gatewayRestarting = false;
  }
}

// Polling utilities
let healthPollInterval: number | null = null;

export function startHealthPolling(
  state: GatewayControlState,
  intervalMs = 10000,
): void {
  if (healthPollInterval != null) {
    return;
  }
  // Initial fetch
  void fetchGatewayHealth(state);
  // Start polling
  healthPollInterval = window.setInterval(() => {
    void fetchGatewayHealth(state);
  }, intervalMs);
}

export function stopHealthPolling(): void {
  if (healthPollInterval == null) {
    return;
  }
  clearInterval(healthPollInterval);
  healthPollInterval = null;
}

export function restartHealthPolling(
  state: GatewayControlState,
  intervalMs = 10000,
): void {
  stopHealthPolling();
  startHealthPolling(state, intervalMs);
}
