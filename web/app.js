const STORAGE_KEY = "aquafeed-device-url";
const REFRESH_INTERVAL_MS = 15000;

const elements = {
  connectionForm: document.querySelector("#connectionForm"),
  deviceUrl: document.querySelector("#deviceUrl"),
  connectionPill: document.querySelector("#connectionPill"),
  connectionLabel: document.querySelector("#connectionLabel"),
  deviceAddressText: document.querySelector("#deviceAddressText"),
  deviceTime: document.querySelector("#deviceTime"),
  feedCount: document.querySelector("#feedCount"),
  wifiSignal: document.querySelector("#wifiSignal"),
  lastFeed: document.querySelector("#lastFeed"),
  refreshButton: document.querySelector("#refreshButton"),
  feedButton: document.querySelector("#feedButton"),
  scheduleForm: document.querySelector("#scheduleForm"),
  schedule1: document.querySelector("#schedule1"),
  schedule2: document.querySelector("#schedule2"),
  savedBadge: document.querySelector("#savedBadge"),
  toast: document.querySelector("#toast"),
};

let deviceBaseUrl = "";
let refreshTimer;
let toastTimer;

function normalizeDeviceUrl(value) {
  const trimmed = value.trim().replace(/\/+$/, "");
  if (!trimmed) return "";
  return /^https?:\/\//i.test(trimmed) ? trimmed : `http://${trimmed}`;
}

function setConnectionState(state, label) {
  elements.connectionPill.dataset.state = state;
  elements.connectionLabel.textContent = label;
}

function setControlsDisabled(disabled) {
  elements.refreshButton.disabled = disabled;
  elements.feedButton.disabled = disabled;
  elements.scheduleForm
    .querySelectorAll("input, button")
    .forEach((element) => (element.disabled = disabled));
}

function showToast(message, type = "success") {
  clearTimeout(toastTimer);
  elements.toast.textContent = message;
  elements.toast.className = `toast visible${type === "error" ? " error" : ""}`;
  toastTimer = setTimeout(() => {
    elements.toast.className = "toast";
  }, 3500);
}

async function apiRequest(path, options = {}) {
  if (!deviceBaseUrl) throw new Error("Connect to the ESP-01 first.");

  const response = await fetch(`${deviceBaseUrl}${path}`, {
    ...options,
    headers: {
      "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
      ...options.headers,
    },
  });

  let body;
  try {
    body = await response.json();
  } catch {
    body = {};
  }

  if (!response.ok) {
    throw new Error(body.message || `Device request failed (${response.status}).`);
  }

  return body;
}

function renderStatus(status) {
  elements.deviceTime.textContent = status.deviceTime || "—";
  elements.feedCount.textContent = Number.isFinite(status.feedCount)
    ? status.feedCount
    : "—";
  elements.wifiSignal.textContent = Number.isFinite(status.rssi)
    ? `${status.rssi} dBm`
    : "—";
  elements.lastFeed.textContent = status.lastFeed || "No data yet";

  if (status.schedule1) elements.schedule1.value = status.schedule1;
  if (status.schedule2) elements.schedule2.value = status.schedule2;
}

async function refreshStatus({ silent = false } = {}) {
  if (!deviceBaseUrl) return;

  setConnectionState("busy", "Checking feeder…");
  try {
    const status = await apiRequest("/api/status");
    renderStatus(status);
    setConnectionState("online", "Feeder online");
    setControlsDisabled(false);
    if (!silent) showToast("Feeder status updated.");
  } catch (error) {
    setConnectionState("offline", "Connection failed");
    setControlsDisabled(true);
    if (!silent) showToast(error.message, "error");
    throw error;
  }
}

async function connectToDevice(value) {
  const normalized = normalizeDeviceUrl(value);
  if (!normalized) {
    showToast("Enter the ESP-01 IP address.", "error");
    return;
  }

  deviceBaseUrl = normalized;
  elements.deviceUrl.value = normalized;
  elements.deviceAddressText.textContent = normalized;
  localStorage.setItem(STORAGE_KEY, normalized);
  clearInterval(refreshTimer);

  try {
    await refreshStatus();
    refreshTimer = setInterval(() => refreshStatus({ silent: true }).catch(() => {}), REFRESH_INTERVAL_MS);
  } catch {
    // The UI already shows the connection error and retains the address for retrying.
  }
}

elements.connectionForm.addEventListener("submit", (event) => {
  event.preventDefault();
  connectToDevice(elements.deviceUrl.value);
});

elements.refreshButton.addEventListener("click", () => {
  refreshStatus().catch(() => {});
});

elements.feedButton.addEventListener("click", async () => {
  const confirmed = window.confirm("Run one feeding cycle now?");
  if (!confirmed) return;

  elements.feedButton.disabled = true;
  setConnectionState("busy", "Dispensing…");
  try {
    const result = await apiRequest("/api/feed", { method: "POST" });
    showToast(result.message || "Feed cycle completed.");
    await refreshStatus({ silent: true });
  } catch (error) {
    showToast(error.message, "error");
    setConnectionState("offline", "Connection failed");
  } finally {
    elements.feedButton.disabled = false;
  }
});

elements.scheduleForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const body = new URLSearchParams({
    time1: elements.schedule1.value,
    time2: elements.schedule2.value,
  });

  elements.savedBadge.textContent = "Saving…";
  try {
    const result = await apiRequest("/api/schedule", {
      method: "POST",
      body: body.toString(),
    });
    elements.savedBadge.textContent = "Stored on feeder";
    showToast(result.message || "Schedule saved.");
    await refreshStatus({ silent: true });
  } catch (error) {
    elements.savedBadge.textContent = "Not saved";
    showToast(error.message, "error");
  }
});

setControlsDisabled(true);
const savedAddress = localStorage.getItem(STORAGE_KEY);
if (savedAddress) {
  elements.deviceUrl.value = savedAddress;
  connectToDevice(savedAddress);
}
