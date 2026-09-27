const assert = require("node:assert/strict");
const { test } = require("node:test");
const fs = require("node:fs");
const vm = require("node:vm");

function popup() {
  const html = fs.readFileSync("xtouch28/popup.html", "utf8");
  const ids = [...html.matchAll(/id="([^"]+)"/g)].map((m) => m[1]);
  const elements = Object.fromEntries(ids.map((id) => [id, {
    value: "", textContent: "", style: {}, disabled: false, addEventListener() {},
  }]));
  const storage = new Map();
  let domReady;
  const document = {
    getElementById(id) { assert.ok(elements[id], `missing element: ${id}`); return elements[id]; },
    addEventListener(_, fn) { domReady = fn; },
    querySelector() { return { addEventListener() {} }; },
    createElement() { return { click() {}, remove() {} }; },
  };
  const context = vm.createContext({
    document, TextEncoder, URL, Blob, AbortSignal, setTimeout: () => 0,
    atob: (s) => Buffer.from(s, "base64").toString("binary"),
    localStorage: {
      getItem: (key) => storage.get(key) ?? null,
      setItem: (key, value) => storage.set(key, value),
      removeItem: (key) => storage.delete(key),
    },
  });
  vm.runInContext(fs.readFileSync("xtouch28/popup.js", "utf8"), context);
  Object.entries({ ssid: " wifi ", password: " pass ", serialNumber: "01P00A123456789", printerModel: "P1S", cloudUsername: "u_123", cloudToken: "test-token", cloudRegion: "Global" }).forEach(([id, value]) => elements[id].value = value);
  return { context, elements, storage, domReady };
}

test("manual HA setup opens on any tab without calling browser APIs", () => {
  const p = popup();
  p.domReady();
  assert.equal(p.elements["main-container"].style.display, "block");
  assert.equal(p.elements["main-loading"].style.display, "none");
});

test("download config preserves spaces and needs no device IP", () => {
  const { context } = popup();
  assert.equal(context.validateForm(), true);
  assert.equal(context.validateForm(true), false);
  const config = context.buildProvisioningConfig();
  assert.equal(config.ssid, " wifi ");
  assert.equal(config.pwd, " pass ");
  assert.equal(config.mqtt.printerModel, "P1S");
  assert.equal(config["cloud-authToken"], undefined);
});

test("input limits match firmware and invalid IPs never get a request", () => {
  const { context, elements } = popup();
  elements.ip.value = "10.0.1.250";
  assert.equal(context.validateForm(true), true);
  for (const ip of ["0.0.0.0", "10.0.1.999", "localhost/path", "1.2.3"] ) {
    elements.ip.value = ip;
    assert.equal(context.validateForm(true), false);
  }
  elements.cloudToken.value = "x".repeat(2048);
  assert.equal(context.validateForm(), false);
  elements.cloudToken.value = "test-token";
  elements.ssid.value = "가".repeat(11);
  assert.equal(context.validateForm(), false);
});

test("stored credentials are removed when migrating extension preferences", () => {
  const { context, storage } = popup();
  storage.set("jsonData", JSON.stringify({ pwd: "secret", "cloud-authToken": "private", serialNumber: "01P00A123456789" }));
  context.loadStoredData();
  const saved = JSON.parse(storage.get("jsonData"));
  assert.equal(saved.pwd, undefined);
  assert.equal(saved["cloud-authToken"], undefined);
  assert.equal(saved.serialNumber, "01P00A123456789");
});

test("JWT claim extraction handles padding and opaque tokens", () => {
  const { context } = popup();
  const token = "header." + Buffer.from(JSON.stringify({ username: "u_123" })).toString("base64url") + ".sig";
  assert.equal(context.usernameFromToken(token), "u_123");
  assert.equal(context.usernameFromToken("opaque"), "");
});

test("HTTP provisioning uses nested config and reports device rejection", async () => {
  const { context, elements } = popup();
  elements.ip.value = "10.0.1.250";
  let sent;
  context.fetch = async (url, options) => {
    sent = { url, config: JSON.parse(options.body) };
    return { ok: false, json: async () => ({ error: "provisioning is not active on the screen" }) };
  };
  await context.provisionDevice();
  assert.equal(sent.url, "http://10.0.1.250/provision");
  assert.equal(sent.config.mqtt.mode, "cloud");
  assert.equal(elements["provision-error"].style.display, "block");
  assert.equal(elements["provisionDevice-button"].disabled, false);
  context.fetch = async () => ({ ok: true, json: async () => ({ status: "ok" }) });
  await context.provisionDevice();
  assert.match(elements["cloud-status"].textContent, /restarting/);
});

test("Bambu cookie lookup is confined to the selected account domain", async () => {
  const { context, elements } = popup();
  const token = "header." + Buffer.from(JSON.stringify({ username: "u_123" })).toString("base64url") + ".sig";
  let queried;
  context.chrome = {
    tabs: { query: async () => [{ id: 7, url: "https://bambulab.cn/zh" }] },
    cookies: { getAll: async (query) => { queried = query; return [{ name: "token", value: token }]; } },
  };
  await context.fetchCloudLogin();
  assert.equal(queried.url, "https://bambulab.cn");
  assert.equal(elements.cloudRegion.value, "China");
  assert.equal(elements.cloudUsername.value, "u_123");
  queried = undefined;
  context.chrome.tabs.query = async () => [{ url: "https://bambulab.cn.evil.test/" }];
  await context.fetchCloudLogin();
  assert.equal(queried, undefined);
});
