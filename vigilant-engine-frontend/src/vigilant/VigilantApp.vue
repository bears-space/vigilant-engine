<template>
  <div class="container">
    <h1>⚡ Vigilant Engine</h1>
    <div class="device-name">{{ deviceName }}</div>
    <div class="status"><span class="dot"></span>{{ statusText }}</div>

    <div class="dashboard">
      <div class="sidebar">
        <h3>System Controls</h3>
        <button class="btn-danger" @click="showRecovery">
          ⚠️ Reboot to Recovery
        </button>
      </div>

      <div class="main-panel">
        <div class="console-label">System Console</div>
        <pre ref="consoleEl" class="console" v-html="consoleHtml"></pre>
      </div>
    </div>

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
import { computed, nextTick, ref } from "vue";

const deviceName = "Vigilant ESP Test";
const statusText = "System Operational";

const overlayActive = ref(false);
const proceeding = ref(false);

const lines = ref<string[]>([
  "System initialized",
  "All modules loaded",
  "Ready for operation",
]);

const consoleEl = ref<HTMLElement | null>(null);

const consoleHtml = computed(() =>
  lines.value.map((l) => `<span class="dot"></span>${escapeHtml(l)}`).join("\n")
);

function escapeHtml(s: string) {
  return s
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

async function log(msg: string) {
  lines.value.push(msg);
  await nextTick();
  if (consoleEl.value) {
    consoleEl.value.scrollTop = consoleEl.value.scrollHeight;
  }
}

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
  max-width: 900px;
  width: 100%;
  padding: 32px;
}

h1 {
  font-size: 2rem;
  font-weight: 700;
  letter-spacing: -0.02em;
  margin-bottom: 4px;
  background: linear-gradient(135deg, #60a5fa, #3b82f6);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.device-name {
  font-size: 0.8125rem;
  color: #60a5fa;
  margin-bottom: 20px;
  font-weight: 600;
  letter-spacing: 0.15em;
  text-transform: uppercase;
  opacity: 0.8;
  border-left: 3px solid #3b82f6;
  padding-left: 12px;
}

.status {
  font-size: 0.875rem;
  color: #10b981;
  margin-bottom: 32px;
  opacity: 0.9;
}

.dashboard {
  display: grid;
  grid-template-columns: 300px 1fr;
  gap: 20px;
  align-items: start;
}

@media (max-width: 768px) {
  .dashboard { grid-template-columns: 1fr; }
}

.sidebar,
.main-panel {
  background: linear-gradient(145deg, #141922, #0f1319);
  border: 1px solid #1f2937;
  border-radius: 8px;
  padding: 24px;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.4);
}

.sidebar h3 {
  font-size: 0.875rem;
  color: #6b7280;
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

.console-label {
  font-size: 0.75rem;
  color: #6b7280;
  margin-bottom: 8px;
  text-transform: uppercase;
  letter-spacing: 0.1em;
}

.console {
  background: #0d1117;
  border: 1px solid #1f2937;
  border-radius: 6px;
  padding: 16px;
  font-size: 0.8125rem;
  line-height: 1.5;
  min-height: 200px;
  color: #10b981;
  overflow-x: auto;
  animation: fadeIn 0.3s;
  white-space: pre-wrap;
}

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
