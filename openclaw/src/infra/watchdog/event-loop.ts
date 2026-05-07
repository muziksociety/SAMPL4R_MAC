/**
 * Event Loop Lag Monitoring
 *
 * Uses Node's built-in perf_hooks.monitorEventLoopDelay() to track event loop
 * responsiveness. If the event loop is blocked for too long, the service is
 * considered unhealthy.
 */
import { monitorEventLoopDelay, type IntervalHistogram } from "node:perf_hooks";

export type EventLoopMonitorOptions = {
  /** Lag threshold in milliseconds. Default: 100ms */
  thresholdMs?: number;
  /** Sample resolution in milliseconds. Default: 20ms */
  resolutionMs?: number;
};

export type EventLoopMonitorState = {
  /** Current p99 lag in milliseconds */
  lagP99Ms: number;
  /** Maximum recorded lag in milliseconds */
  maxLagMs: number;
  /** Whether the event loop is considered healthy (lag below threshold) */
  healthy: boolean;
};

export class EventLoopMonitor {
  private readonly histogram: IntervalHistogram;
  private readonly thresholdMs: number;
  private started = false;

  constructor(opts: EventLoopMonitorOptions = {}) {
    this.thresholdMs = opts.thresholdMs ?? 100;
    const resolutionMs = opts.resolutionMs ?? 20;

    // Create histogram with specified resolution
    this.histogram = monitorEventLoopDelay({ resolution: resolutionMs });
    // Use unref() so this doesn't keep the process alive.
    // The IntervalHistogram has an internal timer that can be unref'd,
    // but the TypeScript types don't expose it directly. Cast to access it.
    const histogramWithTimer = this.histogram as unknown as { unref?: () => void };
    histogramWithTimer.unref?.();
  }

  /**
   * Start monitoring the event loop.
   */
  start(): void {
    if (this.started) {
      return;
    }
    this.histogram.enable();
    this.started = true;
  }

  /**
   * Stop monitoring the event loop.
   */
  stop(): void {
    if (!this.started) {
      return;
    }
    this.histogram.disable();
    this.started = false;
  }

  /**
   * Reset the histogram data (useful after recovery).
   */
  reset(): void {
    this.histogram.reset();
  }

  /**
   * Check if the event loop is responsive (lag below threshold).
   */
  isHealthy(): boolean {
    if (!this.started) {
      // If not started, assume healthy (we can't measure)
      return true;
    }
    const p99Ms = this.getP99Ms();
    return p99Ms < this.thresholdMs;
  }

  /**
   * Get the p99 event loop lag in milliseconds.
   * This represents the delay that 99% of iterations complete within.
   */
  getP99Ms(): number {
    // Histogram values are in nanoseconds
    const p99Ns = this.histogram.percentile(99);
    return p99Ns / 1_000_000;
  }

  /**
   * Get the maximum recorded event loop lag in milliseconds.
   */
  getMaxMs(): number {
    return this.histogram.max / 1_000_000;
  }

  /**
   * Get the minimum recorded event loop lag in milliseconds.
   */
  getMinMs(): number {
    return this.histogram.min / 1_000_000;
  }

  /**
   * Get the mean event loop lag in milliseconds.
   */
  getMeanMs(): number {
    return this.histogram.mean / 1_000_000;
  }

  /**
   * Get the configured threshold in milliseconds.
   */
  getThresholdMs(): number {
    return this.thresholdMs;
  }

  /**
   * Get a snapshot of the current monitor state.
   */
  getState(): EventLoopMonitorState {
    return {
      lagP99Ms: Math.round(this.getP99Ms() * 100) / 100,
      maxLagMs: Math.round(this.getMaxMs() * 100) / 100,
      healthy: this.isHealthy(),
    };
  }
}
