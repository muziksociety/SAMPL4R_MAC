/**
 * Escalating Restart Delay Strategy
 *
 * Implements progressive backoff for crash recovery:
 * - First crash: 1s delay
 * - Second crash: 2s delay
 * - Third crash: 5s delay
 * - Fourth crash: 15s delay
 * - Fifth crash: 30s delay
 * - Subsequent crashes: 60s delay (max)
 *
 * Resets to initial delay after a stable period (default 5 minutes without crash).
 */

export type EscalationConfig = {
  /** Delay sequence in milliseconds. Default: [1000, 2000, 5000, 15000, 30000, 60000] */
  delays?: number[];
  /** Time without crashes before resetting to initial delay (ms). Default: 300000 (5 min) */
  stablePeriodMs?: number;
};

export type EscalationState = {
  /** Current crash count (0 = no recent crashes) */
  crashCount: number;
  /** Timestamp of the last crash (ms since epoch), or null if none */
  lastCrashAt: number | null;
  /** Next delay that will be returned if a crash occurs */
  nextDelayMs: number;
  /** Whether the system is currently in a stable period */
  stable: boolean;
};

const DEFAULT_DELAYS = [1000, 2000, 5000, 15000, 30000, 60000];
const DEFAULT_STABLE_PERIOD_MS = 5 * 60 * 1000; // 5 minutes

export class EscalationStrategy {
  private readonly delays: number[];
  private readonly stablePeriodMs: number;
  private crashCount = 0;
  private lastCrashAt: number | null = null;

  constructor(config: EscalationConfig = {}) {
    this.delays = config.delays ?? DEFAULT_DELAYS;
    this.stablePeriodMs = config.stablePeriodMs ?? DEFAULT_STABLE_PERIOD_MS;
    if (this.delays.length === 0) {
      throw new Error("EscalationStrategy: delays array cannot be empty");
    }
  }

  /**
   * Record that a crash occurred and get the delay before restarting.
   * Returns the delay in milliseconds.
   */
  recordCrash(): number {
    const now = Date.now();

    // Check if we should reset due to stable period
    if (this.lastCrashAt !== null) {
      const timeSinceLastCrash = now - this.lastCrashAt;
      if (timeSinceLastCrash >= this.stablePeriodMs) {
        this.crashCount = 0;
      }
    }

    this.crashCount += 1;
    this.lastCrashAt = now;

    return this.getCurrentDelay();
  }

  /**
   * Record that the system has been stable (e.g., successful startup).
   * If called after a stable period has elapsed, resets the crash count.
   */
  recordStable(): void {
    const now = Date.now();
    if (this.lastCrashAt !== null) {
      const timeSinceLastCrash = now - this.lastCrashAt;
      if (timeSinceLastCrash >= this.stablePeriodMs) {
        this.crashCount = 0;
        this.lastCrashAt = null;
      }
    }
  }

  /**
   * Get the next delay that would be applied if a crash occurs now.
   * Does not modify state.
   */
  getNextDelay(): number {
    const now = Date.now();

    // Check if we would reset
    if (this.lastCrashAt !== null) {
      const timeSinceLastCrash = now - this.lastCrashAt;
      if (timeSinceLastCrash >= this.stablePeriodMs) {
        // Would reset, so next crash would be first
        return this.delays[0];
      }
    }

    // Otherwise, return the delay for the next crash count
    const nextIndex = Math.min(this.crashCount, this.delays.length - 1);
    return this.delays[nextIndex];
  }

  /**
   * Get the current delay based on crash count (internal use after recording crash).
   */
  private getCurrentDelay(): number {
    const index = Math.min(this.crashCount - 1, this.delays.length - 1);
    return this.delays[Math.max(0, index)];
  }

  /**
   * Reset the escalation state completely.
   */
  reset(): void {
    this.crashCount = 0;
    this.lastCrashAt = null;
  }

  /**
   * Check if the system is currently in a stable period.
   */
  isStable(): boolean {
    if (this.lastCrashAt === null) {
      return true;
    }
    return Date.now() - this.lastCrashAt >= this.stablePeriodMs;
  }

  /**
   * Get the current crash count.
   */
  getCrashCount(): number {
    return this.crashCount;
  }

  /**
   * Get the configured stable period in milliseconds.
   */
  getStablePeriodMs(): number {
    return this.stablePeriodMs;
  }

  /**
   * Get a snapshot of the current escalation state.
   */
  getState(): EscalationState {
    return {
      crashCount: this.crashCount,
      lastCrashAt: this.lastCrashAt,
      nextDelayMs: this.getNextDelay(),
      stable: this.isStable(),
    };
  }
}
