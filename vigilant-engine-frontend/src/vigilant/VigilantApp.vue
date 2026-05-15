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
    </header>

    <main class="workspace">
      <nav class="tabs" aria-label="Primary sections">
        <button
          v-for="tab in tabs"
          :key="tab.id"
          type="button"
          class="tab"
          :class="{ active: activeTab === tab.id }"
          @click="activeTab = tab.id"
        >
          {{ tab.label }}
        </button>
      </nav>

      <section v-if="activeTab === 'console'" class="tab-panel console-panel">
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

      <section v-else-if="activeTab === 'connected-devices'" class="tab-panel connected-panel">
        <div class="connected-list">
          <div class="connected-list-header">
            <div class="connected-section-title">Connected Devices</div>
          </div>

          <template v-if="connectedDevices.length">
            <div v-if="addedConnectedDevices.length" class="connected-list-group">
              <div class="connected-subsection-title">Added Devices</div>

              <button
                v-for="device in addedConnectedDevices"
                :key="device.id"
                type="button"
                class="connected-device"
                :class="{ active: activeConnectedDevice?.id === device.id }"
                :aria-pressed="selectedConnectedDeviceId === device.id"
                @click="selectedConnectedDeviceId = device.id"
                @mouseenter="hoveredConnectedDeviceId = device.id"
                @mouseleave="hoveredConnectedDeviceId = null"
                @focus="hoveredConnectedDeviceId = device.id"
                @blur="hoveredConnectedDeviceId = null"
              >
                <div class="connected-device-copy">
                  <div class="connected-device-name">{{ device.name }}</div>
                </div>
                <div class="connected-device-badges">
                  <span
                    class="device-state-pill"
                    :class="deviceStatePillClass(device)"
                  >
                    {{ deviceStateLabel(device) }}
                  </span>
                  <span
                    class="protocol-pill"
                    :class="protocolPillClass(device.protocol)"
                  >
                    {{ protocolLabel(device.protocol) }}
                  </span>
                </div>
              </button>
            </div>

            <div v-if="detectedOnlyConnectedDevices.length" class="connected-list-group">
              <div class="connected-subsection-title">Detected Devices</div>

              <button
                v-for="device in detectedOnlyConnectedDevices"
                :key="device.id"
                type="button"
                class="connected-device"
                :class="{ active: activeConnectedDevice?.id === device.id }"
                :aria-pressed="selectedConnectedDeviceId === device.id"
                @click="selectedConnectedDeviceId = device.id"
                @mouseenter="hoveredConnectedDeviceId = device.id"
                @mouseleave="hoveredConnectedDeviceId = null"
                @focus="hoveredConnectedDeviceId = device.id"
                @blur="hoveredConnectedDeviceId = null"
              >
                <div class="connected-device-copy">
                  <div class="connected-device-name">{{ device.name }}</div>
                </div>
                <div class="connected-device-badges">
                  <span
                    class="device-state-pill"
                    :class="deviceStatePillClass(device)"
                  >
                    {{ deviceStateLabel(device) }}
                  </span>
                  <span
                    class="protocol-pill"
                    :class="protocolPillClass(device.protocol)"
                  >
                    {{ protocolLabel(device.protocol) }}
                  </span>
                </div>
              </button>
            </div>

            <div v-if="wifiConnectedDevices.length" class="connected-list-group">
              <div class="connected-subsection-title">WiFi Clients</div>

              <button
                v-for="device in wifiConnectedDevices"
                :key="device.id"
                type="button"
                class="connected-device"
                :class="{ active: activeConnectedDevice?.id === device.id }"
                :aria-pressed="selectedConnectedDeviceId === device.id"
                @click="selectedConnectedDeviceId = device.id"
                @mouseenter="hoveredConnectedDeviceId = device.id"
                @mouseleave="hoveredConnectedDeviceId = null"
                @focus="hoveredConnectedDeviceId = device.id"
                @blur="hoveredConnectedDeviceId = null"
              >
                <div class="connected-device-copy">
                  <div class="connected-device-name">{{ device.name }}</div>
                  <div v-if="device.addressIp" class="connected-device-sub">
                    {{ device.addressIp }}
                  </div>
                </div>
                <div class="connected-device-badges">
                  <span
                    class="device-state-pill"
                    :class="deviceStatePillClass(device)"
                  >
                    {{ deviceStateLabel(device) }}
                  </span>
                  <span
                    class="protocol-pill"
                    :class="protocolPillClass(device.protocol)"
                  >
                    {{ protocolLabel(device.protocol) }}
                  </span>
                </div>
              </button>
            </div>
          </template>

          <div v-else class="connected-empty">
            No connected devices reported.
          </div>
        </div>

        <div v-if="activeConnectedDevice" class="connected-detail">
          <div class="connected-detail-header">
            <div class="connected-detail-name">{{ activeConnectedDevice.name }}</div>
            <div class="connected-device-badges">
              <span
                class="device-state-pill"
                :class="deviceStatePillClass(activeConnectedDevice)"
              >
                {{ deviceStateLabel(activeConnectedDevice) }}
              </span>
              <span
                class="protocol-pill"
                :class="protocolPillClass(activeConnectedDevice.protocol)"
              >
                {{ protocolLabel(activeConnectedDevice.protocol) }}
              </span>
            </div>
          </div>

          <dl class="connected-detail-grid">
            <div
              v-for="detail in activeConnectedDeviceDetails"
              :key="detail.label"
              class="connected-detail-row"
            >
              <dt>{{ detail.label }}</dt>
              <dd>{{ detail.value }}</dd>
            </div>
          </dl>

          <div v-if="activeConnectedDevice.canManageRecovery" class="connected-actions">
            <button
              type="button"
              class="btn-danger connected-action"
              :disabled="remoteRecoveryBusyId === activeConnectedDevice.id"
              @click="rebootConnectedDeviceToRecovery(activeConnectedDevice)"
            >
              {{
                remoteRecoveryBusyId === activeConnectedDevice.id
                  ? "Requesting Recovery..."
                  : "Reboot to Recovery"
              }}
            </button>
            <div
              v-if="remoteActionDeviceId === activeConnectedDevice.id && remoteActionMessage"
              class="connected-action-status"
            >
              {{ remoteActionMessage }}
            </div>
          </div>
        </div>

        <div v-else class="connected-detail connected-empty-panel">
          <div class="connected-section-title">Device Details</div>
          <div class="connected-empty">
            No connected devices are currently available.
          </div>
        </div>
      </section>

      <section v-else-if="activeTab === 'settings'" class="tab-panel settings-panel">
        <div class="settings-group">
          <div class="settings-group-title">Device Settings</div>
          <button class="btn-danger settings-action" @click="showRecovery">
            ⚠️ Reboot to Recovery
          </button>
        </div>
      </section>

      <section v-else class="tab-panel tab-panel-empty"></section>
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
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from "vue";

type ProtocolId = "i2c" | "spi" | "canfd" | "wifi";
type DeviceState = "added" | "detected" | "vigilant" | "other" | "unknown";
type DeviceGroup = "added" | "detected" | "wifi";
type ConnectedDevice = {
  id: string;
  name: string;
  protocol: ProtocolId;
  state: DeviceState;
  group: DeviceGroup;
  details: Array<{ label: string; value: string }>;
  addressIp?: string;
  remoteMac?: string;
  isVigilantDevice?: boolean;
  canManageRecovery?: boolean;
};
type I2cAddedApiDevice = {
  name?: unknown;
  address?: unknown;
  address_hex?: unknown;
  whoami_reg?: unknown;
  whoami_reg_hex?: unknown;
  expected_whoami?: unknown;
  expected_whoami_hex?: unknown;
};
type I2cDetectedApiDevice = {
  name?: unknown;
  address?: unknown;
  address_hex?: unknown;
};
type I2cInfoResponse = {
  enabled?: unknown;
  sda_io?: unknown;
  scl_io?: unknown;
  frequency_hz?: unknown;
  added_devices?: unknown;
  detected_devices?: unknown;
};
type WifiApiDevice = {
  is_vigilant_device?: unknown;
  identity?: unknown;
  name?: unknown;
  mac?: unknown;
  address?: unknown;
  address_ip?: unknown;
};
type WifiInfoResponse = {
  network_mode?: unknown;
  mac?: unknown;
  ap_ssid?: unknown;
  sta_ssid?: unknown;
  ip_sta?: unknown;
  ip_ap?: unknown;
  connected_devices?: unknown;
};
type RemoteVigilantInfoResponse = {
  name?: unknown;
  is_vigilant_device?: unknown;
  vigilant_magic?: unknown;
  network_mode?: unknown;
  mac?: unknown;
  ap_ssid?: unknown;
  sta_ssid?: unknown;
  ip_sta?: unknown;
  ip_ap?: unknown;
};
type RemoteInfoState = {
  loading: boolean;
  error?: string;
  data?: RemoteVigilantInfoResponse;
};

const MAX_LOG_LINES = 200;
const PING_INTERVAL_MS = 15000;
const HEARTBEAT_TIMEOUT_MS = 45000;
const tabs = [
  { id: "console", label: "Console" },
  { id: "connected-devices", label: "Connected Devices" },
  { id: "settings", label: "Settings" },
] as const;
type TabId = (typeof tabs)[number]["id"];

const deviceName = ref("Vigilant ESP Test");
const statusText = ref("System Operational");
const activeTab = ref<TabId>("console");
const connectedDevices = ref<ConnectedDevice[]>([]);
const selectedConnectedDeviceId = ref("");
const hoveredConnectedDeviceId = ref<string | null>(null);
const remoteWifiInfoByDeviceId = ref<Record<string, RemoteInfoState>>({});
const remoteRecoveryBusyId = ref<string | null>(null);
const remoteActionDeviceId = ref<string | null>(null);
const remoteActionMessage = ref("");

const overlayActive = ref(false);
const proceeding = ref(false);

const lines = ref<string[]>([]);
const consoleEl = ref<HTMLElement | null>(null);
const socket = ref<WebSocket | null>(null);
const reconnectHandle = ref<number | null>(null);
const reconnectAttempts = ref(0);
const pingTimer = ref<number | null>(null);
const lastHeartbeat = ref<number>(0);
const connectionOk = ref(false);
let consoleScrollQueued = false;

const consoleHtml = computed(() =>
  lines.value
    .map((line) => `<span class="log-line ${levelClass(line)}">${escapeHtml(line)}</span>`)
    .join("\n")
);
const addedConnectedDevices = computed(() =>
  connectedDevices.value.filter((device) => device.group === "added")
);
const detectedOnlyConnectedDevices = computed(() =>
  connectedDevices.value.filter((device) => device.group === "detected")
);
const wifiConnectedDevices = computed(() =>
  connectedDevices.value.filter((device) => device.group === "wifi")
);
const activeConnectedDevice = computed(
  () =>
    connectedDevices.value.find(
      (device) => device.id === (hoveredConnectedDeviceId.value ?? selectedConnectedDeviceId.value)
    ) ?? connectedDevices.value[0] ?? null
);
const activeConnectedDeviceDetails = computed(() => {
  const device = activeConnectedDevice.value;
  if (!device) return [];

  const details = [...device.details];
  if (device.protocol !== "wifi" || !device.isVigilantDevice) {
    return details;
  }

  const remoteInfo = remoteWifiInfoByDeviceId.value[device.id];
  if (!remoteInfo) {
    details.push({ label: "Vigilant Info", value: "Queued" });
    return details;
  }

  if (remoteInfo.loading) {
    details.push({ label: "Vigilant Info", value: "Loading" });
    return details;
  }

  if (remoteInfo.error) {
    details.push({ label: "Vigilant Info", value: remoteInfo.error });
    return details;
  }

  if (remoteInfo.data) {
    details.push(...mapRemoteVigilantInfoDetails(remoteInfo.data));
  }

  return details;
});

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

function protocolLabel(protocol: ProtocolId) {
  if (protocol === "canfd") return "CAN FD";
  if (protocol === "wifi") return "WiFi";
  return protocol.toUpperCase();
}

function protocolPillClass(protocol: ProtocolId) {
  return `protocol-${protocol}`;
}

function deviceStateLabel(device: ConnectedDevice) {
  if (device.state === "added") return "Added";
  if (device.state === "detected") return "Detected";
  if (device.state === "vigilant") return "Vigilant";
  if (device.state === "other") return "Other";
  return "Unknown";
}

function deviceStatePillClass(device: ConnectedDevice) {
  return `device-state-${device.state}`;
}

function asString(value: unknown) {
  return typeof value === "string" && value.trim() ? value : null;
}

function asNumber(value: unknown) {
  return typeof value === "number" && Number.isFinite(value) ? value : null;
}

function formatHexByte(value: number | null) {
  if (value === null) return "n/a";
  return `0x${value.toString(16).toUpperCase().padStart(2, "0")}`;
}

function networkModeLabel(value: unknown) {
  const mode = asNumber(value);
  if (mode === 0) return "AP";
  if (mode === 1) return "STA";
  if (mode === 2) return "APSTA";
  return "n/a";
}

function identityLabel(identity: string | null, isVigilantDevice: boolean) {
  if (isVigilantDevice || identity === "vigilant") return "Vigilant";
  if (identity === "other") return "Other";
  return "Unknown";
}

function formatI2cBusLabel(response: I2cInfoResponse) {
  const scl = asNumber(response.scl_io);
  const sda = asNumber(response.sda_io);
  const frequencyHz = asNumber(response.frequency_hz);
  const busParts = ["I2C0"];

  if (frequencyHz !== null) {
    busParts.push(`@ ${Math.round(frequencyHz / 1000)} kHz`);
  }

  if (scl !== null && sda !== null) {
    busParts.push(`SCL ${scl} / SDA ${sda}`);
  }

  return busParts.join(" ");
}

function mapI2cDevices(response: I2cInfoResponse): ConnectedDevice[] {
  if (response.enabled !== true) {
    return [];
  }

  const busLabel = formatI2cBusLabel(response);
  const addedDevices = Array.isArray(response.added_devices) ? response.added_devices : [];
  const detectedDevices = Array.isArray(response.detected_devices) ? response.detected_devices : [];
  const addedAddresses = new Set<string>();

  const mappedAddedDevices = addedDevices.flatMap((rawDevice) => {
    if (!rawDevice || typeof rawDevice !== "object") {
      return [];
    }

    const device = rawDevice as I2cAddedApiDevice;
    const address = asNumber(device.address);
    if (address === null) {
      return [];
    }

    const addressHex = asString(device.address_hex) ?? formatHexByte(address);
    addedAddresses.add(addressHex);
    const whoamiReg = asNumber(device.whoami_reg);
    const expectedWhoami = asNumber(device.expected_whoami);

    return [
      {
        id: `i2c-${addressHex.toLowerCase()}`,
        name: asString(device.name) ?? `I2C Device ${addressHex}`,
        protocol: "i2c",
        state: "added",
        group: "added",
        details: [
          { label: "Address", value: addressHex },
          { label: "Bus", value: busLabel },
          { label: "Registration", value: "Added through Vigilant API" },
          { label: "WHOAMI Reg", value: asString(device.whoami_reg_hex) ?? formatHexByte(whoamiReg) },
          { label: "Expected WHOAMI", value: asString(device.expected_whoami_hex) ?? formatHexByte(expectedWhoami) },
        ],
      },
    ];
  });

  const mappedDetectedDevices = detectedDevices.flatMap((rawDevice) => {
    if (!rawDevice || typeof rawDevice !== "object") {
      return [];
    }

    const device = rawDevice as I2cDetectedApiDevice;
    const address = asNumber(device.address);
    if (address === null) {
      return [];
    }

    const addressHex = asString(device.address_hex) ?? formatHexByte(address);
    if (addedAddresses.has(addressHex)) {
      return [];
    }

    return [
      {
        id: `i2c-detected-${addressHex.toLowerCase()}`,
        name: asString(device.name) ?? `Detected I2C Device ${addressHex}`,
        protocol: "i2c",
        state: "detected",
        group: "detected",
        details: [
          { label: "Address", value: addressHex },
          { label: "Bus", value: busLabel },
          { label: "Registration", value: "Detected on bus, not added" },
        ],
      },
    ];
  });

  return [...mappedAddedDevices, ...mappedDetectedDevices];
}

function mapWifiDevices(response: WifiInfoResponse): ConnectedDevice[] {
  const clients = Array.isArray(response.connected_devices) ? response.connected_devices : [];
  const apSsid = asString(response.ap_ssid);

  return clients.flatMap((rawDevice, index) => {
    if (!rawDevice || typeof rawDevice !== "object") {
      return [];
    }

    const device = rawDevice as WifiApiDevice;
    const mac = asString(device.mac);
    const addressIp = asString(device.address_ip);
    const identity = asString(device.identity);
    const isVigilantDevice = device.is_vigilant_device === true || identity === "vigilant";
    const state: DeviceState = isVigilantDevice
      ? "vigilant"
      : identity === "other"
        ? "other"
        : "unknown";
    const displayName =
      asString(device.name) ??
      (isVigilantDevice ? "Vigilant WiFi Device" : "WiFi Client");
    const idSuffix = (mac ?? addressIp ?? String(index)).toLowerCase();

    return [
      {
        id: `wifi-${idSuffix}`,
        name: displayName,
        protocol: "wifi",
        state,
        group: "wifi",
        addressIp: addressIp ?? undefined,
        remoteMac: mac ?? undefined,
        isVigilantDevice,
        canManageRecovery: isVigilantDevice && !!mac,
        details: [
          { label: "Identity", value: identityLabel(identity, isVigilantDevice) },
          { label: "Address", value: addressIp ?? "n/a" },
          { label: "MAC", value: mac ?? "n/a" },
          { label: "Access Point", value: apSsid ?? "n/a" },
          { label: "Registration", value: "Connected to SoftAP" },
          ...(isVigilantDevice
            ? [{ label: "Vigilant Magic", value: "vigilant-engine-device-v1" }]
            : []),
        ],
      },
    ];
  });
}

function mapRemoteVigilantInfoDetails(info: RemoteVigilantInfoResponse) {
  return [
    { label: "Remote Name", value: asString(info.name) ?? "n/a" },
    { label: "Remote MAC", value: asString(info.mac) ?? "n/a" },
    { label: "Network Mode", value: networkModeLabel(info.network_mode) },
    { label: "AP SSID", value: asString(info.ap_ssid) ?? "n/a" },
    { label: "STA SSID", value: asString(info.sta_ssid) ?? "n/a" },
    { label: "STA IP", value: asString(info.ip_sta) ?? "n/a" },
    { label: "AP IP", value: asString(info.ip_ap) ?? "n/a" },
    { label: "Magic", value: asString(info.vigilant_magic) ?? "n/a" },
  ];
}

async function loadDeviceInfo() {
  try {
    const res = await fetch("/info", { cache: "no-cache" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();
    if (typeof data?.name === "string" && data.name.trim()) {
      deviceName.value = data.name.trim();
    }
  } catch (err) {
    console.warn("Failed to load device info", err);
  }
}

async function loadConnectedDevices() {
  const [i2cResult, wifiResult] = await Promise.allSettled([
    fetch("/i2cinfo", { cache: "no-cache" }),
    fetch("/wifiinfo", { cache: "no-cache" }),
  ]);

  const devices: ConnectedDevice[] = [];

  if (i2cResult.status === "fulfilled") {
    try {
      if (!i2cResult.value.ok) throw new Error(`HTTP ${i2cResult.value.status}`);
      devices.push(...mapI2cDevices((await i2cResult.value.json()) as I2cInfoResponse));
    } catch (err) {
      console.warn("Failed to load I2C devices", err);
    }
  } else {
    console.warn("Failed to load I2C devices", i2cResult.reason);
  }

  if (wifiResult.status === "fulfilled") {
    try {
      if (!wifiResult.value.ok) throw new Error(`HTTP ${wifiResult.value.status}`);
      devices.push(...mapWifiDevices((await wifiResult.value.json()) as WifiInfoResponse));
    } catch (err) {
      console.warn("Failed to load WiFi devices", err);
    }
  } else {
    console.warn("Failed to load WiFi devices", wifiResult.reason);
  }

  connectedDevices.value = devices;
  hoveredConnectedDeviceId.value = null;

  const validDeviceIds = new Set(devices.map((device) => device.id));
  remoteWifiInfoByDeviceId.value = Object.fromEntries(
    Object.entries(remoteWifiInfoByDeviceId.value).filter(([id]) => validDeviceIds.has(id))
  );

  if (!devices.length) {
    selectedConnectedDeviceId.value = "";
    return;
  }

  if (!devices.some((device) => device.id === selectedConnectedDeviceId.value)) {
    selectedConnectedDeviceId.value = devices[0].id;
  }
}

async function loadRemoteWifiInfo(device: ConnectedDevice | null) {
  if (!device?.isVigilantDevice || !device.remoteMac) {
    return;
  }

  if (remoteWifiInfoByDeviceId.value[device.id]) {
    return;
  }

  remoteWifiInfoByDeviceId.value = {
    ...remoteWifiInfoByDeviceId.value,
    [device.id]: { loading: true },
  };

  try {
    const res = await fetch(`/wifi/deviceinfo?mac=${encodeURIComponent(device.remoteMac)}`, {
      cache: "no-cache",
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = (await res.json()) as RemoteVigilantInfoResponse;
    remoteWifiInfoByDeviceId.value = {
      ...remoteWifiInfoByDeviceId.value,
      [device.id]: { loading: false, data },
    };
  } catch (err) {
    remoteWifiInfoByDeviceId.value = {
      ...remoteWifiInfoByDeviceId.value,
      [device.id]: {
        loading: false,
        error: err instanceof Error ? err.message : "Unavailable",
      },
    };
  }
}

const scrollConsoleToBottom = () => {
  if (consoleEl.value) {
    consoleEl.value.scrollTop = consoleEl.value.scrollHeight;
  }
};

function scheduleConsoleScroll() {
  if (consoleScrollQueued) return;
  consoleScrollQueued = true;
  nextTick().then(() => {
    consoleScrollQueued = false;
    scrollConsoleToBottom();
  });
}

function appendLogLines(newLines: string[]) {
  if (!newLines.length) return;
  lines.value = [...lines.value, ...newLines].slice(-MAX_LOG_LINES);
  scheduleConsoleScroll();
}

async function pushLine(msg: string) {
  appendLogLines([msg]);
  await nextTick();
}

async function log(msg: string) {
  await pushLine(msg);
}

function handleLogPayload(raw: unknown) {
  if (!raw || typeof raw !== "object") return;
  const payload = raw as { type?: string; line?: unknown; lines?: unknown };

  if (payload.type === "pong") {
    return;
  }

  if (payload.type === "logs" && Array.isArray(payload.lines)) {
    const normalized = normalizeLogLines(
      payload.lines.filter((line): line is string => typeof line === "string")
    );
    lines.value = normalized.slice(-MAX_LOG_LINES);
    scheduleConsoleScroll();
    return;
  }

  if (payload.type === "log" && typeof payload.line === "string") {
    const merged = splitAndNormalize(payload.line);
    appendLogLines(merged);
  }
}

function clearPingTimer() {
  if (pingTimer.value !== null) {
    clearInterval(pingTimer.value);
    pingTimer.value = null;
  }
}

function scheduleReconnect(ws: WebSocket) {
  if (socket.value !== ws) return;

  if (reconnectHandle.value !== null) return;
  socket.value = null;
  connectionOk.value = false;
  clearPingTimer();
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

  clearPingTimer();
  const protocol = window.location.protocol === "https:" ? "wss" : "ws";
  const ws = new WebSocket(`${protocol}://${window.location.host}/ws`);

  socket.value = ws;

  ws.addEventListener("open", () => {
    if (socket.value !== ws) {
      try { ws.close(); } catch (_) {}
      return;
    }

    reconnectAttempts.value = 0;
    lastHeartbeat.value = performance.now();
    connectionOk.value = true;

    const timer = window.setInterval(() => {
      if (socket.value !== ws) {
        clearInterval(timer);
        return;
      }

      if (ws.readyState !== WebSocket.OPEN) {
        return;
      }

      const diff = performance.now() - lastHeartbeat.value;
      if (diff > HEARTBEAT_TIMEOUT_MS) {
        connectionOk.value = false;
        try { ws.close(); } catch (_) {}
        return;
      }

      try { ws.send('{"type":"ping"}'); } catch (_) {}
    }, PING_INTERVAL_MS);
    pingTimer.value = timer;
  });

  ws.addEventListener("message", (event) => {
    if (socket.value !== ws) return;
    if (typeof event.data !== "string") return;
    lastHeartbeat.value = performance.now();
    connectionOk.value = true;
    try {
      handleLogPayload(JSON.parse(event.data));
    } catch {
      // Ignore malformed frames
    }
  });

  ws.addEventListener("close", () => scheduleReconnect(ws));
  ws.addEventListener("error", () => {
    if (socket.value !== ws) return;
    connectionOk.value = false;
    scheduleReconnect(ws);
    try { ws.close(); } catch (_) {}
  });
}

watch(activeTab, async (tabId) => {
  if (tabId === "console") {
    await nextTick();
    scrollConsoleToBottom();
  }

  if (tabId === "connected-devices") {
    loadConnectedDevices();
  }
});

watch(
  () => activeConnectedDevice.value?.id,
  () => {
    loadRemoteWifiInfo(activeConnectedDevice.value);
  }
);

onMounted(() => {
  connectLogStream();
  loadDeviceInfo();
  loadConnectedDevices();
});

onBeforeUnmount(() => {
  if (reconnectHandle.value !== null) {
    clearTimeout(reconnectHandle.value);
    reconnectHandle.value = null;
  }
  clearPingTimer();
  if (socket.value) {
    socket.value.close();
    socket.value = null;
  }
});

async function rebootConnectedDeviceToRecovery(device: ConnectedDevice) {
  if (!device.remoteMac || !device.canManageRecovery) {
    return;
  }

  const confirmed = window.confirm(`Reboot ${device.name} to recovery mode?`);
  if (!confirmed) {
    return;
  }

  remoteRecoveryBusyId.value = device.id;
  remoteActionDeviceId.value = device.id;
  remoteActionMessage.value = "";

  try {
    const res = await fetch(`/wifi/rebootfactory?mac=${encodeURIComponent(device.remoteMac)}`, {
      method: "POST",
      cache: "no-cache",
    });

    if (!res.ok) {
      const message = await res.text();
      throw new Error(message || `HTTP ${res.status}`);
    }

    remoteActionMessage.value = "Recovery reboot requested.";
    await log(`Requested ${device.name} to reboot to recovery mode.`);
    loadConnectedDevices();
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    remoteActionMessage.value = message;
    await log(`Warning: could not reboot ${device.name}: ${message}`);
  } finally {
    remoteRecoveryBusyId.value = null;
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
.container,
.tab,
.btn-danger,
.btn-primary,
.connected-device,
.protocol-pill,
.pill,
.settings-group-title,
.connected-section-title,
.connected-device-name,
.connected-device-sub,
.connected-detail-name,
.status,
.console-sub,
.modal,
.credentials,
.label {
  font-family: Inter, "Segoe UI", "Helvetica Neue", Arial, sans-serif;
}

.container {
  width: 100%;
  padding: 24px;
  display: flex;
  flex-direction: column;
  gap: 20px;
  height: calc(100vh - 32px); /* body padding 16px x2 */
}

h1 {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
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
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
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
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
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

.workspace {
  display: flex;
  flex-direction: column;
  gap: 0;
  flex: 1;
  min-height: 0;
}

.tabs {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
  align-items: flex-end;
  padding: 0 14px;
  margin-bottom: -1px;
  position: relative;
  z-index: 2;
}

.tab {
  padding: 12px 18px 13px;
  border-radius: 12px 12px 0 0;
  border: 1px solid #1f2937;
  border-bottom-color: #11161d;
  background: linear-gradient(180deg, rgba(24, 31, 42, 0.96), rgba(16, 22, 30, 0.92));
  color: #9ca3af;
  font: inherit;
  font-size: 0.78rem;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  cursor: pointer;
  transition: all 0.2s ease;
  position: relative;
}

.tab:hover {
  color: #d1d5db;
  border-color: #334155;
  border-bottom-color: #11161d;
  background: linear-gradient(180deg, rgba(29, 37, 49, 0.98), rgba(18, 24, 33, 0.94));
}

.tab.active {
  color: #60a5fa;
  background: linear-gradient(145deg, #141922, #0f1319);
  border-color: rgba(96, 165, 250, 0.4);
  border-bottom-color: #141922;
  box-shadow:
    inset 0 1px 0 rgba(96, 165, 250, 0.14),
    0 -1px 0 rgba(96, 165, 250, 0.08);
  z-index: 3;
}

.tab-panel {
  background: linear-gradient(145deg, #141922, #0f1319);
  border: 1px solid #1f2937;
  border-radius: 12px;
  padding: 24px 20px 20px;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.4);
  min-height: 0;
  flex: 1;
  overflow: hidden;
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

.btn-danger:disabled {
  cursor: not-allowed;
  opacity: 0.62;
  transform: none;
  box-shadow: none;
}

.console-panel { display: flex; flex-direction: column; gap: 10px; }

.connected-panel {
  display: grid;
  grid-template-columns: minmax(240px, 320px) minmax(0, 1fr);
  grid-template-rows: minmax(0, 1fr);
  gap: 14px;
  align-content: stretch;
  padding: 16px;
}

.connected-list,
.connected-detail {
  display: flex;
  flex-direction: column;
  gap: 10px;
  min-height: 0;
  padding: 14px;
  border-radius: 10px;
  border: 1px solid #1f2937;
  background: rgba(13, 17, 23, 0.78);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.02);
  overflow-y: auto;
  scrollbar-width: thin;
}

.connected-detail {
  min-height: 0;
}

.connected-list {
  align-items: flex-start;
}

.connected-list-group {
  display: flex;
  flex-direction: column;
  gap: 10px;
  width: 100%;
}

.connected-subsection-title {
  color: #6b7280;
  font-size: 0.72rem;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.connected-empty {
  color: #6b7280;
  font-size: 0.84rem;
  line-height: 1.5;
}

.connected-empty-panel {
  justify-content: center;
}

.connected-list-header,
.connected-detail-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
}

.connected-section-title {
  font-size: 0.78rem;
  color: #9ca3af;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.connected-detail-name {
  color: #e5e7eb;
  font-size: 0.96rem;
  font-weight: 700;
  letter-spacing: 0.04em;
  line-height: 1.25;
  text-transform: uppercase;
}

.connected-device {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  width: min(100%, 320px);
  padding: 10px 12px;
  border-radius: 8px;
  border: 1px solid #1f2937;
  background: linear-gradient(180deg, rgba(15, 19, 25, 0.92), rgba(12, 16, 22, 0.9));
  color: inherit;
  font: inherit;
  cursor: pointer;
  text-align: left;
  transition: all 0.2s ease;
}

.connected-device:hover,
.connected-device:focus-visible {
  border-color: #334155;
  background: linear-gradient(180deg, rgba(20, 26, 35, 0.96), rgba(14, 19, 26, 0.94));
  outline: none;
}

.connected-device.active {
  border-color: rgba(96, 165, 250, 0.35);
  box-shadow: inset 0 0 0 1px rgba(96, 165, 250, 0.08);
}

.connected-device-copy {
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.connected-device-badges {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 6px;
  flex-wrap: wrap;
  flex-shrink: 0;
}

.connected-device-name {
  color: #e5e7eb;
  font-size: 0.88rem;
  font-weight: 600;
  line-height: 1.3;
}

.connected-device-sub {
  color: #6b7280;
  font-size: 0.74rem;
  line-height: 1.3;
  margin-top: 2px;
}

.protocol-pill {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 5px 9px;
  border-radius: 999px;
  font-size: 0.68rem;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  border: 1px solid #1f2937;
  white-space: nowrap;
}

.device-state-pill {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 5px 9px;
  border-radius: 999px;
  font-size: 0.68rem;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  border: 1px solid #1f2937;
  white-space: nowrap;
}

.device-state-added {
  color: #34d399;
  background: rgba(16, 185, 129, 0.12);
}

.device-state-detected {
  color: #facc15;
  background: rgba(234, 179, 8, 0.12);
}

.device-state-vigilant {
  color: #34d399;
  background: rgba(16, 185, 129, 0.12);
}

.device-state-other {
  color: #f472b6;
  background: rgba(236, 72, 153, 0.12);
}

.device-state-unknown {
  color: #9ca3af;
  background: rgba(107, 114, 128, 0.12);
}

.protocol-i2c {
  color: #60a5fa;
  background: rgba(59, 130, 246, 0.12);
}

.protocol-spi {
  color: #34d399;
  background: rgba(16, 185, 129, 0.12);
}

.protocol-canfd {
  color: #facc15;
  background: rgba(234, 179, 8, 0.12);
}

.protocol-wifi {
  color: #f472b6;
  background: rgba(236, 72, 153, 0.12);
}

.connected-detail-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
}

.connected-detail-row {
  padding: 10px 12px;
  border-radius: 8px;
  border: 1px solid #1f2937;
  background: rgba(10, 14, 20, 0.62);
}

.connected-detail-row dt {
  font-size: 0.68rem;
  color: #6b7280;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  margin-bottom: 4px;
}

.connected-detail-row dd {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
  color: #e5e7eb;
  font-size: 0.84rem;
  font-weight: 600;
  margin: 0;
}

.connected-actions {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
  padding-top: 4px;
}

.connected-action {
  width: auto;
  min-width: 220px;
  max-width: 280px;
}

.connected-action-status {
  color: #9ca3af;
  font-size: 0.84rem;
}

.settings-panel {
  display: flex;
  align-items: flex-start;
}

.settings-group {
  width: min(100%, 420px);
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding: 18px;
  border-radius: 10px;
  border: 1px solid #1f2937;
  background: rgba(13, 17, 23, 0.78);
}

.settings-group-title {
  font-size: 0.82rem;
  color: #9ca3af;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.settings-action {
  max-width: 320px;
}

.tab-panel-empty {
  background:
    linear-gradient(145deg, rgba(20, 25, 34, 0.98), rgba(15, 19, 25, 0.96)),
    repeating-linear-gradient(
      135deg,
      rgba(59, 130, 246, 0.03),
      rgba(59, 130, 246, 0.03) 18px,
      transparent 18px,
      transparent 36px
    );
}

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
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
  background: #0d1117;
  border: 1px solid #1f2937;
  border-radius: 8px;
  padding: 14px;
  font-size: 0.84rem;
  line-height: 1.05;
  min-height: 0;
  height: 100%;
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
.value {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
}

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

@media (max-width: 840px) {
  .container {
    padding: 20px 16px;
  }

  .topbar {
    flex-direction: column;
  }

  .tabs {
    padding: 0 10px;
  }

  .tab {
    padding: 11px 14px 12px;
  }

  .connected-panel {
    grid-template-columns: 1fr;
    grid-template-rows: auto;
    gap: 12px;
    padding: 14px;
    overflow-y: auto;
  }

  .connected-detail-grid {
    grid-template-columns: 1fr;
  }

  .connected-list,
  .connected-detail {
    overflow: visible;
  }

  .console-header {
    flex-direction: column;
    align-items: flex-start;
  }
}
</style>
