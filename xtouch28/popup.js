const $id = document.getElementById.bind(document);
const byteLength = (value) => new TextEncoder().encode(value).length;

function buildProvisioningConfig() {
  return {
    // Spaces are valid in SSIDs and passwords.
    ssid: $id("ssid").value,
    pwd: $id("password").value,
    mqtt: {
      mode: "cloud",
      region: $id("cloudRegion").value,
      username: $id("cloudUsername").value.trim(),
      authToken: $id("cloudToken").value.trim(),
      serialNumber: $id("serialNumber").value.trim(),
      printerModel: $id("printerModel").value.trim(),
    },
  };
}

function validateForm(requireIp = false) {
  const config = buildProvisioningConfig();
  const mqtt = config.mqtt;
  const errors = {
    ssid: byteLength(config.ssid) < 1 || byteLength(config.ssid) > 32 ? "SSID must contain 1-32 bytes." : "",
    password: byteLength(config.pwd) > 64 ? "WiFi password must contain at most 64 bytes." : "",
    serialNumber: !/^[a-zA-Z0-9]{1,15}$/.test(mqtt.serialNumber) ? "Enter the printer serial (up to 15 letters or digits)." : "",
    printerModel: !["P1P", "P1S", "X1", "X1C", "C11", "C12", "3DPrinter-X1", "3DPrinter-X1-Carbon"].includes(mqtt.printerModel) ? "Use P1P, P1S, X1, or X1C." : "",
    cloudUsername: !/^u_[a-zA-Z0-9_]+$/.test(mqtt.username) || byteLength(mqtt.username) > 63 ? "Enter the Cloud MQTT username (u_...)." : "",
    cloudToken: !mqtt.authToken || byteLength(mqtt.authToken) > 2047 ? "Enter a Cloud token of at most 2047 bytes." : "",
    ip: "",
  };
  if (requireIp) {
    const ip = $id("ip").value.trim();
    const parts = ip.split(".");
    if (parts.length !== 4 || parts.some((p) => !/^\d{1,3}$/.test(p) || Number(p) > 255) || ip === "0.0.0.0")
      errors.ip = "Enter the IP shown on the CYD screen.";
  }
  for (const [name, message] of Object.entries(errors)) $id(`${name}-error`).textContent = message;
  return Object.values(errors).every((message) => !message);
}

function savePreferences() {
  // Tokens and WiFi passwords are never written to browser storage.
  localStorage.setItem("jsonData", JSON.stringify({
    ssid: $id("ssid").value,
    ip: $id("ip").value.trim(),
    serialNumber: $id("serialNumber").value.trim(),
    printerModel: $id("printerModel").value.trim(),
    region: $id("cloudRegion").value,
  }));
}

function loadStoredData() {
  try {
    const stored = JSON.parse(localStorage.getItem("jsonData") || "{}");
    for (const key of ["ssid", "ip", "serialNumber", "printerModel"])
      if (typeof stored[key] === "string") $id(key).value = stored[key];
    $id("cloudRegion").value = stored.region === "China" ? "China" : "Global";
    // Migrate old extension storage by removing saved secrets.
    savePreferences();
  } catch {
    localStorage.removeItem("jsonData");
  }
}

function downloadJsonData() {
  if (!validateForm()) return;
  savePreferences();
  const url = URL.createObjectURL(new Blob([JSON.stringify(buildProvisioningConfig(), null, 2)], { type: "application/json" }));
  const link = document.createElement("a");
  link.href = url;
  // xtouch.json takes precedence over the legacy provisioning.json on SD.
  link.download = "xtouch.json";
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

async function provisionDevice() {
  if (!validateForm(true)) return;
  const button = $id("provisionDevice-button");
  const status = $id("provision-error");
  button.disabled = true;
  status.style.display = "none";
  try {
    const response = await fetch(`http://${$id("ip").value.trim()}/provision`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(buildProvisioningConfig()),
      signal: AbortSignal.timeout(15000),
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || "Provisioning failed.");
    savePreferences();
    $id("cloud-status").textContent = "Configuration saved. The CYD is restarting.";
  } catch (error) {
    status.textContent = error.message || "Could not reach the CYD.";
    status.style.display = "block";
  } finally {
    button.disabled = false;
  }
}

function usernameFromToken(token) {
  try {
    let payload = token.split(".")[1].replace(/-/g, "+").replace(/_/g, "/");
    payload += "=".repeat((4 - payload.length % 4) % 4);
    const claims = JSON.parse(atob(payload));
    return typeof claims.username === "string" ? claims.username : "";
  } catch { return ""; }
}

async function fetchCloudLogin() {
  const status = $id("cloud-status");
  try {
    const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
    const url = new URL(tab?.url || "");
    const china = url.hostname === "bambulab.cn" || url.hostname.endsWith(".bambulab.cn");
    const global = url.hostname === "bambulab.com" || url.hostname.endsWith(".bambulab.com");
    if (url.protocol !== "https:" || (!china && !global)) throw new Error("Open a signed-in Bambu tab, or paste your HA credentials above.");
    const cookies = await chrome.cookies.getAll({ url: url.origin });
    const token = cookies.find((cookie) => cookie.name === "token")?.value;
    if (!token) throw new Error("No Cloud token found. Paste the username and auth_token used by HA.");
    let username = usernameFromToken(token);
    if (!username) {
      const results = await chrome.scripting.executeScript({
        target: { tabId: tab.id },
        func: () => {
          try {
            const data = JSON.parse(document.getElementById("__NEXT_DATA__")?.textContent || "{}");
            const uid = data?.props?.pageProps?.session?.user?.uidStr;
            return uid ? `u_${uid}` : "";
          } catch { return ""; }
        },
      });
      username = results[0]?.result || "";
    }
    $id("cloudToken").value = token;
    $id("cloudUsername").value = username;
    $id("cloudRegion").value = china ? "China" : "Global";
    status.textContent = username ? "Cloud credentials loaded into this popup." : "Token loaded. Enter the Cloud MQTT username from HA.";
  } catch (error) { status.textContent = error.message; }
}

document.addEventListener("DOMContentLoaded", () => {
  $id("main-loading").style.display = "none";
  $id("main-container").style.display = "block";
  loadStoredData();
  document.querySelector("form").addEventListener("submit", (event) => event.preventDefault());
  $id("downloadJson").addEventListener("click", downloadJsonData);
  $id("provisionDevice-button").addEventListener("click", provisionDevice);
  $id("readCloudLogin").addEventListener("click", fetchCloudLogin);
  $id("cloudToken").addEventListener("input", () => {
    if (!$id("cloudUsername").value) $id("cloudUsername").value = usernameFromToken($id("cloudToken").value.trim());
  });
});
