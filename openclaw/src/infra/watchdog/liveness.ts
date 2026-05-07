/**
 * Liveness and Readiness Health Probes
 *
 * Implements Kubernetes-style health probes:
 *
 * - isLive(): Indicates if the process is alive and not deadlocked.
 *   If this fails, the process should be restarted.
 *   Checks: event loop responsive, no fatal errors, memory usage acceptable.
 *
 * - isReady(): Indicates if the service is ready to accept traffic.
 *   If this fails, traffic should be routed elsewhere (but don't restart).
 *   Checks: all channels initialized, accepting connections.
 */
import v8 from "node:v8";
import type { EventLoopMonitor } from "./event-loop.js";

export type LivenessConfig = {
  /** Maximum heap usage percentage before considered unhealthy. Default: 0.9 (90%) */
  maxHeapUsageRatio?: number;
};

export type LivenessState = {
  /** Whether the event loop is responsive */
  eventLoopHealthy: boolean;
  /** Whether memory usage is within acceptable limits */
  memoryHealthy: boolean;
  /** Heap used vs heap limit (0-1) — true memory pressure indicator */
  heapUsageRatio: number;
  /** Heap used vs current allocation (0-1) — V8 internal pressure */
  heapAllocationRatio: number;
  /** Heap used in MB */
  heapUsedMB: number;
  /** Heap limit in MB */
  heapLimitMB: number;
  /** Whether there are fatal errors recorded */
  hasFatalErrors: boolean;
  /** Overall liveness status */
  live: boolean;
};

export type ReadinessState = {
  /** Whether channels are initialized */
  channelsReady: boolean;
  /** Whether the server is accepting connections */
  acceptingConnections: boolean;
  /** Overall readiness status */
  ready: boolean;
};

export type LivenessChecker = {
  isLive: () => boolean;
  getLivenessState: () => LivenessState;
};

export type ReadinessChecker = {
  isReady: () => boolean;
  getReadinessState: () => ReadinessState;
};

/**
 * Create a liveness checker that evaluates process health.
 */
export function createLivenessChecker(params: {
  eventLoopMonitor: EventLoopMonitor | null;
  getFatalErrorCount: () => number;
  config?: LivenessConfig;
}): LivenessChecker {
  const { eventLoopMonitor, getFatalErrorCount, config } = params;
  const maxHeapUsageRatio = config?.maxHeapUsageRatio ?? 0.9;

  type HeapMetrics = {
    /** Heap used vs max limit — true "are we running out of memory?" */
    usageRatio: number;
    /** Heap used vs current allocation — V8 internal pressure */
    allocationRatio: number;
    /** Heap used in MB */
    usedMB: number;
    /** Heap limit in MB */
    limitMB: number;
  };

  function getHeapMetrics(): HeapMetrics {
    const memUsage = process.memoryUsage();
    const heapStats = v8.getHeapStatistics();
    
    // heapUsed / heap_size_limit = actual memory pressure against max allowed
    // This is what matters for "are we going to OOM?"
    const usageRatio = memUsage.heapUsed / heapStats.heap_size_limit;
    
    // heapUsed / heapTotal = V8 internal allocation pressure
    // Useful for seeing if V8 is frequently GC'ing within its current allocation
    const allocationRatio = memUsage.heapUsed / memUsage.heapTotal;
    
    return {
      usageRatio,
      allocationRatio,
      usedMB: Math.round(memUsage.heapUsed / 1024 / 1024),
      limitMB: Math.round(heapStats.heap_size_limit / 1024 / 1024),
    };
  }

  function isEventLoopHealthy(): boolean {
    if (!eventLoopMonitor) {
      // If no monitor, assume healthy
      return true;
    }
    return eventLoopMonitor.isHealthy();
  }

  function isMemoryHealthy(): boolean {
    return getHeapMetrics().usageRatio < maxHeapUsageRatio;
  }

  function hasFatalErrors(): boolean {
    return getFatalErrorCount() > 0;
  }

  function isLive(): boolean {
    return isEventLoopHealthy() && isMemoryHealthy() && !hasFatalErrors();
  }

  function getLivenessState(): LivenessState {
    const eventLoopHealthy = isEventLoopHealthy();
    const heap = getHeapMetrics();
    const memoryHealthy = heap.usageRatio < maxHeapUsageRatio;
    const fatalErrors = hasFatalErrors();
    return {
      eventLoopHealthy,
      memoryHealthy,
      heapUsageRatio: Math.round(heap.usageRatio * 1000) / 1000,
      heapAllocationRatio: Math.round(heap.allocationRatio * 1000) / 1000,
      heapUsedMB: heap.usedMB,
      heapLimitMB: heap.limitMB,
      hasFatalErrors: fatalErrors,
      live: eventLoopHealthy && memoryHealthy && !fatalErrors,
    };
  }

  return { isLive, getLivenessState };
}

/**
 * Create a readiness checker that evaluates service readiness.
 */
export function createReadinessChecker(params: {
  getChannelsReady: () => boolean;
  getAcceptingConnections: () => boolean;
}): ReadinessChecker {
  const { getChannelsReady, getAcceptingConnections } = params;

  function isReady(): boolean {
    return getChannelsReady() && getAcceptingConnections();
  }

  function getReadinessState(): ReadinessState {
    const channelsReady = getChannelsReady();
    const acceptingConnections = getAcceptingConnections();
    return {
      channelsReady,
      acceptingConnections,
      ready: channelsReady && acceptingConnections,
    };
  }

  return { isReady, getReadinessState };
}
