/**
 * Watchdog Controller
 *
 * Coordinates all watchdog subsystems:
 * - Event loop monitoring
 * - Liveness/readiness probes
 * - Systemd notification (future)
 * - Circuit breakers (future)
 *
 * Provides a clean interface for the gateway to integrate health monitoring.
 */
import type { CircuitBreakerRegistry } from "./circuit-breaker.js";
import type { SystemdNotify } from "./sd-notify.js";
import { createCircuitBreakerRegistry } from "./circuit-breaker.js";
import { type EventLoopMonitorOptions, EventLoopMonitor } from "./event-loop.js";
import {
  type LivenessChecker,
  type LivenessConfig,
  type LivenessState,
  type ReadinessChecker,
  type ReadinessState,
  createLivenessChecker,
  createReadinessChecker,
} from "./liveness.js";
import { createSystemdNotify } from "./sd-notify.js";

export type WatchdogConfig = {
  /** Event loop monitoring options */
  eventLoop?: EventLoopMonitorOptions;
  /** Liveness check options */
  liveness?: LivenessConfig;
  /** Enable event loop monitoring. Default: true */
  eventLoopEnabled?: boolean;
  /** Enable systemd notify integration. Default: true (but no-op if not on systemd) */
  systemdEnabled?: boolean;
};

export type WatchdogState = {
  /** Event loop monitor state */
  eventLoop: {
    enabled: boolean;
    lagP99Ms: number;
    maxLagMs: number;
    healthy: boolean;
  };
  /** Liveness state */
  liveness: LivenessState;
  /** Readiness state */
  readiness: ReadinessState;
  /** Overall health status */
  healthy: boolean;
  /** Uptime in milliseconds */
  uptimeMs: number;
};

export type WatchdogReadinessProvider = {
  /** Check if all channels are ready */
  getChannelsReady: () => boolean;
  /** Check if the server is accepting connections */
  getAcceptingConnections: () => boolean;
};

/**
 * WatchdogController coordinates all health monitoring subsystems.
 */
export class WatchdogController {
  private eventLoopMonitor: EventLoopMonitor | null = null;
  private livenessChecker: LivenessChecker | null = null;
  private readinessChecker: ReadinessChecker | null = null;
  private systemdNotify: SystemdNotify | null = null;
  private circuitBreakerRegistry: CircuitBreakerRegistry | null = null;

  private fatalErrorCount = 0;
  private started = false;
  private startedAt: number | null = null;

  private readinessProvider: WatchdogReadinessProvider | null = null;
  private readonly config: WatchdogConfig;

  constructor(config: WatchdogConfig = {}) {
    this.config = config;
  }

  /**
   * Start all watchdog subsystems.
   */
  start(): void {
    if (this.started) {
      return;
    }

    this.startedAt = Date.now();

    // Initialize event loop monitor
    if (this.config.eventLoopEnabled !== false) {
      this.eventLoopMonitor = new EventLoopMonitor(this.config.eventLoop);
      this.eventLoopMonitor.start();
    }

    // Initialize liveness checker
    this.livenessChecker = createLivenessChecker({
      eventLoopMonitor: this.eventLoopMonitor,
      getFatalErrorCount: () => this.fatalErrorCount,
      config: this.config.liveness,
    });

    // Initialize readiness checker (will use default provider until set)
    this.readinessChecker = createReadinessChecker({
      getChannelsReady: () => this.readinessProvider?.getChannelsReady() ?? true,
      getAcceptingConnections: () => this.readinessProvider?.getAcceptingConnections() ?? false,
    });

    // Initialize systemd notify (no-op if not on systemd)
    if (this.config.systemdEnabled !== false) {
      this.systemdNotify = createSystemdNotify();
    }

    // Initialize circuit breaker registry
    this.circuitBreakerRegistry = createCircuitBreakerRegistry();

    this.started = true;
  }

  /**
   * Stop all watchdog subsystems.
   */
  stop(): void {
    if (!this.started) {
      return;
    }

    // Notify systemd we're stopping
    this.systemdNotify?.stopping();

    // Stop event loop monitor
    if (this.eventLoopMonitor) {
      this.eventLoopMonitor.stop();
      this.eventLoopMonitor = null;
    }

    this.livenessChecker = null;
    this.readinessChecker = null;
    this.systemdNotify = null;
    this.circuitBreakerRegistry = null;

    this.started = false;
  }

  /**
   * Set the readiness provider for checking service readiness.
   * Call this after the gateway is initialized to plug in actual readiness checks.
   */
  setReadinessProvider(provider: WatchdogReadinessProvider): void {
    this.readinessProvider = provider;
  }

  /**
   * Record a fatal error (affects liveness check).
   */
  recordFatalError(): void {
    this.fatalErrorCount += 1;
  }

  /**
   * Clear fatal error count (e.g., after recovery).
   */
  clearFatalErrors(): void {
    this.fatalErrorCount = 0;
  }

  /**
   * Get the current fatal error count.
   */
  getFatalErrorCount(): number {
    return this.fatalErrorCount;
  }

  /**
   * Signal that the service is ready (notifies systemd if available).
   */
  signalReady(): void {
    this.systemdNotify?.ready();
  }

  /**
   * Check if the service is live (healthy enough to keep running).
   * If this returns false, the service should be restarted.
   */
  isLive(): boolean {
    if (!this.livenessChecker) {
      return true;
    }
    return this.livenessChecker.isLive();
  }

  /**
   * Check if the service is ready (able to accept traffic).
   * If this returns false, traffic should be routed elsewhere.
   */
  isReady(): boolean {
    if (!this.readinessChecker) {
      return false;
    }
    return this.readinessChecker.isReady();
  }

  /**
   * Get detailed liveness state.
   */
  getLivenessState(): LivenessState {
    if (!this.livenessChecker) {
      return {
        eventLoopHealthy: true,
        memoryHealthy: true,
        heapUsageRatio: 0,
        heapAllocationRatio: 0,
        heapUsedMB: 0,
        heapLimitMB: 0,
        hasFatalErrors: false,
        live: true,
      };
    }
    return this.livenessChecker.getLivenessState();
  }

  /**
   * Get detailed readiness state.
   */
  getReadinessState(): ReadinessState {
    if (!this.readinessChecker) {
      return {
        channelsReady: false,
        acceptingConnections: false,
        ready: false,
      };
    }
    return this.readinessChecker.getReadinessState();
  }

  /**
   * Get the event loop monitor instance (for advanced usage).
   */
  getEventLoopMonitor(): EventLoopMonitor | null {
    return this.eventLoopMonitor;
  }

  /**
   * Get the systemd notify instance (for advanced usage).
   */
  getSystemdNotify(): SystemdNotify | null {
    return this.systemdNotify;
  }

  /**
   * Get the circuit breaker registry (for advanced usage).
   */
  getCircuitBreakerRegistry(): CircuitBreakerRegistry | null {
    return this.circuitBreakerRegistry;
  }

  /**
   * Get the current uptime in milliseconds.
   */
  getUptimeMs(): number {
    if (!this.startedAt) {
      return 0;
    }
    return Date.now() - this.startedAt;
  }

  /**
   * Check if the watchdog is currently running.
   */
  isRunning(): boolean {
    return this.started;
  }

  /**
   * Get a complete state snapshot of all watchdog subsystems.
   */
  getState(): WatchdogState {
    const livenessState = this.getLivenessState();
    const readinessState = this.getReadinessState();
    const eventLoopState = this.eventLoopMonitor?.getState() ?? {
      lagP99Ms: 0,
      maxLagMs: 0,
      healthy: true,
    };

    return {
      eventLoop: {
        enabled: this.eventLoopMonitor !== null,
        ...eventLoopState,
      },
      liveness: livenessState,
      readiness: readinessState,
      healthy: livenessState.live && readinessState.ready,
      uptimeMs: this.getUptimeMs(),
    };
  }
}

/**
 * Create a new WatchdogController with the given configuration.
 */
export function createWatchdogController(config: WatchdogConfig = {}): WatchdogController {
  return new WatchdogController(config);
}
