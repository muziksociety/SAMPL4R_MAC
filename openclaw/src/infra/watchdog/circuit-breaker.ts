/**
 * Circuit Breaker Pattern (STUB)
 *
 * This module provides a circuit breaker implementation for isolating failing
 * services (channels, providers, external APIs). It is currently a stub with
 * detailed documentation for future implementation.
 *
 * ## What is the Circuit Breaker Pattern?
 *
 * The circuit breaker pattern prevents cascading failures by temporarily
 * stopping requests to a failing service. Like an electrical circuit breaker,
 * it "trips" when too many failures occur, then periodically tests if the
 * service has recovered.
 *
 * ## States
 *
 * ```
 *                      failure threshold
 *                         exceeded
 *   ┌──────────┐        ┌──────────┐
 *   │  CLOSED  │───────►│   OPEN   │
 *   │ (normal) │        │ (failed) │
 *   └──────────┘        └──────────┘
 *        ▲                    │
 *        │                    │ timeout expires
 *        │                    ▼
 *        │              ┌──────────┐
 *        │              │HALF_OPEN │
 *        │              │ (testing)│
 *        │              └──────────┘
 *        │                    │
 *        └────────────────────┘
 *              success / failure
 * ```
 *
 * - **CLOSED**: Normal operation. Requests pass through, failures are counted.
 *   Transitions to OPEN when failure threshold is exceeded.
 *
 * - **OPEN**: Circuit is tripped. Requests fail immediately without attempting.
 *   Transitions to HALF_OPEN after a timeout period.
 *
 * - **HALF_OPEN**: Testing recovery. A limited number of requests pass through.
 *   Success → CLOSED, Failure → OPEN.
 *
 * ## When would we need this?
 *
 * - **Multiple unreliable external services**: Discord, Telegram, OpenAI APIs
 *   can all have temporary outages. Circuit breakers prevent one failing API
 *   from blocking all operations.
 *
 * - **High-traffic scenarios**: When handling many requests, a slow or failing
 *   downstream service can cause thread pool exhaustion. Circuit breakers
 *   provide fast failure and backpressure.
 *
 * - **Provider rotation**: When one LLM provider fails, quickly switch to
 *   an alternative without waiting for timeouts.
 *
 * - **Channel isolation**: A failing Discord webhook shouldn't affect Telegram
 *   message delivery.
 *
 * ## Configuration options
 *
 * - `failureThreshold`: Number of failures before opening (e.g., 5)
 * - `successThreshold`: Successes in HALF_OPEN to close (e.g., 3)
 * - `timeout`: Time in OPEN before trying HALF_OPEN (e.g., 30 seconds)
 * - `volumeThreshold`: Minimum requests before evaluation (e.g., 10)
 * - `errorFilter`: Function to determine which errors count as failures
 *
 * ## Example usage (future)
 *
 * ```typescript
 * const discordBreaker = createCircuitBreaker({
 *   name: 'discord',
 *   failureThreshold: 5,
 *   timeout: 30000,
 *   successThreshold: 3,
 * });
 *
 * async function sendDiscordMessage(message: string) {
 *   return discordBreaker.execute(async () => {
 *     return await discord.send(message);
 *   });
 * }
 * ```
 *
 * ## Monitoring
 *
 * Circuit breakers should emit events for monitoring:
 * - `stateChange`: When transitioning between states
 * - `success`: Successful request through breaker
 * - `failure`: Failed request (counted toward threshold)
 * - `rejected`: Request rejected while OPEN
 *
 * ## Implementation considerations
 *
 * - Use sliding window for failure counting (not just consecutive)
 * - Consider exponential backoff for timeout increases
 * - Track separate metrics per operation type
 * - Integrate with health state for dashboard visibility
 */

/** Circuit breaker states */
export type CircuitState = "CLOSED" | "OPEN" | "HALF_OPEN";

/** Configuration for a circuit breaker */
export type CircuitBreakerConfig = {
  /** Unique name for this breaker */
  name: string;
  /** Number of failures before opening the circuit */
  failureThreshold: number;
  /** Time in milliseconds to wait before transitioning from OPEN to HALF_OPEN */
  timeoutMs: number;
  /** Number of consecutive successes in HALF_OPEN required to close */
  successThreshold: number;
  /** Minimum number of requests before evaluating failures */
  volumeThreshold?: number;
  /** Function to determine if an error should count as a failure */
  errorFilter?: (error: unknown) => boolean;
};

/** Current state snapshot of a circuit breaker */
export type CircuitBreakerState = {
  /** Current state of the circuit */
  state: CircuitState;
  /** Number of failures in the current window */
  failureCount: number;
  /** Number of successes in HALF_OPEN */
  successCount: number;
  /** Timestamp when the circuit opened (ms since epoch) */
  openedAt: number | null;
  /** Total requests through this breaker */
  totalRequests: number;
  /** Total failures */
  totalFailures: number;
  /** Total rejections (requests blocked while OPEN) */
  totalRejections: number;
};

/** Circuit breaker event types */
export type CircuitBreakerEventType = "stateChange" | "success" | "failure" | "rejected";

/** Circuit breaker event payload */
export type CircuitBreakerEvent = {
  type: CircuitBreakerEventType;
  name: string;
  state: CircuitState;
  previousState?: CircuitState;
  error?: unknown;
};

/**
 * STUB: CircuitBreaker interface for future implementation.
 */
export interface CircuitBreaker {
  /** Get the circuit breaker name */
  getName(): string;

  /** Get the current state */
  getState(): CircuitState;

  /** Get a full state snapshot */
  getStateSnapshot(): CircuitBreakerState;

  /** Check if the circuit allows requests */
  isAllowed(): boolean;

  /**
   * Execute a function through the circuit breaker.
   * Throws CircuitOpenError if the circuit is open.
   */
  execute<T>(fn: () => Promise<T>): Promise<T>;

  /**
   * Record a successful call (manual tracking).
   */
  recordSuccess(): void;

  /**
   * Record a failed call (manual tracking).
   */
  recordFailure(error?: unknown): void;

  /**
   * Force the circuit to a specific state (for testing/admin).
   */
  forceState(state: CircuitState): void;

  /**
   * Reset the circuit breaker to initial state.
   */
  reset(): void;

  /**
   * Subscribe to circuit breaker events.
   */
  onEvent(handler: (event: CircuitBreakerEvent) => void): () => void;
}

/**
 * Error thrown when a request is rejected due to an open circuit.
 */
export class CircuitOpenError extends Error {
  readonly circuitName: string;
  readonly retryAfterMs: number | null;

  constructor(circuitName: string, retryAfterMs?: number) {
    super(`Circuit breaker '${circuitName}' is open`);
    this.name = "CircuitOpenError";
    this.circuitName = circuitName;
    this.retryAfterMs = retryAfterMs ?? null;
  }
}

/**
 * STUB: Create a no-op circuit breaker.
 *
 * This implementation always allows requests and doesn't track state.
 * Replace with actual implementation when needed.
 *
 * Future implementation would:
 * 1. Track request successes/failures in a sliding window
 * 2. Transition between CLOSED/OPEN/HALF_OPEN based on thresholds
 * 3. Emit events for monitoring
 * 4. Integrate with the health state system
 */
export function createCircuitBreaker(config: CircuitBreakerConfig): CircuitBreaker {
  const { name } = config;

  // STUB: Always allow, no state tracking
  const stubState: CircuitBreakerState = {
    state: "CLOSED",
    failureCount: 0,
    successCount: 0,
    openedAt: null,
    totalRequests: 0,
    totalFailures: 0,
    totalRejections: 0,
  };

  return {
    getName: () => name,
    getState: () => "CLOSED",
    getStateSnapshot: () => ({ ...stubState }),
    isAllowed: () => true,
    execute: async <T>(fn: () => Promise<T>): Promise<T> => {
      // STUB: Just execute without tracking
      return fn();
    },
    recordSuccess: () => {
      /* no-op */
    },
    recordFailure: (_error?: unknown) => {
      /* no-op */
    },
    forceState: (_state: CircuitState) => {
      /* no-op */
    },
    reset: () => {
      /* no-op */
    },
    onEvent: (_handler: (event: CircuitBreakerEvent) => void) => {
      return () => {
        /* no-op unsubscribe */
      };
    },
  };
}

/**
 * STUB: Circuit breaker registry for managing multiple breakers.
 *
 * Future implementation would provide:
 * - Named breaker lookup
 * - Batch state queries for health dashboard
 * - Shared event bus for monitoring
 */
export interface CircuitBreakerRegistry {
  /** Get or create a circuit breaker by name */
  get(name: string, config?: Partial<CircuitBreakerConfig>): CircuitBreaker;

  /** Get all registered breakers */
  getAll(): Map<string, CircuitBreaker>;

  /** Get combined state snapshot for health reporting */
  getHealthSnapshot(): Record<string, CircuitBreakerState>;
}

/**
 * STUB: Create a no-op circuit breaker registry.
 */
export function createCircuitBreakerRegistry(): CircuitBreakerRegistry {
  const breakers = new Map<string, CircuitBreaker>();

  return {
    get: (name: string, config?: Partial<CircuitBreakerConfig>) => {
      let breaker = breakers.get(name);
      if (!breaker) {
        breaker = createCircuitBreaker({
          name,
          failureThreshold: config?.failureThreshold ?? 5,
          timeoutMs: config?.timeoutMs ?? 30000,
          successThreshold: config?.successThreshold ?? 3,
          ...config,
        });
        breakers.set(name, breaker);
      }
      return breaker;
    },
    getAll: () => new Map(breakers),
    getHealthSnapshot: () => {
      const snapshot: Record<string, CircuitBreakerState> = {};
      for (const [name, breaker] of breakers) {
        snapshot[name] = breaker.getStateSnapshot();
      }
      return snapshot;
    },
  };
}
