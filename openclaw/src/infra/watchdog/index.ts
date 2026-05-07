/**
 * Watchdog System
 *
 * Provides health monitoring, liveness/readiness probes, and infrastructure
 * for service supervision.
 *
 * @example
 * ```typescript
 * import { createWatchdogController, EscalationStrategy } from './infra/watchdog';
 *
 * // Create and start watchdog
 * const watchdog = createWatchdogController({
 *   eventLoop: { thresholdMs: 100 },
 *   liveness: { maxHeapUsageRatio: 0.9 },
 * });
 * watchdog.start();
 *
 * // Set readiness provider
 * watchdog.setReadinessProvider({
 *   getChannelsReady: () => channelsInitialized,
 *   getAcceptingConnections: () => serverListening,
 * });
 *
 * // Check health
 * if (!watchdog.isLive()) {
 *   // Process should be restarted
 * }
 * if (!watchdog.isReady()) {
 *   // Don't send traffic yet
 * }
 *
 * // Escalating restart strategy
 * const escalation = new EscalationStrategy();
 * const delay = escalation.recordCrash();
 * await sleep(delay);
 * // restart...
 * ```
 */

// Controller
export {
  type WatchdogConfig,
  type WatchdogState,
  type WatchdogReadinessProvider,
  WatchdogController,
  createWatchdogController,
} from "./controller.js";

// Event loop monitoring
export {
  type EventLoopMonitorOptions,
  type EventLoopMonitorState,
  EventLoopMonitor,
} from "./event-loop.js";

// Liveness/readiness probes
export {
  type LivenessConfig,
  type LivenessState,
  type ReadinessState,
  type LivenessChecker,
  type ReadinessChecker,
  createLivenessChecker,
  createReadinessChecker,
} from "./liveness.js";

// Escalation strategy
export {
  type EscalationConfig,
  type EscalationState,
  EscalationStrategy,
} from "./escalation.js";

// Systemd integration (stub)
export {
  type SystemdNotifyState,
  type SystemdNotify,
  createSystemdNotify,
} from "./sd-notify.js";

// Circuit breaker (stub)
export {
  type CircuitState,
  type CircuitBreakerConfig,
  type CircuitBreakerState,
  type CircuitBreakerEventType,
  type CircuitBreakerEvent,
  type CircuitBreaker,
  type CircuitBreakerRegistry,
  CircuitOpenError,
  createCircuitBreaker,
  createCircuitBreakerRegistry,
} from "./circuit-breaker.js";
