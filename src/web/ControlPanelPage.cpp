#include "ControlPanelPage.h"

const char kControlPanelHtml[] PROGMEM = R"controlpanel(
<!doctype html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Translation Control Panel</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f5f7f8;
      --panel: #ffffff;
      --text: #17212b;
      --muted: #5d6975;
      --line: #d8dee4;
      --primary: #0f766e;
      --primary-strong: #115e59;
      --danger: #b42318;
      --warn: #b54708;
      --ok: #157f3b;
      --input: #fbfcfd;
    }

    * {
      box-sizing: border-box;
    }

    body {
      margin: 0;
      background: var(--bg);
      color: var(--text);
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      font-size: 15px;
      line-height: 1.45;
    }

    header {
      background: #17212b;
      color: #fff;
      padding: 18px 20px;
    }

    header h1 {
      margin: 0;
      font-size: 22px;
      font-weight: 700;
    }

    header p {
      margin: 4px 0 0;
      color: #d7dee6;
    }

    .topbar {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 14px;
    }

    .topbar-title {
      display: grid;
      gap: 2px;
    }

    .screen[hidden] {
      display: none;
    }

    .voice-home {
      min-height: calc(100vh - 82px);
      display: grid;
      grid-template-rows: auto 1fr;
      gap: 18px;
      background: #05070a;
      color: #f8fafc;
      padding: 28px 18px;
    }

    .voice-home-inner {
      width: min(1120px, 100%);
      margin: 0 auto;
      display: grid;
      grid-template-columns: minmax(280px, 520px) minmax(280px, 1fr);
      gap: 18px;
      align-self: center;
      align-items: stretch;
    }

    main {
      width: min(1180px, 100%);
      margin: 0 auto;
      padding: 18px;
    }

    .grid {
      display: grid;
      grid-template-columns: repeat(12, 1fr);
      gap: 14px;
      align-items: start;
    }

    section {
      grid-column: span 12;
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 16px;
    }

    @media (min-width: 860px) {
      .half {
        grid-column: span 6;
      }

      .third {
        grid-column: span 4;
      }

      .wide {
        grid-column: span 8;
      }
    }

    h2 {
      margin: 0 0 12px;
      font-size: 17px;
    }

    .status-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(170px, 1fr));
      gap: 10px;
    }

    .metric {
      border: 1px solid var(--line);
      border-radius: 6px;
      padding: 10px;
      background: #fbfcfd;
      min-height: 74px;
    }

    .metric span {
      display: block;
      color: var(--muted);
      font-size: 12px;
      margin-bottom: 4px;
    }

    .metric strong {
      display: block;
      overflow-wrap: anywhere;
      font-size: 16px;
    }

    form {
      display: grid;
      gap: 12px;
    }

    .fields {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(210px, 1fr));
      gap: 12px;
    }

    label {
      display: grid;
      gap: 5px;
      color: var(--muted);
      font-size: 13px;
    }

    input,
    select {
      width: 100%;
      border: 1px solid var(--line);
      border-radius: 6px;
      background: var(--input);
      color: var(--text);
      font: inherit;
      min-height: 38px;
      padding: 8px 10px;
    }

    input[type="checkbox"] {
      width: 18px;
      height: 18px;
      min-height: 18px;
      padding: 0;
    }

    .check-row {
      display: flex;
      gap: 10px;
      align-items: center;
      color: var(--text);
      min-height: 38px;
    }

    .actions {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      align-items: center;
    }

    button {
      border: 1px solid transparent;
      border-radius: 6px;
      color: #fff;
      background: var(--primary);
      font: inherit;
      font-weight: 650;
      min-height: 38px;
      padding: 8px 12px;
      cursor: pointer;
    }

    button:hover,
    button:focus-visible {
      background: var(--primary-strong);
    }

    button.secondary {
      color: var(--text);
      background: #eef2f4;
      border-color: var(--line);
    }

    button.secondary:hover,
    button.secondary:focus-visible {
      background: #e2e8ec;
    }

    button.warn {
      background: var(--warn);
    }

    button.danger {
      background: var(--danger);
    }

    .terminal-row {
      display: grid;
      grid-template-columns: 1fr auto;
      gap: 8px;
    }

    .message {
      min-height: 24px;
      color: var(--muted);
      overflow-wrap: anywhere;
    }

    .message.ok {
      color: var(--ok);
    }

    .message.error {
      color: var(--danger);
    }

    .last-text {
      display: grid;
      gap: 8px;
    }

    .text-box {
      min-height: 52px;
      border: 1px solid var(--line);
      border-radius: 6px;
      background: #fbfcfd;
      padding: 10px;
      overflow-wrap: anywhere;
    }

    .voice-stage {
      display: grid;
      place-items: center;
      gap: 14px;
      min-height: 288px;
      border-radius: 8px;
      background: #05070a;
      color: #f8fafc;
      padding: 22px;
    }

    .voice-home .voice-stage {
      grid-column: 1;
      grid-row: 1;
      min-height: 520px;
      border: 0;
      padding: 30px 20px;
    }

    #voiceActionMessage {
      grid-column: 1;
      grid-row: 2;
      margin: 0;
      text-align: center;
    }

    .voice-orb {
      width: 144px;
      height: 144px;
      border-radius: 50%;
      background:
        radial-gradient(circle at 34% 28%, #f7feff 0 16%, #b7efff 26%, transparent 42%),
        radial-gradient(circle at 64% 70%, #0ea5e9 0 22%, transparent 48%),
        radial-gradient(circle at 42% 62%, #38bdf8 0 28%, #2563eb 58%, #e0f2fe 100%);
      box-shadow: 0 0 24px rgba(14, 165, 233, 0.52);
      transform: scale(0.9);
      transition: box-shadow 180ms ease, transform 180ms ease, filter 180ms ease;
    }

    .voice-home .voice-orb {
      width: min(210px, 48vw);
      height: min(210px, 48vw);
    }

    .voice-orb.idle {
      filter: saturate(0.68);
      opacity: 0.72;
    }

    .voice-orb.listening {
      animation: voicePulse 980ms ease-in-out infinite;
      box-shadow: 0 0 34px rgba(56, 189, 248, 0.68);
    }

    .voice-orb.thinking {
      animation: voiceThink 1300ms linear infinite;
      box-shadow: 0 0 28px rgba(20, 184, 166, 0.58);
    }

    .voice-orb.speaking {
      animation: voiceSpeak 520ms ease-in-out infinite alternate;
      box-shadow: 0 0 42px rgba(59, 130, 246, 0.74);
    }

    .voice-label {
      margin: 0;
      min-height: 22px;
      color: #dbeafe;
      font-weight: 700;
      text-align: center;
      font-size: 22px;
    }

    .voice-subtitle {
      margin: -8px 0 0;
      min-height: 20px;
      color: #94a3b8;
      text-align: center;
      max-width: 420px;
    }

    .voice-controls {
      display: flex;
      gap: 12px;
      align-items: center;
      justify-content: center;
    }

    .round-button {
      width: 46px;
      height: 46px;
      min-height: 46px;
      border-radius: 50%;
      padding: 0;
      display: inline-grid;
      place-items: center;
      background: #1f2937;
      color: #fff;
      border-color: #263241;
      font-size: 22px;
      line-height: 1;
    }

    .round-button:hover,
    .round-button:focus-visible {
      background: #334155;
    }

    .round-button.primary-action {
      background: #0f766e;
      border-color: #0f766e;
    }

    .history-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      margin-bottom: 12px;
    }

    .history-header h2 {
      margin: 0;
    }

    .voice-history-panel {
      grid-column: 2;
      grid-row: 1 / span 2;
      min-height: 520px;
      border: 1px solid #1f2937;
      border-radius: 8px;
      background: #0b1018;
      padding: 16px;
      color: #f8fafc;
      display: grid;
      grid-template-rows: auto 1fr;
      gap: 10px;
    }

    .chat-history {
      display: grid;
      gap: 10px;
      max-height: 430px;
      overflow-y: auto;
      padding-right: 4px;
    }

    .empty-history {
      margin: 0;
      color: var(--muted);
    }

    .voice-history-panel .empty-history {
      color: #94a3b8;
    }

    .chat-entry {
      display: grid;
      gap: 7px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: #fbfcfd;
      padding: 10px;
    }

    .chat-line {
      display: grid;
      gap: 4px;
    }

    .chat-line span {
      color: var(--muted);
      font-size: 12px;
      font-weight: 700;
    }

    .chat-line p {
      margin: 0;
      overflow-wrap: anywhere;
    }

    .chat-time {
      color: var(--muted);
      font-size: 12px;
    }

    .voice-history-panel .chat-history {
      max-height: none;
      align-content: start;
    }

    .voice-history-panel .chat-entry {
      border-color: #1f2937;
      background: #111827;
    }

    .voice-history-panel .chat-line span,
    .voice-history-panel .chat-time {
      color: #94a3b8;
    }

    .voice-history-panel .chat-line p {
      color: #f8fafc;
    }

    @media (max-width: 820px) {
      .voice-home-inner {
        grid-template-columns: 1fr;
      }

      .voice-home .voice-stage,
      .voice-history-panel {
        min-height: auto;
      }

      #voiceActionMessage,
      .voice-history-panel {
        grid-column: auto;
        grid-row: auto;
      }

      .voice-history-panel {
        max-height: 42vh;
      }
    }

    @keyframes voicePulse {
      0%,
      100% {
        transform: scale(0.92);
      }

      50% {
        transform: scale(1.04);
      }
    }

    @keyframes voiceThink {
      from {
        transform: rotate(0deg) scale(0.95);
      }

      to {
        transform: rotate(360deg) scale(0.95);
      }
    }

    @keyframes voiceSpeak {
      from {
        transform: scale(0.95);
      }

      to {
        transform: scale(1.08);
      }
    }

    code {
      background: #eef2f4;
      border-radius: 4px;
      padding: 2px 4px;
    }
  </style>
</head>
<body>
  <header class="topbar">
    <div class="topbar-title">
      <h1>ESP32 Voice Translator</h1>
      <p id="topbarStatus">Ready</p>
    </div>
    <button type="button" class="secondary" id="settingsToggleButton">Settings</button>
  </header>

  <section class="screen voice-home" id="voiceScreen">
    <div class="voice-home-inner">
      <div class="voice-stage">
        <div class="voice-orb idle" id="voiceOrb" aria-hidden="true"></div>
        <p class="voice-label" id="voiceLabel">Ready</p>
        <p class="voice-subtitle" id="voiceSubtitle">Waiting for distance and microphone input.</p>
        <div class="voice-controls">
          <button type="button" class="round-button primary-action" id="voiceRecordButton" title="Start recording" aria-label="Start recording">●</button>
          <button type="button" class="round-button" id="voiceRefreshButton" title="Refresh status" aria-label="Refresh status">↻</button>
        </div>
      </div>
      <p class="message" id="voiceActionMessage"></p>
      <div class="voice-history-panel">
        <div class="history-header">
          <h2>Conversation History</h2>
          <button type="button" class="secondary" id="clearHistoryButton">Clear</button>
        </div>
        <div class="chat-history" id="chatHistory"></div>
      </div>
    </div>
  </section>

  <main class="screen grid" id="settingsScreen" hidden>
    <section class="wide">
      <h2>Status</h2>
      <div class="status-grid">
        <div class="metric"><span>Wi-Fi</span><strong id="wifiState">-</strong></div>
        <div class="metric"><span>IP</span><strong id="ipAddress">-</strong></div>
        <div class="metric"><span>Recording</span><strong id="sessionState">-</strong></div>
        <div class="metric"><span>Distance</span><strong id="distance">-</strong></div>
        <div class="metric"><span>Microphone</span><strong id="microphone">-</strong></div>
        <div class="metric"><span>Speaker</span><strong id="speaker">-</strong></div>
        <div class="metric"><span>VL53L0X</span><strong id="sensor">-</strong></div>
        <div class="metric"><span>Heap</span><strong id="heap">-</strong></div>
      </div>
    </section>

    <section class="third">
      <h2>Quick Actions</h2>
      <div class="actions">
        <button type="button" id="recordButton">Start Recording</button>
        <button type="button" class="secondary" id="refreshButton">Refresh</button>
        <button type="button" class="warn" id="restartButton">Restart</button>
        <button type="button" class="danger" id="clearButton">Clear Settings</button>
      </div>
      <p class="message" id="actionMessage"></p>
    </section>

    <section class="half">
      <h2>Network and OpenRouter</h2>
      <form id="networkForm">
        <div class="fields">
          <label>Wi-Fi SSID
            <input id="wifiSsid" autocomplete="off">
          </label>
          <label>Wi-Fi password
            <input id="wifiPassword" type="password" autocomplete="new-password" placeholder="Leave blank to keep unchanged">
          </label>
          <label>OpenRouter API key
            <input id="openRouterApiKey" type="password" autocomplete="new-password" placeholder="Leave blank to keep unchanged">
          </label>
          <label>OpenRouter IP fallback
            <input id="openRouterIpOverride" autocomplete="off" placeholder="Uses DNS when blank">
          </label>
          <label>DNS mode
            <select id="dnsMode">
              <option value="manual">Manual DNS</option>
              <option value="dhcp">DHCP DNS</option>
            </select>
          </label>
          <label>Primary DNS
            <input id="primaryDns" inputmode="numeric" autocomplete="off">
          </label>
          <label>Secondary DNS
            <input id="secondaryDns" inputmode="numeric" autocomplete="off">
          </label>
        </div>
        <div class="actions">
          <button type="submit">Save Network Settings</button>
        </div>
      </form>
    </section>

    <section class="half">
      <h2>Translation and Audio</h2>
      <form id="translationForm">
        <div class="fields">
          <label>Source language
            <select id="sourceLanguage"></select>
          </label>
          <label>Target language
            <select id="targetLanguage"></select>
          </label>
          <label>STT model
            <input id="sttModel" autocomplete="off">
          </label>
          <label>Translation model
            <input id="translationModel" autocomplete="off">
          </label>
          <label>TTS model
            <input id="ttsModel" autocomplete="off">
          </label>
          <label>TTS voice
            <input id="ttsVoice" autocomplete="off">
          </label>
          <label>TTS PCM rate
            <input id="ttsPcmRateHz" type="number" min="8000" max="48000" step="1">
          </label>
          <label>TTS speed
            <input id="ttsSpeed" type="number" min="0.25" max="4" step="0.05">
          </label>
          <label>Playback volume
            <input id="playbackVolume" type="number" min="0.1" max="12" step="0.1">
          </label>
          <label class="check-row">
            <input id="autoPresence" type="checkbox">
            Automatic presence trigger
          </label>
        </div>
        <div class="actions">
          <button type="submit">Save Translation Settings</button>
        </div>
      </form>
    </section>

    <section class="half">
      <h2>Last Session</h2>
      <div class="last-text">
        <div>
          <strong>Detected text</strong>
          <div class="text-box" id="lastTranscript">-</div>
        </div>
        <div>
          <strong>Translation</strong>
          <div class="text-box" id="lastTranslation">-</div>
        </div>
      </div>
    </section>

    <section class="half">
      <h2>Terminal Command</h2>
      <div class="terminal-row">
        <input id="terminalCommand" autocomplete="off" placeholder="Example: lang en tr, volume 4, presence off">
        <button type="button" id="sendCommandButton">Send</button>
      </div>
      <p class="message" id="terminalMessage">The same serial terminal commands also work here: <code>r</code>, <code>show</code>, <code>dns auto</code>, <code>orip clear</code>, <code>restart</code>, <code>clear</code>.</p>
    </section>
  </main>
  <script>
    const statusIds = {
      wifiState: document.getElementById("wifiState"),
      ipAddress: document.getElementById("ipAddress"),
      sessionState: document.getElementById("sessionState"),
      distance: document.getElementById("distance"),
      microphone: document.getElementById("microphone"),
      speaker: document.getElementById("speaker"),
      sensor: document.getElementById("sensor"),
      heap: document.getElementById("heap"),
      lastTranscript: document.getElementById("lastTranscript"),
      lastTranslation: document.getElementById("lastTranslation")
    };

    const voiceUi = {
      orb: document.getElementById("voiceOrb"),
      label: document.getElementById("voiceLabel"),
      subtitle: document.getElementById("voiceSubtitle"),
      topbarStatus: document.getElementById("topbarStatus")
    };
    const voiceScreen = document.getElementById("voiceScreen");
    const settingsScreen = document.getElementById("settingsScreen");
    const settingsToggleButton = document.getElementById("settingsToggleButton");
    const chatHistoryElement = document.getElementById("chatHistory");
    const chatHistoryStorageKey = "esp32TranslatorChatHistory.v1";
    const maxChatHistoryItems = 80;
    const networkFieldIds = ["wifiSsid", "openRouterIpOverride", "dnsMode", "primaryDns", "secondaryDns"];
    const translationFieldIds = ["sourceLanguage", "targetLanguage", "sttModel", "translationModel", "ttsModel", "ttsVoice", "ttsPcmRateHz", "ttsSpeed", "playbackVolume", "autoPresence"];
    const dirtyForms = { network: false, translation: false };
    let sourceLanguageOptions = [];
    let targetLanguageOptions = [];
    let chatHistory = [];
    let lastStoredConversationKey = "";
    let statusRefreshPromise = null;

    function text(value, fallback = "-") {
      if (value === null || value === undefined || value === "") {
        return fallback;
      }
      return String(value);
    }

    function setMessage(elementId, message, isError = false) {
      const element = document.getElementById(elementId);
      element.textContent = message;
      element.className = isError ? "message error" : "message ok";
    }

    function setFieldValue(elementId, value, formName, force = false) {
      if (!force && dirtyForms[formName]) {
        return;
      }

      const element = document.getElementById(elementId);
      if (element.type === "checkbox") {
        element.checked = Boolean(value);
        return;
      }

      element.value = text(value, "");
    }

    function showSettingsScreen(isSettingsVisible) {
      voiceScreen.hidden = isSettingsVisible;
      settingsScreen.hidden = !isSettingsVisible;
      settingsToggleButton.textContent = isSettingsVisible ? "Voice Chat" : "Settings";
    }

    function loadChatHistory() {
      try {
        const rawHistory = localStorage.getItem(chatHistoryStorageKey);
        if (!rawHistory) {
          chatHistory = [];
          return;
        }

        const parsedHistory = JSON.parse(rawHistory);
        if (!Array.isArray(parsedHistory)) {
          chatHistory = [];
          return;
        }

        chatHistory = parsedHistory
          .filter((entry) => entry && typeof entry.transcript === "string" && typeof entry.translation === "string")
          .slice(-maxChatHistoryItems);
      } catch (error) {
        chatHistory = [];
      }
    }

    function saveChatHistory() {
      localStorage.setItem(chatHistoryStorageKey, JSON.stringify(chatHistory.slice(-maxChatHistoryItems)));
    }

    function renderChatHistory() {
      chatHistoryElement.replaceChildren();
      if (chatHistory.length === 0) {
        const emptyMessage = document.createElement("p");
        emptyMessage.className = "empty-history";
        emptyMessage.textContent = "No conversation history yet.";
        chatHistoryElement.appendChild(emptyMessage);
        return;
      }

      for (const entry of chatHistory) {
        const item = document.createElement("article");
        item.className = "chat-entry";

        const time = document.createElement("div");
        time.className = "chat-time";
        time.textContent = new Date(entry.createdAt).toLocaleString("en-US");
        item.appendChild(time);

        const transcriptLine = document.createElement("div");
        transcriptLine.className = "chat-line";
        const transcriptLabel = document.createElement("span");
        transcriptLabel.textContent = "You";
        const transcriptText = document.createElement("p");
        transcriptText.textContent = entry.transcript;
        transcriptLine.append(transcriptLabel, transcriptText);
        item.appendChild(transcriptLine);

        const translationLine = document.createElement("div");
        translationLine.className = "chat-line";
        const translationLabel = document.createElement("span");
        translationLabel.textContent = "Translation";
        const translationText = document.createElement("p");
        translationText.textContent = entry.translation;
        translationLine.append(translationLabel, translationText);
        item.appendChild(translationLine);

        chatHistoryElement.appendChild(item);
      }

      chatHistoryElement.scrollTop = chatHistoryElement.scrollHeight;
    }

    function conversationKey(transcript, translation) {
      return `${transcript.trim()}\n${translation.trim()}`;
    }

    function rememberCompletedConversation(runtime) {
      const transcript = text(runtime.lastTranscript, "").trim();
      const translation = text(runtime.lastTranslation, "").trim();
      if (transcript.length === 0 || translation.length === 0) {
        return;
      }

      const key = conversationKey(transcript, translation);
      if (key === lastStoredConversationKey || chatHistory.some((entry) => entry.key === key)) {
        lastStoredConversationKey = key;
        return;
      }

      chatHistory.push({
        key,
        transcript,
        translation,
        createdAt: new Date().toISOString()
      });
      chatHistory = chatHistory.slice(-maxChatHistoryItems);
      lastStoredConversationKey = key;
      saveChatHistory();
      renderChatHistory();
    }

    function updateVoiceUi(runtime) {
      const mode = text(runtime.activityMode, "idle");
      const message = text(runtime.activityMessage, "");
      let stateName = "idle";
      let label = "Ready";
      let subtitle = message || (runtime.distanceSensorReady && runtime.lastDistanceMm <= 1000
        ? "Distance is ready for speech."
        : "Waiting for distance and microphone input.");

      if (mode === "listening" || runtime.sessionActive) {
        stateName = "listening";
        label = "Listening";
      } else if (mode === "speaking" || runtime.playbackActive) {
        stateName = "speaking";
        label = "Speaking";
      } else if (mode === "thinking" || runtime.processingActive) {
        stateName = "thinking";
        label = "Thinking";
      }

      voiceUi.orb.className = `voice-orb ${stateName}`;
      voiceUi.label.textContent = label;
      voiceUi.subtitle.textContent = subtitle;
      voiceUi.topbarStatus.textContent = subtitle;
    }

    function populateLanguageSelect(selectId, options, selectedCode, force = false) {
      if (!force && dirtyForms.translation) {
        return;
      }

      const select = document.getElementById(selectId);
      const normalizedSelectedCode = text(selectedCode, "");
      select.replaceChildren();

      let hasSelectedCode = false;
      for (const language of options) {
        const option = document.createElement("option");
        option.value = language.code;
        option.textContent = `${language.name} (${language.code})`;
        if (language.code === normalizedSelectedCode) {
          hasSelectedCode = true;
        }
        select.appendChild(option);
      }

      if (!hasSelectedCode && normalizedSelectedCode.length > 0) {
        const option = document.createElement("option");
        option.value = normalizedSelectedCode;
        option.textContent = `${normalizedSelectedCode} (saved but not in the supported list)`;
        select.appendChild(option);
      }

      select.value = normalizedSelectedCode;
    }

    async function loadLanguages() {
      const payload = await requestJson("/api/languages");
      sourceLanguageOptions = payload.sourceLanguages;
      targetLanguageOptions = payload.targetLanguages;
      populateLanguageSelect("sourceLanguage", sourceLanguageOptions, "auto", true);
      populateLanguageSelect("targetLanguage", targetLanguageOptions, "tr", true);
    }

    async function requestJson(path, options = {}) {
      const response = await fetch(path, {
        headers: { "Content-Type": "application/json" },
        ...options
      });
      const payload = await response.json();
      if (!response.ok || payload.ok === false) {
        throw new Error(payload.message || "Request failed.");
      }
      return payload;
    }

    function applyStatus(payload, forceFormUpdate = false) {
      const wifi = payload.wifi;
      const runtime = payload.runtime;
      const settings = payload.settings;
      const audio = payload.audio;
      const memory = payload.memory;

      statusIds.wifiState.textContent = wifi.connected ? `${text(wifi.ssid)} connected` : `${text(wifi.ssid, "No SSID")} not connected`;
      statusIds.ipAddress.textContent = text(wifi.ip);
      statusIds.sessionState.textContent = text(runtime.activityMessage, runtime.sessionActive ? "Recording" : runtime.playbackActive ? "Playing audio" : runtime.processingActive ? "Processing" : "Ready");
      statusIds.distance.textContent = runtime.distanceSensorReady ? `${runtime.lastDistanceMm} mm` : "-";
      statusIds.microphone.textContent = runtime.microphoneReady ? "Ready" : "Not ready";
      statusIds.speaker.textContent = runtime.speakerReady ? "Ready" : "Not ready";
      statusIds.sensor.textContent = runtime.distanceSensorReady ? "Ready" : "Not ready";
      statusIds.heap.textContent = `${memory.freeHeap} B`;
      statusIds.lastTranscript.textContent = text(runtime.lastTranscript);
      statusIds.lastTranslation.textContent = text(runtime.lastTranslation);
      updateVoiceUi(runtime);
      rememberCompletedConversation(runtime);

      setFieldValue("wifiSsid", settings.wifiSsid, "network", forceFormUpdate);
      setFieldValue("openRouterIpOverride", settings.openRouterIpOverride, "network", forceFormUpdate);
      setFieldValue("dnsMode", settings.useDhcpDns ? "dhcp" : "manual", "network", forceFormUpdate);
      setFieldValue("primaryDns", settings.primaryDns, "network", forceFormUpdate);
      setFieldValue("secondaryDns", settings.secondaryDns, "network", forceFormUpdate);
      populateLanguageSelect("sourceLanguage", sourceLanguageOptions, settings.sourceLanguage, forceFormUpdate);
      populateLanguageSelect("targetLanguage", targetLanguageOptions, settings.targetLanguage, forceFormUpdate);
      setFieldValue("sttModel", settings.sttModel, "translation", forceFormUpdate);
      setFieldValue("translationModel", settings.translationModel, "translation", forceFormUpdate);
      setFieldValue("ttsModel", settings.ttsModel, "translation", forceFormUpdate);
      setFieldValue("ttsVoice", settings.ttsVoice, "translation", forceFormUpdate);
      setFieldValue("ttsPcmRateHz", audio.ttsPcmRateHz, "translation", forceFormUpdate);
      setFieldValue("ttsSpeed", audio.ttsSpeed, "translation", forceFormUpdate);
      setFieldValue("playbackVolume", audio.playbackVolume, "translation", forceFormUpdate);
      setFieldValue("autoPresence", settings.autoPresence, "translation", forceFormUpdate);
    }

    async function refreshStatus(forceFormUpdate = false) {
      if (statusRefreshPromise !== null) {
        return statusRefreshPromise;
      }

      statusRefreshPromise = requestJson("/api/status")
        .then((statusPayload) => {
          applyStatus(statusPayload, forceFormUpdate);
        })
        .finally(() => {
          statusRefreshPromise = null;
        });
      return statusRefreshPromise;
    }

    async function saveSettings(payload, messageId, formName) {
      await requestJson("/api/settings", {
        method: "POST",
        body: JSON.stringify(payload)
      });
      setMessage(messageId, "Settings saved.");
      document.getElementById("wifiPassword").value = "";
      document.getElementById("openRouterApiKey").value = "";
      dirtyForms[formName] = false;
      await refreshStatus(true);
    }

    document.getElementById("networkForm").addEventListener("submit", async (event) => {
      event.preventDefault();
      const wifiPassword = document.getElementById("wifiPassword").value;
      const openRouterApiKey = document.getElementById("openRouterApiKey").value;
      const payload = {
        wifiSsid: document.getElementById("wifiSsid").value,
        openRouterIpOverride: document.getElementById("openRouterIpOverride").value,
        useDhcpDns: document.getElementById("dnsMode").value === "dhcp",
        primaryDns: document.getElementById("primaryDns").value,
        secondaryDns: document.getElementById("secondaryDns").value
      };
      if (wifiPassword.length > 0) {
        payload.wifiPassword = wifiPassword;
      }
      if (openRouterApiKey.length > 0) {
        payload.openRouterApiKey = openRouterApiKey;
      }
      try {
        await saveSettings(payload, "actionMessage", "network");
      } catch (error) {
        setMessage("actionMessage", error.message, true);
      }
    });

    document.getElementById("translationForm").addEventListener("submit", async (event) => {
      event.preventDefault();
      const payload = {
        sourceLanguage: document.getElementById("sourceLanguage").value,
        targetLanguage: document.getElementById("targetLanguage").value,
        sttModel: document.getElementById("sttModel").value,
        translationModel: document.getElementById("translationModel").value,
        ttsModel: document.getElementById("ttsModel").value,
        ttsVoice: document.getElementById("ttsVoice").value,
        ttsPcmRateHz: Number(document.getElementById("ttsPcmRateHz").value),
        ttsSpeed: Number(document.getElementById("ttsSpeed").value),
        playbackVolume: Number(document.getElementById("playbackVolume").value),
        autoPresence: document.getElementById("autoPresence").checked
      };
      try {
        await saveSettings(payload, "actionMessage", "translation");
      } catch (error) {
        setMessage("actionMessage", error.message, true);
      }
    });

    async function sendCommand(command, messageId = "terminalMessage") {
      await requestJson("/api/command", {
        method: "POST",
        body: JSON.stringify({ command })
      });
      setMessage(messageId, `Command sent: ${command}`);
      await refreshStatus();
    }

    document.getElementById("recordButton").addEventListener("click", async () => {
      try {
        await sendCommand("r", "actionMessage");
      } catch (error) {
        setMessage("actionMessage", error.message, true);
      }
    });

    document.getElementById("voiceRecordButton").addEventListener("click", async () => {
      try {
        await sendCommand("r", "voiceActionMessage");
      } catch (error) {
        setMessage("voiceActionMessage", error.message, true);
      }
    });

    document.getElementById("refreshButton").addEventListener("click", async () => {
      try {
        await refreshStatus();
        setMessage("actionMessage", "Status updated.");
      } catch (error) {
        setMessage("actionMessage", error.message, true);
      }
    });

    document.getElementById("voiceRefreshButton").addEventListener("click", async () => {
      try {
        await refreshStatus();
        setMessage("voiceActionMessage", "Status updated.");
      } catch (error) {
        setMessage("voiceActionMessage", error.message, true);
      }
    });

    document.getElementById("clearHistoryButton").addEventListener("click", () => {
      if (!confirm("Delete conversation history from this browser?")) {
        return;
      }
      chatHistory = [];
      lastStoredConversationKey = "";
      localStorage.removeItem(chatHistoryStorageKey);
      renderChatHistory();
      setMessage("actionMessage", "Conversation history cleared.");
    });

    document.getElementById("restartButton").addEventListener("click", async () => {
      if (!confirm("Restart the device?")) {
        return;
      }
      try {
        await requestJson("/api/restart", { method: "POST", body: "{}" });
        setMessage("actionMessage", "Restart scheduled. Refresh the page after a few seconds.");
      } catch (error) {
        setMessage("actionMessage", error.message, true);
      }
    });

    document.getElementById("clearButton").addEventListener("click", async () => {
      if (!confirm("Delete saved settings and restart the device?")) {
        return;
      }
      try {
        await requestJson("/api/clear", { method: "POST", body: "{}" });
        setMessage("actionMessage", "Settings cleared. The device will restart.");
      } catch (error) {
        setMessage("actionMessage", error.message, true);
      }
    });

    document.getElementById("sendCommandButton").addEventListener("click", async () => {
      const input = document.getElementById("terminalCommand");
      const command = input.value.trim();
      if (command.length === 0) {
        setMessage("terminalMessage", "Command cannot be empty.", true);
        return;
      }
      try {
        await sendCommand(command);
        input.value = "";
      } catch (error) {
        setMessage("terminalMessage", error.message, true);
      }
    });

    document.getElementById("terminalCommand").addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        document.getElementById("sendCommandButton").click();
      }
    });

    settingsToggleButton.addEventListener("click", () => {
      showSettingsScreen(settingsScreen.hidden);
    });

    for (const fieldId of networkFieldIds) {
      document.getElementById(fieldId).addEventListener("input", () => {
        dirtyForms.network = true;
      });
      document.getElementById(fieldId).addEventListener("change", () => {
        dirtyForms.network = true;
      });
    }

    for (const fieldId of translationFieldIds) {
      document.getElementById(fieldId).addEventListener("input", () => {
        dirtyForms.translation = true;
      });
      document.getElementById(fieldId).addEventListener("change", () => {
        dirtyForms.translation = true;
      });
    }

    loadChatHistory();
    renderChatHistory();
    loadLanguages()
      .then(() => refreshStatus(true))
      .catch((error) => setMessage("actionMessage", error.message, true));
    setInterval(() => refreshStatus().catch(() => {}), 800);
  </script>
</body>
</html>
)controlpanel";
