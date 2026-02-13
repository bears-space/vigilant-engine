<template>
  <div class="container">
    <h2>⚡ Vigilant Engine</h2>
    <div class="subtitle">⚠️ Recovery Mode</div>

    <label for="f" class="file-input">
      <span>{{ fileLabel }}</span>
    </label>
    <input id="f" ref="fileEl" type="file" accept=".bin" @change="onPick" />

    <div class="btn-group">
      <button class="primary" @click="uploadAndBoot">Upload &amp; Boot</button>
      <button class="secondary" @click="bootNow">Boot Now</button>
    </div>

    <pre><span class="dot"></span>{{ status }}</pre>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from "vue";

const fileEl = ref<HTMLInputElement | null>(null);
const picked = ref<File | null>(null);

const status = ref("Ready");

const fileLabel = computed(() =>
  picked.value ? `✓ ${picked.value.name}` : "📁 Select main.bin file"
);

function onPick() {
  const f = fileEl.value?.files?.[0] ?? null;
  picked.value = f;
}

async function uploadAndBoot() {
  const f = picked.value;
  if (!f) {
    status.value = "❌ No file selected";
    return;
  }

  status.value = `⏳ Uploading ${f.name} (${f.size} bytes)...`;

  try {
    const buf = await f.arrayBuffer();
    const r = await fetch("/update", {
      method: "POST",
      headers: { "Content-Type": "application/octet-stream" },
      body: buf,
    });
    status.value = `✓ ${await r.text()}`;
  } catch (e: any) {
    status.value = `❌ Error: ${e?.message ?? String(e)}`;
  }
}

async function bootNow() {
  status.value = "⏳ Booting...";
  fetch("/boot", { method: "POST" })
    .then((r) => r.text())
    .then((t) => (status.value = `✓ ${t}`))
    .catch((e) => (status.value = `❌ Error: ${e?.message ?? String(e)}`));
}
</script>

<style scoped>
.container {
  max-width: 480px;
  width: 100%;
  background: linear-gradient(145deg, #141922, #0f1319);
  border: 1px solid #1f2937;
  border-radius: 8px;
  padding: 32px;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.6);
}

h2 {
  font-size: 1.5rem;
  font-weight: 700;
  letter-spacing: -0.02em;
  margin-bottom: 8px;
  background: linear-gradient(135deg, #60a5fa, #3b82f6);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.subtitle {
  font-size: 0.875rem;
  color: #ef4444;
  margin-bottom: 24px;
  font-weight: 600;
}

.file-input {
  display: block;
  width: 100%;
  padding: 16px;
  background: #1a1f2e;
  border: 2px dashed #374151;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.2s;
  margin-bottom: 16px;
  text-align: center;
  font-size: 0.875rem;
}

.file-input:hover {
  border-color: #3b82f6;
  background: #1f2937;
}

input[type="file"] {
  display: none;
}

.btn-group {
  display: flex;
  gap: 12px;
  margin-bottom: 20px;
}

button {
  flex: 1;
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
}

.primary {
  background: #3b82f6;
  color: #fff;
}

.primary:hover {
  background: #2563eb;
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(59, 130, 246, 0.4);
}

.secondary {
  background: #1f2937;
  color: #d4d4d8;
  border: 1px solid #374151;
}

.secondary:hover {
  background: #374151;
}

pre {
  background: #0d1117;
  border: 1px solid #1f2937;
  border-radius: 6px;
  padding: 16px;
  font-size: 0.8125rem;
  line-height: 1.5;
  min-height: 60px;
  color: #10b981;
  overflow-x: auto;
  animation: fadeIn 0.3s;
}
</style>
