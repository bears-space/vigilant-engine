<template>
  <div class="container">
    <header class="topbar">
      <div class="title-block">
        <h1>⚡ Vigilant Engine</h1>
        <div class="device-row">
          <div class="device-name">{{ deviceName }}</div>
          <div class="ws-chip" :class="connectionOk ? 'ws-up' : 'ws-down'">
            <span class="ws-dot"></span>{{ connectionOk ? "WS Connected" : "WS Disconnected" }}
          </div>
        </div>
        <div class="status">{{ statusText }}</div>
      </div>
      <div class="actions">
        <button class="btn-danger" @click="showRecovery">
          ⚠️ Reboot to Recovery
        </button>
      </div>
    </header>

    <main class="layout">
      <section class="console-panel">
        <div class="console-header">
          <div>
            <div class="console-title">System Console</div>
            <div class="console-sub">Live stream from /ws</div>
          </div>
          <div class="legend">
            <span class="pill pill-info">Info</span>
            <span class="pill pill-warn">Warn</span>
            <span class="pill pill-error">Error</span>
          </div>
        </div>
        <pre ref="consoleEl" class="console" v-html="consoleHtml"></pre>
      </section>

      <aside class="sidebar">
        <h3>System Controls</h3>
        <button class="btn-danger" @click="showRecovery">
          ⚠️ Reboot to Recovery
        </button>
      </aside>
    </main>

    <div class="overlay" :class="{ active: overlayActive }">
      <div class="modal">
        <h2>⚠️ Booting to Recovery Mode</h2>

        <p>
          The ESP is currently booting to recovery, it will open a AP in the next few
          seconds. To enter recovery mode, you need to connect to the recovery access
          point:
        </p>

        <div class="credentials">
          <div><span class="label">Network (SSID):</span><span class="value">VE-Recovery</span></div>
          <div><span class="label">Password:</span><span class="value">starstreak</span></div>
        </div>

        <p>Once connected, the recovery interface will load automatically.</p>

        <button class="btn-primary" :disabled="proceeding" @click="proceed">
          <template v-if="proceeding">
            <span class="spinner"></span>Connecting...
          </template>
          <template v-else>
            Understood and Continue
          </template>
        </button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from "vue";

const MAX_LOG_LINES = 200;

const deviceName = ref("Vigilant ESP Test");
const statusText = ref("System Operational");

const overlayActive = ref(false);
const proceeding = ref(false);

const lines = ref<string[]>([]);
const consoleEl = ref<HTMLElement | null>(null);
const socket = ref<WebSocket | null>(null);
const reconnectHandle = ref<number | null>(null);
const reconnectAttempts = ref(0);
const pingTimer = ref<number | null>(null);
const heartbeatTimer = ref<number | null>(null);
const lastHeartbeat = ref<number>(0);
const connectionOk = ref(false);

const consoleHtml = computed(() =>
  lines.value
    .map((line) => `<span class="log-line ${levelClass(line)}">${escapeHtml(line)}</span>`)
    .join("\n")
);

function escapeHtml(s: string) {
  return s
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function levelClass(line: string) {
  const match = line.match(/^\s*([IWE])\s*\(/i);
  const level = match?.[1]?.toUpperCase();
  if (level === "W") return "log-warn";
  if (level === "E") return "log-error";
  if (level === "I") return "log-info";
  return "log-other";
}

function startsWithLevel(line: string) {
  return /^\s*[IWE]\s*\(/i.test(line);
}

function normalizeLogLines(rawLines: string[]): string[] {
  const normalized: string[] = [];

  for (const raw of rawLines) {
    const line = raw.replace(/\r?\n/g, "").trim();
    if (!line) continue; // drop empty fragments

    if (startsWithLevel(line) || normalized.length === 0) {
      normalized.push(line);
    } else {
      // continuation of previous line: glue with a space
      normalized[normalized.length - 1] = `${normalized[normalized.length - 1]} ${line}`;
    }
  }

  return normalized;
}

function splitAndNormalize(text: string) {
  return normalizeLogLines(text.split(/\r?\n/));
}

async function loadDeviceInfo() {
  const INFO_URL = "http://192.168.13.234/info";
  try {
    const res = await fetch(INFO_URL, { cache: "no-cache" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();
    if (typeof data?.name === "string" && data.name.trim()) {
      deviceName.value = data.name.trim();
    }
  } catch (err) {
    console.warn("Failed to load device info", err);
  }
}

const scrollConsoleToBottom = () => {
  if (consoleEl.value) {
    consoleEl.value.scrollTop = consoleEl.value.scrollHeight;
  }
};

async function pushLine(msg: string) {
  lines.value = [...lines.value, msg].slice(-MAX_LOG_LINES);
  await nextTick();
  scrollConsoleToBottom();
}

async function log(msg: string) {
  await pushLine(msg);
}

function handleLogPayload(raw: unknown) {
  if (!raw || typeof raw !== "object") return;
  const payload = raw as { type?: string; line?: unknown; lines?: unknown };

  if (payload.type === "logs" && Array.isArray(payload.lines)) {
    const normalized = normalizeLogLines(
      payload.lines.filter((line): line is string => typeof line === "string")
    );
    lines.value = normalized.slice(-MAX_LOG_LINES);
    nextTick().then(scrollConsoleToBottom);
    return;
  }

  if (payload.type === "log" && typeof payload.line === "string") {
    const merged = splitAndNormalize(payload.line);
    for (const l of merged) {
      pushLine(l);
    }
  }
}

function scheduleReconnect() {
  if (reconnectHandle.value !== null) return;
  socket.value = null;
  reconnectAttempts.value += 1;
  const delay = Math.min(1000 + reconnectAttempts.value * 1000, 2500);
  reconnectHandle.value = window.setTimeout(() => {
    reconnectHandle.value = null;
    connectLogStream();
  }, delay);
}

function connectLogStream() {
  if (reconnectHandle.value !== null) {
    clearTimeout(reconnectHandle.value);
    reconnectHandle.value = null;
  }

  if (socket.value) {
    try { socket.value.close(); } catch (_) {}
    socket.value = null;
  }

  if (pingTimer.value !== null) {
    clearInterval(pingTimer.value);
    pingTimer.value = null;
  }

  const protocol = window.location.protocol === "https:" ? "wss" : "ws";
  const ws = new WebSocket(`${protocol}://${window.location.host}/ws`);

  socket.value = ws;

  ws.addEventListener("open", () => {
    reconnectAttempts.value = 0;
    lastHeartbeat.value = performance.now();
    connectionOk.value = true;

    pingTimer.value = window.setInterval(() => {
      if (ws.readyState === WebSocket.OPEN) {
        try { ws.send('{\"type\":\"ping\"}'); } catch (_) {}
      }
    }, 200);

    if (heartbeatTimer.value === null) {
      heartbeatTimer.value = window.setInterval(() => {
        const diff = performance.now() - lastHeartbeat.value;
        const alive = diff <= 1000;
        connectionOk.value = alive;
        if (!alive && ws.readyState === WebSocket.OPEN) {
          try { ws.close(); } catch (_) {}
        }
      }, 100);
    }
  });

  ws.addEventListener("message", (event) => {
    if (typeof event.data !== "string") return;
    lastHeartbeat.value = performance.now();
    connectionOk.value = true;
    try {
      handleLogPayload(JSON.parse(event.data));
    } catch {
      // Ignore malformed frames
    }
  });

  ws.addEventListener("close", scheduleReconnect);
  ws.addEventListener("error", () => {
    scheduleReconnect();
    try { ws.close(); } catch (_) {}
  });
}

onMounted(() => {
  connectLogStream();
  loadDeviceInfo();
});

onBeforeUnmount(() => {
  if (reconnectHandle.value !== null) {
    clearTimeout(reconnectHandle.value);
    reconnectHandle.value = null;
  }
  if (pingTimer.value !== null) {
    clearInterval(pingTimer.value);
    pingTimer.value = null;
  }
  if (heartbeatTimer.value !== null) {
    clearInterval(heartbeatTimer.value);
    heartbeatTimer.value = null;
  }
  if (socket.value) {
    socket.value.close();
    socket.value = null;
  }
});

function showRecovery() {
  overlayActive.value = true;

  fetch("/rebootfactory", { method: "GET" })
    .then(() => log("Rebooting to recovery mode..."))
    .catch((e) => log("Warning: " + (e?.message ?? String(e))));
}

async function proceed() {
  proceeding.value = true;
  await log("Waiting for recovery AP...");

  let attempts = 0;

  const check = async () => {
    try {
      await fetch("http://192.168.4.1", { mode: "no-cors", cache: "no-cache" });
      window.location.href = "http://192.168.4.1";
    } catch (e) {
      attempts++;
      if (attempts < 60) {
        setTimeout(check, 1000);
      } else {
        await log("❌ Could not reach recovery interface");
        overlayActive.value = false;
        proceeding.value = false;
      }
    }
  };

  check();
}
</script>

<style scoped>
.container {
  max-width: 1280px;
  width: 100%;
  padding: 24px;
  display: flex;
  flex-direction: column;
  gap: 20px;
  height: calc(100vh - 32px); /* body padding 16px x2 */
}

h1 {
  font-size: 2.1rem;
  font-weight: 700;
  letter-spacing: -0.02em;
  margin-bottom: 6px;
  background: linear-gradient(135deg, #60a5fa, #3b82f6);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.device-name {
  font-size: 0.8125rem;
  color: #60a5fa;
  font-weight: 600;
  letter-spacing: 0.15em;
  text-transform: uppercase;
  opacity: 0.8;
  border-left: 3px solid #3b82f6;
  padding-left: 12px;
}

.device-row {
  display: flex;
  gap: 10px;
  align-items: center;
  margin-bottom: 12px;
  flex-wrap: wrap;
}

.ws-chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 10px;
  border-radius: 999px;
  font-size: 0.75rem;
  letter-spacing: 0.05em;
  border: 1px solid #1f2937;
}

.ws-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
}

.ws-up {
  background: rgba(16, 185, 129, 0.12);
  color: #34d399;
}
.ws-up .ws-dot { background: #34d399; }

.ws-down {
  background: rgba(248, 113, 113, 0.12);
  color: #f87171;
}
.ws-down .ws-dot { background: #f87171; }

.status {
  font-size: 0.9rem;
  color: #9ca3af;
  opacity: 0.9;
}

.topbar {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 16px;
}

.title-block { display: flex; flex-direction: column; gap: 4px; }

.actions { display: flex; gap: 12px; }

.layout {
  display: grid;
  grid-template-columns: 1fr minmax(240px, 320px);
  gap: 20px;
  align-items: stretch;
  flex: 1;
  min-height: 0;
}

.console-panel,
.sidebar {
  background: linear-gradient(145deg, #141922, #0f1319);
  border: 1px solid #1f2937;
  border-radius: 10px;
  padding: 20px;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.4);
}

.sidebar h3 {
  font-size: 0.85rem;
  color: #9ca3af;
  margin-bottom: 16px;
  text-transform: uppercase;
  letter-spacing: 0.1em;
  font-weight: 600;
}

.btn-danger {
  width: 100%;
  padding: 14px;
  font: inherit;
  font-size: 0.875rem;
  font-weight: 600;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.2s;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  background: #dc2626;
  color: #fff;
}

.btn-danger:hover {
  background: #b91c1c;
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(220, 38, 38, 0.4);
}

.console-panel { display: flex; flex-direction: column; gap: 10px; }

.console-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.console-title {
  font-size: 0.9rem;
  color: #e5e7eb;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.console-sub { font-size: 0.75rem; color: #9ca3af; }

.legend { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }

.pill {
  padding: 6px 10px;
  border-radius: 999px;
  font-size: 0.75rem;
  font-weight: 700;
  letter-spacing: 0.05em;
  border: 1px solid #1f2937;
}

.pill-info { background: rgba(16, 185, 129, 0.12); color: #34d399; }
.pill-warn { background: rgba(234, 179, 8, 0.12); color: #facc15; }
.pill-error { background: rgba(248, 113, 113, 0.12); color: #f87171; }

.console {
  background: #0d1117;
  border: 1px solid #1f2937;
  border-radius: 8px;
  padding: 14px;
  font-size: 0.84rem;
  line-height: 1.05;
  min-height: 220px;
  max-height: 60vh;
  color: #e5e7eb;
  overflow-y: auto;
  overflow-x: auto;
  animation: fadeIn 0.3s;
  white-space: pre-wrap;
}

:deep(.log-line) { display: block; padding: 0; margin: 0; line-height: 1.05; }
:deep(.log-info) { color: #34d399; }
:deep(.log-warn) { color: #fbbf24; }
:deep(.log-error) { color: #f87171; }
:deep(.log-other) { color: #9ca3af; }

.overlay {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background: rgba(0, 0, 0, 0.9);
  display: none;
  align-items: center;
  justify-content: center;
  z-index: 1000;
  backdrop-filter: blur(4px);
  animation: overlayFade 0.3s;
}

.overlay.active { display: flex; }

.modal {
  background: linear-gradient(145deg, #1a1f2e, #141922);
  border: 1px solid #374151;
  border-radius: 12px;
  padding: 40px;
  max-width: 480px;
  width: 90%;
  box-shadow: 0 25px 80px rgba(0, 0, 0, 0.8);
  animation: modalSlide 0.3s;
}

.modal h2 {
  font-size: 1.5rem;
  color: #fbbf24;
  margin-bottom: 20px;
  display: flex;
  align-items: center;
  gap: 12px;
}

.modal p { line-height: 1.6; margin-bottom: 16px; }

.credentials {
  background: #0d1117;
  border: 1px solid #374151;
  border-radius: 6px;
  padding: 16px;
  margin: 20px 0;
}

.credentials div {
  display: flex;
  justify-content: space-between;
  margin-bottom: 8px;
  font-size: 0.875rem;
}

.credentials div:last-child { margin-bottom: 0; }

.label { color: #6b7280; }
.value { color: #60a5fa; font-weight: 600; }

.btn-primary {
  width: 100%;
  padding: 14px;
  font: inherit;
  font-size: 0.875rem;
  font-weight: 600;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.2s;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  background: #3b82f6;
  color: #fff;
}

.btn-primary:hover {
  background: #2563eb;
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(59, 130, 246, 0.4);
}

.btn-primary:disabled {
  background: #374151;
  cursor: not-allowed;
  transform: none;
  box-shadow: none;
}

.spinner {
  display: inline-block;
  width: 14px;
  height: 14px;
  border: 2px solid rgba(255, 255, 255, 0.3);
  border-top-color: #fff;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
  margin-right: 8px;
}
</style>
