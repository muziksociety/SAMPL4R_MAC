/**
 * systemd Watchdog Integration (STUB)
 *
 * This module provides integration with systemd's service supervision features.
 * It is currently a stub with detailed documentation for future implementation.
 *
 * ## What is sd-notify?
 *
 * sd-notify is a protocol for communicating with systemd, the init system used
 * by most Linux distributions. It allows services to:
 *
 * - Signal when they're ready to accept connections (READY=1)
 * - Send periodic "I'm alive" heartbeats (WATCHDOG=1)
 * - Notify when starting a graceful shutdown (STOPPING=1)
 * - Report status messages (STATUS=<message>)
 * - Extend startup/shutdown timeouts (EXTEND_TIMEOUT_USEC=<microseconds>)
 *
 * ## How it works
 *
 * When a service is configured with `Type=notify` in its systemd unit file,
 * systemd waits for a READY=1 notification before considering the service started.
 *
 * When `WatchdogSec=` is set, systemd expects periodic WATCHDOG=1 notifications.
 * If the service fails to send these within the timeout, systemd will restart it.
 *
 * Communication happens over a Unix socket (NOTIFY_SOCKET environment variable).
 *
 * ## Unit file example
 *
 * ```ini
 * [Unit]
 * Description=OpenClaw Gateway
 * After=network.target
 *
 * [Service]
 * Type=notify
 * ExecStart=/usr/bin/openclaw gateway start
 * WatchdogSec=30
 * Restart=on-failure
 * RestartSec=5s
 *
 * [Install]
 * WantedBy=multi-user.target
 * ```
 *
 * ## Key signals
 *
 * - `READY=1` - Service is ready to accept connections. Send after initialization.
 * - `WATCHDOG=1` - Heartbeat signal. Send at least every WatchdogSec/2.
 * - `STOPPING=1` - Graceful shutdown initiated. Send before cleanup starts.
 * - `STATUS=<msg>` - Human-readable status (e.g., "Processing requests").
 * - `WATCHDOG=trigger` - Ask systemd to restart the service immediately.
 *
 * ## Implementation with sd-notify package
 *
 * The `sd-notify` npm package provides a simple interface:
 *
 * ```typescript
 * import sdNotify from 'sd-notify';
 *
 * // After initialization:
 * sdNotify.ready();
 *
 * // Periodic heartbeat (start an interval):
 * if (sdNotify.watchdogInterval()) {
 *   const interval = Math.floor(sdNotify.watchdogInterval() / 2000);
 *   setInterval(() => sdNotify.watchdog(), interval);
 * }
 *
 * // Before shutdown:
 * sdNotify.stopping();
 * ```
 *
 * Package: https://www.npmjs.com/package/sd-notify
 *
 * ## Environment requirements
 *
 * - `NOTIFY_SOCKET` must be set by systemd
 * - Process must have write access to the socket
 * - `WATCHDOG_USEC` contains the watchdog interval in microseconds
 * - `WATCHDOG_PID` may contain the PID that should send notifications
 *
 * ## When to implement
 *
 * This should be implemented when deploying OpenClaw as a system service on Linux,
 * particularly for production deployments where reliability is critical.
 */

export type SystemdNotifyState = {
  /** Whether systemd notify is available (NOTIFY_SOCKET set) */
  available: boolean;
  /** Watchdog interval in milliseconds, or null if not configured */
  watchdogIntervalMs: number | null;
  /** Whether we've sent READY=1 */
  readySent: boolean;
  /** Whether we've sent STOPPING=1 */
  stoppingSent: boolean;
};

/**
 * STUB: SystemdNotify interface for future implementation.
 *
 * When implementing, replace this stub with actual sd-notify integration.
 */
export interface SystemdNotify {
  /**
   * Check if systemd notify is available (NOTIFY_SOCKET is set).
   */
  isAvailable(): boolean;

  /**
   * Get the watchdog interval in milliseconds, or null if not configured.
   */
  getWatchdogIntervalMs(): number | null;

  /**
   * Signal that the service is ready (READY=1).
   * Call this after initialization is complete.
   */
  ready(): void;

  /**
   * Send a watchdog heartbeat (WATCHDOG=1).
   * Call this periodically, at least every watchdogIntervalMs/2.
   */
  watchdog(): void;

  /**
   * Signal that the service is stopping (STOPPING=1).
   * Call this before beginning graceful shutdown.
   */
  stopping(): void;

  /**
   * Send a status message (STATUS=<msg>).
   * @param message Human-readable status message
   */
  status(message: string): void;

  /**
   * Request systemd to restart the service (WATCHDOG=trigger).
   */
  triggerRestart(): void;

  /**
   * Get the current state of the systemd integration.
   */
  getState(): SystemdNotifyState;

  /**
   * Start automatic watchdog heartbeats.
   * Returns a cleanup function to stop the heartbeats.
   */
  startWatchdogHeartbeat(): () => void;
}

/**
 * STUB: Create a no-op SystemdNotify instance.
 *
 * This implementation does nothing but provides the interface.
 * Replace with actual implementation when sd-notify is added as a dependency.
 *
 * Future implementation:
 * ```typescript
 * import sdNotify from 'sd-notify';
 *
 * export function createSystemdNotify(): SystemdNotify {
 *   let readySent = false;
 *   let stoppingSent = false;
 *
 *   return {
 *     isAvailable: () => Boolean(process.env.NOTIFY_SOCKET),
 *     getWatchdogIntervalMs: () => {
 *       const usec = sdNotify.watchdogInterval();
 *       return usec ? usec / 1000 : null;
 *     },
 *     ready: () => { sdNotify.ready(); readySent = true; },
 *     watchdog: () => sdNotify.watchdog(),
 *     stopping: () => { sdNotify.stopping(); stoppingSent = true; },
 *     status: (msg) => sdNotify.sendStatus(msg),
 *     triggerRestart: () => sdNotify.sendNotify('WATCHDOG=trigger'),
 *     getState: () => ({
 *       available: Boolean(process.env.NOTIFY_SOCKET),
 *       watchdogIntervalMs: sdNotify.watchdogInterval() / 1000 || null,
 *       readySent,
 *       stoppingSent,
 *     }),
 *     startWatchdogHeartbeat: () => {
 *       const intervalMs = sdNotify.watchdogInterval();
 *       if (!intervalMs) return () => {};
 *       const timer = setInterval(() => sdNotify.watchdog(), intervalMs / 2000);
 *       timer.unref();
 *       return () => clearInterval(timer);
 *     },
 *   };
 * }
 * ```
 */
export function createSystemdNotify(): SystemdNotify {
  // STUB: No-op implementation
  return {
    isAvailable: () => false,
    getWatchdogIntervalMs: () => null,
    ready: () => {
      /* no-op */
    },
    watchdog: () => {
      /* no-op */
    },
    stopping: () => {
      /* no-op */
    },
    status: (_message: string) => {
      /* no-op */
    },
    triggerRestart: () => {
      /* no-op */
    },
    getState: () => ({
      available: false,
      watchdogIntervalMs: null,
      readySent: false,
      stoppingSent: false,
    }),
    startWatchdogHeartbeat: () => () => {
      /* no-op */
    },
  };
}
