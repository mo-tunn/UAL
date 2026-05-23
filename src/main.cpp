#include <Arduino.h>
#include <Adafruit_VL53L0X.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <lwip/dns.h>
#include <mbedtls/base64.h>
#include <stdint.h>
#include "config/FirmwareConfig.h"
#include "domain/RuntimeTypes.h"
#include "io/DiagnosticConsole.h"
#include "i18n/SupportedLanguages.h"
#include "web/ControlPanelPage.h"

namespace {




void playPcmChunk(const uint8_t *payload, size_t length);
void processControlPanel();
void processSerialInput();


class Base64BodyWriter {
 public:
  /**
   * Creates a streaming base64 writer over a TLS client.
   *
   * Parameters:
   *   outputClient: Connected TLS client.
   * Returns: nothing.
   * Throws: never.
   */
  explicit Base64BodyWriter(WiFiClientSecure &outputClient) : client(outputClient) {}

  /**
   * Writes bytes as base64 while preserving 3-byte alignment between calls.
   *
   * Parameters:
   *   data: Raw bytes to encode.
   *   length: Number of bytes to encode.
   * Returns: true when all encoded bytes were written.
   * Throws: never.
   */
  bool write(const uint8_t *data, size_t length) {
    size_t offset = 0;

    if (carryLength > 0) {
      while (carryLength < sizeof(carryBytes) && offset < length) {
        carryBytes[carryLength] = data[offset];
        ++carryLength;
        ++offset;
      }

      if (carryLength == sizeof(carryBytes)) {
        if (!encodeAndWrite(carryBytes, sizeof(carryBytes))) {
          return false;
        }
        carryLength = 0;
      }
    }

    while (length - offset >= kBase64InputChunkBytes) {
      if (!encodeAndWrite(data + offset, kBase64InputChunkBytes)) {
        return false;
      }
      offset += kBase64InputChunkBytes;
    }

    const size_t alignedLength = ((length - offset) / 3) * 3;
    if (alignedLength > 0) {
      if (!encodeAndWrite(data + offset, alignedLength)) {
        return false;
      }
      offset += alignedLength;
    }

    while (offset < length) {
      carryBytes[carryLength] = data[offset];
      ++carryLength;
      ++offset;
    }

    return true;
  }

  /**
   * Flushes any final bytes with base64 padding.
   *
   * Parameters: none.
   * Returns: true when the final encoded bytes were written.
   * Throws: never.
   */
  bool finish() {
    if (carryLength == 0) {
      return true;
    }

    const bool result = encodeAndWrite(carryBytes, carryLength);
    carryLength = 0;
    return result;
  }

 private:
  WiFiClientSecure &client;
  uint8_t carryBytes[3] = {};
  size_t carryLength = 0;

  /**
   * Encodes a single complete block and writes it to the TLS client.
   *
   * Parameters:
   *   input: Raw bytes.
   *   inputLength: Number of raw bytes.
   * Returns: true when the encoded output was written completely.
   * Throws: never.
   */
  bool encodeAndWrite(const uint8_t *input, size_t inputLength) {
    uint8_t output[kBase64OutputChunkBytes + 8] = {};
    size_t outputLength = 0;
    const int result = mbedtls_base64_encode(
        output,
        sizeof(output),
        &outputLength,
        input,
        inputLength);

    if (result != 0 || outputLength == 0) {
      return false;
    }

    return client.write(output, outputLength) == outputLength;
  }
};

class PcmStreamPlayer {
 public:
  /**
   * Creates a PCM16 streaming player that preserves sample alignment.
   *
   * Parameters: none.
   * Returns: nothing.
   * Throws: never.
   */
  PcmStreamPlayer() = default;

  /**
   * Streams raw PCM16 bytes to the speaker.
   *
   * Parameters:
   *   data: Raw little-endian PCM16 mono bytes.
   *   length: Number of bytes available.
   * Returns: nothing.
   * Throws: never.
   */
  void write(const uint8_t *data, size_t length) {
    size_t offset = 0;

    if (hasPendingByte && length > 0) {
      uint8_t alignedSample[2] = {pendingByte, data[0]};
      playPcmChunk(alignedSample, sizeof(alignedSample));
      hasPendingByte = false;
      offset = 1;
    }

    while (length - offset >= kPcmPlaybackChunkBytes) {
      playPcmChunk(data + offset, kPcmPlaybackChunkBytes);
      offset += kPcmPlaybackChunkBytes;
    }

    const size_t evenLength = ((length - offset) / 2) * 2;
    if (evenLength > 0) {
      playPcmChunk(data + offset, evenLength);
      offset += evenLength;
    }

    if (offset < length) {
      pendingByte = data[offset];
      hasPendingByte = true;
    }
  }

  /**
   * Drops an incomplete trailing byte if the stream ended mid-sample.
   *
   * Parameters: none.
   * Returns: nothing.
   * Throws: never.
   */
  void finish() {
    hasPendingByte = false;
    pendingByte = 0;
  }

 private:
  uint8_t pendingByte = 0;
  bool hasPendingByte = false;
};

DiagnosticConsole Console;
Preferences preferences;
WebServer controlServer(kControlPanelPort);
DeviceSettings settings;
RuntimeState state;
Adafruit_VL53L0X distanceSensor;
char serialBuffer[kSerialBufferLength] = {};
size_t serialBufferIndex = 0;
uint8_t *recordedPcmBuffer = nullptr;
size_t recordedPcmLength = 0;
size_t recordedPcmCapacity = 0;
bool restartScheduled = false;
uint32_t restartScheduledAtMs = 0;

void printHelp();
void printStatus();
void startCaptureSession();
void handleSerialCommand(String commandLine);

/**
 * Updates the status fields consumed by the web voice UI.
 *
 * Parameters:
 *   mode: Stable UI activity mode.
 *   message: Human-readable status aligned with serial logs.
 * Returns: nothing.
 * Throws: never.
 */
void setActivityStatus(const __FlashStringHelper *mode, const __FlashStringHelper *message) {
  state.activityMode = String(mode);
  state.activityMessage = String(message);
}


/**
 * Prints an ESP-IDF error with context.
 *
 * Parameters:
 *   context: Operation that failed.
 *   errorCode: ESP-IDF error code.
 * Returns: nothing.
 * Throws: never.
 */
void printEspError(const __FlashStringHelper *context, esp_err_t errorCode) {
  Console.print(context);
  Console.print(F(" error: "));
  Console.print(esp_err_to_name(errorCode));
  Console.print(F(" ("));
  Console.print(static_cast<int>(errorCode));
  Console.println(F(")"));
}

/**
 * Clamps a signed 32-bit value to int16 range.
 *
 * Parameters:
 *   value: Value to clamp.
 * Returns: Clamped signed 16-bit sample.
 * Throws: never.
 */
int16_t clampToInt16(int32_t value) {
  if (value > INT16_MAX) {
    return INT16_MAX;
  }
  if (value < INT16_MIN) {
    return INT16_MIN;
  }
  return static_cast<int16_t>(value);
}

/**
 * Returns a masked API key suitable for serial diagnostics.
 *
 * Parameters:
 *   apiKey: Full OpenRouter key.
 * Returns: Masked key text.
 * Throws: never.
 */
String maskSecret(const String &apiKey) {
  if (apiKey.length() == 0) {
    return String("(not set)");
  }

  if (apiKey.length() <= 10) {
    return String("***");
  }

  return apiKey.substring(0, 6) + String("...") + apiKey.substring(apiKey.length() - 4);
}

/**
 * Validates a short language code used in OpenRouter requests.
 *
 * Parameters:
 *   value: Raw language code.
 * Returns: true when the language code has a safe shape.
 * Throws: never.
 */
bool isValidLanguageCode(const String &value) {
  if (value.length() < 2 || value.length() > 17) {
    return false;
  }

  for (size_t index = 0; index < value.length(); ++index) {
    const char character = value.charAt(index);
    const bool isLowercaseLetter = character >= 'a' && character <= 'z';
    const bool isDigit = character >= '0' && character <= '9';
    const bool isDash = character == '-';
    if (!isLowercaseLetter && !isDigit && !isDash) {
      return false;
    }
  }

  return true;
}

/**
 * Lowercases ASCII language tags without altering other UTF-8 text paths.
 *
 * Parameters:
 *   value: Language code.
 * Returns: Lowercase language code.
 * Throws: never.
 */
String normalizeLanguageCode(String value) {
  value.trim();
  value.toLowerCase();
  return value;
}

/**
 * Checks whether a language code is supported by the configured STT/TTS pipeline.
 *
 * Parameters:
 *   languageCode: Normalized language code.
 *   allowAutoDetect: Whether the special auto-detect source language code is accepted.
 * Returns: true when the language is supported.
 * Throws: never.
 */
bool isSupportedPipelineLanguageCode(const String &languageCode, bool allowAutoDetect) {
  if (allowAutoDetect && languageCode == kAutoDetectLanguageCode) {
    return true;
  }

  for (size_t index = 0; index < kSupportedLanguageCount; ++index) {
    if (languageCode == kSupportedLanguages[index].code) {
      return true;
    }
  }

  return false;
}

/**
 * Returns a readable language label for prompts and diagnostics.
 *
 * Parameters:
 *   languageCode: Normalized language code.
 * Returns: Human-readable language label with the code.
 * Throws: never.
 */
String languageLabelForCode(const String &languageCode) {
  if (languageCode == kAutoDetectLanguageCode) {
    return F("Auto-detected language");
  }

  for (size_t index = 0; index < kSupportedLanguageCount; ++index) {
    if (languageCode == kSupportedLanguages[index].code) {
      return String(kSupportedLanguages[index].name) + F(" (") + languageCode + F(")");
    }
  }

  return languageCode;
}

/**
 * Converts a user string into a JSON string literal.
 *
 * Parameters:
 *   value: UTF-8 text to quote.
 * Returns: JSON string literal preserving UTF-8 bytes.
 * Throws: never.
 */
String quoteJsonString(const String &value) {
  String quoted;
  quoted.reserve(value.length() + 8);
  quoted += '"';

  for (size_t index = 0; index < value.length(); ++index) {
    const uint8_t character = static_cast<uint8_t>(value.charAt(index));
    switch (character) {
      case '"':
        quoted += F("\\\"");
        break;
      case '\\':
        quoted += F("\\\\");
        break;
      case '\b':
        quoted += F("\\b");
        break;
      case '\f':
        quoted += F("\\f");
        break;
      case '\n':
        quoted += F("\\n");
        break;
      case '\r':
        quoted += F("\\r");
        break;
      case '\t':
        quoted += F("\\t");
        break;
      default:
        if (character < 0x20) {
          char escapeBuffer[7] = {};
          snprintf(escapeBuffer, sizeof(escapeBuffer), "\\u%04x", character);
          quoted += escapeBuffer;
        } else {
          quoted += static_cast<char>(character);
        }
        break;
    }
  }

  quoted += '"';
  return quoted;
}

/**
 * Restores the in-memory settings object to factory defaults.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void resetSettingsToDefaults() {
  settings = DeviceSettings{};
  settings.sourceLanguage = kDefaultSourceLanguage;
  settings.targetLanguage = kDefaultTargetLanguage;
  settings.sttModel = kDefaultSttModel;
  settings.translationModel = kDefaultTranslationModel;
  settings.ttsModel = kDefaultTtsModel;
  settings.ttsVoice = kDefaultTtsVoice;
  settings.primaryDns = kDefaultPrimaryDns;
  settings.secondaryDns = kDefaultSecondaryDns;
  settings.openRouterIpOverride = "";
  settings.ttsPcmRateHz = kDefaultTtsPcmRateHz;
  settings.ttsSpeed = kDefaultTtsSpeed;
  settings.playbackVolume = kDefaultPlaybackVolume;
  settings.useDhcpDns = false;
  settings.autoPresence = true;
}

/**
 * Checks whether a String contains a valid IPv4 address.
 *
 * Parameters:
 *   value: Address text.
 * Returns: true when value is empty or a valid IPv4 address according to allowEmpty.
 * Throws: never.
 */
bool isValidIpv4Address(const String &value, bool allowEmpty) {
  if (value.length() == 0) {
    return allowEmpty;
  }

  IPAddress parsedAddress;
  return parsedAddress.fromString(value);
}

/**
 * Adds common browser security and cache headers to a web response.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void sendCommonWebHeaders() {
  controlServer.sendHeader(F("Cache-Control"), F("no-store"));
  controlServer.sendHeader(F("X-Content-Type-Options"), F("nosniff"));
  controlServer.sendHeader(F("Referrer-Policy"), F("no-referrer"));
  controlServer.sendHeader(
      F("Content-Security-Policy"),
      F("default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'"));
}

/**
 * Sends a JSON document to the active web client.
 *
 * Parameters:
 *   statusCode: HTTP status code.
 *   document: JSON document to serialize.
 * Returns: nothing.
 * Throws: never.
 */
void sendJsonDocument(int statusCode, JsonDocument &document) {
  String responseBody;
  serializeJson(document, responseBody);
  sendCommonWebHeaders();
  controlServer.send(statusCode, F("application/json; charset=utf-8"), responseBody);
}

/**
 * Sends a small JSON success or error message.
 *
 * Parameters:
 *   statusCode: HTTP status code.
 *   ok: Operation result flag.
 *   message: User-facing message.
 * Returns: nothing.
 * Throws: never.
 */
void sendJsonMessage(int statusCode, bool ok, const String &message) {
  JsonDocument document;
  document["ok"] = ok;
  document["message"] = message;
  sendJsonDocument(statusCode, document);
}

/**
 * Reads and parses a JSON request body from the active web client.
 *
 * Parameters:
 *   document: Output parsed JSON document.
 *   errorMessage: Output parse or size error message.
 * Returns: true when the request body contains valid JSON.
 * Throws: never.
 */
bool readJsonRequest(JsonDocument &document, String &errorMessage) {
  const String body = controlServer.arg(F("plain"));
  if (body.length() == 0) {
    errorMessage = F("JSON body is empty.");
    return false;
  }

  if (body.length() > kWebRequestBodyLimitBytes) {
    errorMessage = F("JSON body is too large.");
    return false;
  }

  const DeserializationError error = deserializeJson(document, body);
  if (error) {
    errorMessage = String(F("Could not parse JSON: ")) + error.c_str();
    return false;
  }

  if (!document.is<JsonObject>()) {
    errorMessage = F("JSON body must be an object.");
    return false;
  }

  return true;
}

/**
 * Reads an optional string field from a JSON document.
 *
 * Parameters:
 *   document: Parsed JSON document.
 *   key: Field name.
 *   destination: Output string when the field exists.
 *   maxLength: Maximum accepted byte length.
 *   allowEmpty: Whether an empty value is valid.
 *   errorMessage: Output validation error.
 * Returns: true when the field is absent or valid.
 * Throws: never.
 */
bool readOptionalJsonString(
    JsonDocument &document,
    const char *key,
    String &destination,
    size_t maxLength,
    bool allowEmpty,
    String &errorMessage) {
  JsonVariantConst value = document[key];
  if (value.isNull()) {
    return true;
  }

  if (!value.is<const char *>()) {
    errorMessage = String(key) + F(" must be text.");
    return false;
  }

  String parsedValue = value.as<String>();
  parsedValue.trim();
  if (!allowEmpty && parsedValue.length() == 0) {
    errorMessage = String(key) + F(" cannot be empty.");
    return false;
  }

  if (parsedValue.length() > maxLength) {
    errorMessage = String(key) + F(" is too long.");
    return false;
  }

  destination = parsedValue;
  return true;
}

/**
 * Reads an optional boolean field from a JSON document.
 *
 * Parameters:
 *   document: Parsed JSON document.
 *   key: Field name.
 *   destination: Output bool when the field exists.
 *   errorMessage: Output validation error.
 * Returns: true when the field is absent or valid.
 * Throws: never.
 */
bool readOptionalJsonBool(
    JsonDocument &document,
    const char *key,
    bool &destination,
    String &errorMessage) {
  JsonVariantConst value = document[key];
  if (value.isNull()) {
    return true;
  }

  if (!value.is<bool>()) {
    errorMessage = String(key) + F(" must be true or false.");
    return false;
  }

  destination = value.as<bool>();
  return true;
}

/**
 * Reads an optional integer field from a JSON document.
 *
 * Parameters:
 *   document: Parsed JSON document.
 *   key: Field name.
 *   destination: Output integer when the field exists.
 *   minimumValue: Minimum accepted value.
 *   maximumValue: Maximum accepted value.
 *   errorMessage: Output validation error.
 * Returns: true when the field is absent or valid.
 * Throws: never.
 */
bool readOptionalJsonUInt(
    JsonDocument &document,
    const char *key,
    uint32_t &destination,
    uint32_t minimumValue,
    uint32_t maximumValue,
    String &errorMessage) {
  JsonVariantConst value = document[key];
  if (value.isNull()) {
    return true;
  }

  if (!value.is<uint32_t>()) {
    errorMessage = String(key) + F(" must be a number.");
    return false;
  }

  const uint32_t parsedValue = value.as<uint32_t>();
  if (parsedValue < minimumValue || parsedValue > maximumValue) {
    errorMessage = String(key) + F(" is out of range.");
    return false;
  }

  destination = parsedValue;
  return true;
}

/**
 * Reads an optional floating-point field from a JSON document.
 *
 * Parameters:
 *   document: Parsed JSON document.
 *   key: Field name.
 *   destination: Output float when the field exists.
 *   minimumValue: Minimum accepted value.
 *   maximumValue: Maximum accepted value.
 *   errorMessage: Output validation error.
 * Returns: true when the field is absent or valid.
 * Throws: never.
 */
bool readOptionalJsonFloat(
    JsonDocument &document,
    const char *key,
    float &destination,
    float minimumValue,
    float maximumValue,
    String &errorMessage) {
  JsonVariantConst value = document[key];
  if (value.isNull()) {
    return true;
  }

  if (!value.is<float>()) {
    errorMessage = String(key) + F(" must be a number.");
    return false;
  }

  const float parsedValue = value.as<float>();
  if (parsedValue < minimumValue || parsedValue > maximumValue) {
    errorMessage = String(key) + F(" is out of range.");
    return false;
  }

  destination = parsedValue;
  return true;
}

/**
 * Schedules a device restart after the current HTTP response is flushed.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void scheduleDeviceRestart() {
  restartScheduled = true;
  restartScheduledAtMs = millis() + kWebRestartDelayMs;
}

/**
 * Handles a scheduled restart when its delay has elapsed.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void processScheduledRestart() {
  if (!restartScheduled) {
    return;
  }

  const int32_t remainingMs = static_cast<int32_t>(restartScheduledAtMs - millis());
  if (remainingMs > 0) {
    return;
  }

  Console.println(F("Web panel restart request is being applied."));
  delay(50);
  ESP.restart();
}

/**
 * Loads saved device settings from NVS Preferences.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void loadSettings() {
  preferences.begin(kPreferencesNamespace, false);
  settings.wifiSsid = preferences.getString("ssid", "");
  settings.wifiPassword = preferences.getString("password", "");
  settings.openRouterApiKey = preferences.getString("orkey", "");
  settings.sourceLanguage = preferences.getString("source", kDefaultSourceLanguage);
  settings.targetLanguage = preferences.getString("target", kDefaultTargetLanguage);
  settings.sourceLanguage = normalizeLanguageCode(settings.sourceLanguage);
  settings.targetLanguage = normalizeLanguageCode(settings.targetLanguage);
  if (!isSupportedPipelineLanguageCode(settings.sourceLanguage, true)) {
    settings.sourceLanguage = kDefaultSourceLanguage;
    preferences.putString("source", settings.sourceLanguage);
  }
  if (!isSupportedPipelineLanguageCode(settings.targetLanguage, false)) {
    settings.targetLanguage = kDefaultTargetLanguage;
    preferences.putString("target", settings.targetLanguage);
  }
  settings.sttModel = preferences.getString("sttmodel", kDefaultSttModel);
  settings.translationModel = preferences.getString("trmodel", kDefaultTranslationModel);
  settings.ttsModel = preferences.getString("ttsmodel", kDefaultTtsModel);
  if (settings.ttsModel == kMp3OnlyMistralTtsModel) {
    settings.ttsModel = kDefaultTtsModel;
    preferences.putString("ttsmodel", settings.ttsModel);
  }
  settings.ttsVoice = preferences.getString("voice", kDefaultTtsVoice);
  settings.primaryDns = preferences.getString("dns1", kDefaultPrimaryDns);
  settings.secondaryDns = preferences.getString("dns2", kDefaultSecondaryDns);
  settings.openRouterIpOverride = preferences.getString("orip", "");
  settings.ttsPcmRateHz = preferences.getUInt("ttsrate", kDefaultTtsPcmRateHz);
  settings.ttsSpeed = preferences.getFloat("ttsspeed", kDefaultTtsSpeed);
  if (settings.ttsSpeed < 0.25F || settings.ttsSpeed > 4.0F) {
    settings.ttsSpeed = kDefaultTtsSpeed;
    preferences.putFloat("ttsspeed", settings.ttsSpeed);
  }
  settings.playbackVolume = preferences.getFloat("volume", kDefaultPlaybackVolume);
  if (settings.playbackVolume < 0.1F || settings.playbackVolume > 12.0F) {
    settings.playbackVolume = kDefaultPlaybackVolume;
    preferences.putFloat("volume", settings.playbackVolume);
  }
  settings.useDhcpDns = preferences.getBool("dnsauto", false);
  settings.autoPresence = preferences.getBool("presence", true);
}

/**
 * Stores one string setting in NVS.
 *
 * Parameters:
 *   key: NVS key.
 *   value: String value to store.
 * Returns: nothing.
 * Throws: never.
 */
void saveStringSetting(const char *key, const String &value) {
  preferences.putString(key, value);
}

/**
 * Allocates the fixed recording buffer, preferring PSRAM.
 *
 * Parameters: none.
 * Returns: true when recording memory is available.
 * Throws: never.
 */
bool allocateRecordingBuffer() {
  recordedPcmCapacity = kMaxRecordedPcmBytes;

  if (psramFound()) {
    recordedPcmBuffer = static_cast<uint8_t *>(
        heap_caps_malloc(recordedPcmCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }

  if (recordedPcmBuffer == nullptr) {
    recordedPcmBuffer = static_cast<uint8_t *>(malloc(recordedPcmCapacity));
  }

  if (recordedPcmBuffer == nullptr) {
    Console.print(F("Audio recording buffer could not be allocated. Required bytes: "));
    Console.println(recordedPcmCapacity);
    recordedPcmCapacity = 0;
    return false;
  }

  Console.print(F("Audio recording buffer ready. Bytes: "));
  Console.print(recordedPcmCapacity);
  Console.print(F(", PSRAM: "));
  Console.println(psramFound() ? F("available") : F("not available"));
  return true;
}

/**
 * Applies one IPv4 DNS server to the lwIP resolver.
 *
 * Parameters:
 *   index: DNS server slot.
 *   rawAddress: IPv4 address text.
 * Returns: true when the address was parsed and applied.
 * Throws: never.
 */
bool applyDnsServer(uint8_t index, const String &rawAddress) {
  IPAddress dnsAddress;
  if (!dnsAddress.fromString(rawAddress)) {
    Console.print(F("Invalid DNS address: "));
    Console.println(rawAddress);
    return false;
  }

  ip_addr_t lwipDnsAddress;
  IP_ADDR4(
      &lwipDnsAddress,
      dnsAddress[0],
      dnsAddress[1],
      dnsAddress[2],
      dnsAddress[3]);
  dns_setserver(index, &lwipDnsAddress);
  return true;
}

/**
 * Applies configured DNS behavior after Wi-Fi connects.
 *
 * Parameters: none.
 * Returns: true when DNS configuration is usable.
 * Throws: never.
 */
bool applyConfiguredDns() {
  if (settings.useDhcpDns) {
    Console.print(F("DNS DHCP: "));
    Console.print(WiFi.dnsIP(0));
    Console.print(F(", "));
    Console.println(WiFi.dnsIP(1));
    return true;
  }

  const bool primaryApplied = applyDnsServer(0, settings.primaryDns);
  const bool secondaryApplied = applyDnsServer(1, settings.secondaryDns);

  Console.print(F("DNS configured: "));
  Console.print(settings.primaryDns);
  Console.print(F(", "));
  Console.println(settings.secondaryDns);
  return primaryApplied && secondaryApplied;
}

/**
 * Resolves OpenRouter through the active ESP32 DNS configuration.
 *
 * Parameters:
 *   resolvedAddress: Output IPv4 address.
 *   errorMessage: Output error text.
 * Returns: true when the host resolved.
 * Throws: never.
 */
bool resolveOpenRouterHost(IPAddress &resolvedAddress, String &errorMessage) {
  if (settings.openRouterIpOverride.length() > 0) {
    if (!resolvedAddress.fromString(settings.openRouterIpOverride)) {
      errorMessage = String(F("Saved OpenRouter IP is invalid: ")) + settings.openRouterIpOverride;
      return false;
    }

    Console.print(F("OpenRouter DNS skipped: "));
    Console.print(kOpenRouterHost);
    Console.print(F(" -> "));
    Console.println(resolvedAddress);
    return true;
  }

  if (WiFi.hostByName(kOpenRouterHost, resolvedAddress) != 1) {
    errorMessage = String(F("DNS resolution failed: ")) + kOpenRouterHost +
                   String(F(". Try 'dns 1.1.1.1 8.8.8.8' or 'orip <ip>'."));
    return false;
  }

  Console.print(F("OpenRouter DNS: "));
  Console.print(kOpenRouterHost);
  Console.print(F(" -> "));
  Console.println(resolvedAddress);
  return true;
}

/**
 * Connects to Wi-Fi if credentials are available.
 *
 * Parameters: none.
 * Returns: true when Wi-Fi is connected, otherwise false.
 * Throws: never.
 */
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  if (settings.wifiSsid.length() == 0) {
    return false;
  }

  const uint32_t now = millis();
  if (state.lastWiFiAttemptAtMs != 0 && now - state.lastWiFiAttemptAtMs < kWiFiReconnectIntervalMs) {
    return false;
  }
  state.lastWiFiAttemptAtMs = now;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  processControlPanel();
  WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPassword.c_str());

  Console.print(F("Connecting to Wi-Fi: "));
  Console.println(settings.wifiSsid);

  const uint32_t startedAtMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAtMs < kWiFiConnectTimeoutMs) {
    processControlPanel();
    delay(250);
    Console.print('.');
  }
  Console.println();

  if (WiFi.status() != WL_CONNECTED) {
    Console.println(F("Wi-Fi connection failed."));
    return false;
  }

  Console.print(F("Wi-Fi connected. IP: "));
  Console.println(WiFi.localIP());
  applyConfiguredDns();
  return true;
}

/**
 * Initializes the VL53L0X distance sensor on the current wiring.
 *
 * Parameters: none.
 * Returns: true when the sensor is ready, otherwise false.
 * Throws: never.
 */
bool initializeDistanceSensor() {
  Wire.begin(kVl53l0xSdaPin, kVl53l0xSclPin);
  Wire.setClock(kI2cClockHz);
  Wire.setTimeOut(kI2cTimeoutMs);

  Wire.beginTransmission(kVl53l0xI2cAddress);
  if (Wire.endTransmission() != 0) {
    Console.println(F("VL53L0X not found; manual recording is available with serial command 'r'."));
    return false;
  }

  if (!distanceSensor.begin(kVl53l0xI2cAddress, false, &Wire)) {
    Console.println(F("VL53L0X could not be started; manual recording is available with serial command 'r'."));
    return false;
  }

  Console.println(F("VL53L0X ready."));
  return true;
}

/**
 * Initializes the INMP441 microphone I2S input.
 *
 * Parameters: none.
 * Returns: true when I2S RX is ready, otherwise false.
 * Throws: never.
 */
bool initializeMicrophone() {
  const i2s_config_t microphoneConfig = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = kAudioSampleRateHz,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = kAudioChunkSamples,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };

  const i2s_pin_config_t microphonePins = {
      .bck_io_num = kMicrophoneSckPin,
      .ws_io_num = kMicrophoneWsPin,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = kMicrophoneSdPin,
  };

  esp_err_t result = i2s_driver_install(kMicrophoneI2sPort, &microphoneConfig, 0, nullptr);
  if (result != ESP_OK) {
    printEspError(F("INMP441 I2S driver setup"), result);
    return false;
  }

  result = i2s_set_pin(kMicrophoneI2sPort, &microphonePins);
  if (result != ESP_OK) {
    printEspError(F("INMP441 I2S pin assignment"), result);
    i2s_driver_uninstall(kMicrophoneI2sPort);
    return false;
  }

  i2s_zero_dma_buffer(kMicrophoneI2sPort);
  Console.println(F("INMP441 ready."));
  return true;
}

/**
 * Initializes or reinitializes MAX98357A I2S output.
 *
 * Parameters:
 *   sampleRate: Output sample rate in Hz.
 * Returns: true when I2S TX is ready, otherwise false.
 * Throws: never.
 */
bool initializeSpeaker(uint32_t sampleRate) {
  if (state.speakerReady) {
    i2s_driver_uninstall(kSpeakerI2sPort);
    state.speakerReady = false;
  }

  const i2s_config_t speakerConfig = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = sampleRate,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
  };

  const i2s_pin_config_t speakerPins = {
      .bck_io_num = kSpeakerBclkPin,
      .ws_io_num = kSpeakerLrcPin,
      .data_out_num = kSpeakerDinPin,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };

  esp_err_t result = i2s_driver_install(kSpeakerI2sPort, &speakerConfig, 0, nullptr);
  if (result != ESP_OK) {
    printEspError(F("MAX98357A I2S driver setup"), result);
    return false;
  }

  result = i2s_set_pin(kSpeakerI2sPort, &speakerPins);
  if (result != ESP_OK) {
    printEspError(F("MAX98357A I2S pin assignment"), result);
    i2s_driver_uninstall(kSpeakerI2sPort);
    return false;
  }

  i2s_zero_dma_buffer(kSpeakerI2sPort);
  state.currentSpeakerSampleRate = sampleRate;
  state.speakerReady = true;
  Console.print(F("MAX98357A ready. Sample rate: "));
  Console.println(sampleRate);
  return true;
}

/**
 * Reads one HTTP line with a timeout.
 *
 * Parameters:
 *   client: Connected TLS client.
 *   line: Output line without CR/LF.
 *   timeoutMs: Read timeout in milliseconds.
 * Returns: true when a line was read.
 * Throws: never.
 */
bool readHttpLine(WiFiClientSecure &client, String &line, uint32_t timeoutMs) {
  line = "";
  const uint32_t startedAtMs = millis();

  while (millis() - startedAtMs < timeoutMs) {
    processControlPanel();
    while (client.available() > 0) {
      const char character = static_cast<char>(client.read());
      if (character == '\r') {
        continue;
      }
      if (character == '\n') {
        return true;
      }
      if (line.length() >= kHttpLineLimit) {
        return false;
      }
      line += character;
    }

    if (!client.connected() && client.available() == 0) {
      return line.length() > 0;
    }

    delay(1);
  }

  return false;
}

/**
 * Opens a TLS connection to OpenRouter.
 *
 * Parameters:
 *   client: TLS client to configure and connect.
 *   errorMessage: Output error text.
 * Returns: true when the TLS connection is open.
 * Throws: never.
 */
bool connectOpenRouter(WiFiClientSecure &client, String &errorMessage) {
  if (WiFi.status() != WL_CONNECTED && !connectWiFi()) {
    errorMessage = F("Wi-Fi is not connected");
    return false;
  }

  applyConfiguredDns();

  IPAddress resolvedAddress;
  if (!resolveOpenRouterHost(resolvedAddress, errorMessage)) {
    return false;
  }

  client.setInsecure();
  client.setTimeout(kHttpReadTimeoutMs / 1000);

  const int connectResult = settings.openRouterIpOverride.length() > 0
                                ? client.connect(
                                      resolvedAddress,
                                      kOpenRouterPort,
                                      kOpenRouterHost,
                                      nullptr,
                                      nullptr,
                                      nullptr)
                                : client.connect(kOpenRouterHost, kOpenRouterPort);

  if (connectResult != 1) {
    errorMessage = String(F("OpenRouter TLS connection failed. DNS IP: ")) +
                   resolvedAddress.toString();
    return false;
  }

  return true;
}

/**
 * Sends HTTP request headers for a JSON POST to OpenRouter.
 *
 * Parameters:
 *   client: Connected TLS client.
 *   path: OpenRouter API path.
 *   contentLength: Exact request body length in bytes.
 * Returns: true when all header bytes were queued.
 * Throws: never.
 */
bool sendOpenRouterPostHeaders(WiFiClientSecure &client, const char *path, size_t contentLength) {
  client.print(F("POST "));
  client.print(path);
  client.println(F(" HTTP/1.1"));
  client.print(F("Host: "));
  client.println(kOpenRouterHost);
  client.println(F("User-Agent: ESP32-S3-IoT-Translator/1.0"));
  client.print(F("Authorization: Bearer "));
  client.println(settings.openRouterApiKey);
  client.println(F("Content-Type: application/json"));
  client.println(F("Accept: application/json, audio/pcm, */*"));
  client.println(F("X-Title: ESP32 IoT Translator"));
  client.print(F("Content-Length: "));
  client.println(contentLength);
  client.println(F("Connection: close"));
  client.println();
  return client.connected();
}

/**
 * Parses an HTTP status line and response headers.
 *
 * Parameters:
 *   client: Connected TLS client.
 *   headers: Output header data.
 *   errorMessage: Output error text.
 * Returns: true when a valid HTTP response header block was parsed.
 * Throws: never.
 */
bool readHttpResponseHeaders(
    WiFiClientSecure &client,
    HttpResponseHeaders &headers,
    String &errorMessage) {
  String line;
  if (!readHttpLine(client, line, kHttpReadTimeoutMs)) {
    errorMessage = F("HTTP status line could not be read");
    return false;
  }

  const int firstSpace = line.indexOf(' ');
  const int secondSpace = line.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0) {
    errorMessage = F("Invalid HTTP status line");
    return false;
  }

  headers.statusCode = line.substring(firstSpace + 1, secondSpace).toInt();
  headers.contentLength = -1;
  headers.isChunked = false;
  headers.contentType = "";

  while (readHttpLine(client, line, kHttpReadTimeoutMs)) {
    line.trim();
    if (line.length() == 0) {
      return true;
    }

    const int colonIndex = line.indexOf(':');
    if (colonIndex < 0) {
      continue;
    }

    String headerName = line.substring(0, colonIndex);
    String headerValue = line.substring(colonIndex + 1);
    headerName.trim();
    headerName.toLowerCase();
    headerValue.trim();

    if (headerName == "content-length") {
      headers.contentLength = headerValue.toInt();
    } else if (headerName == "transfer-encoding") {
      String lowerValue = headerValue;
      lowerValue.toLowerCase();
      headers.isChunked = lowerValue.indexOf("chunked") >= 0;
    } else if (headerName == "content-type") {
      headers.contentType = headerValue;
      headers.contentType.toLowerCase();
    }
  }

  errorMessage = F("HTTP header block could not be completed");
  return false;
}

/**
 * Reads bytes from a TLS client with a timeout.
 *
 * Parameters:
 *   client: Connected TLS client.
 *   buffer: Destination buffer.
 *   requestedLength: Maximum bytes to read.
 *   timeoutMs: Timeout in milliseconds.
 * Returns: Number of bytes read.
 * Throws: never.
 */
size_t readNetworkBytes(
    WiFiClientSecure &client,
    uint8_t *buffer,
    size_t requestedLength,
    uint32_t timeoutMs) {
  const uint32_t startedAtMs = millis();

  while (millis() - startedAtMs < timeoutMs) {
    processControlPanel();
    const int availableBytes = client.available();
    if (availableBytes > 0) {
      const size_t bytesToRead = min(requestedLength, static_cast<size_t>(availableBytes));
      return client.read(buffer, bytesToRead);
    }

    if (!client.connected()) {
      return 0;
    }

    delay(1);
  }

  return 0;
}

/**
 * Appends HTTP body bytes to a String with an upper bound.
 *
 * Parameters:
 *   body: Destination string.
 *   data: Bytes to append.
 *   length: Number of bytes.
 *   maxBytes: Maximum accepted body length.
 *   errorMessage: Output error text.
 * Returns: true when bytes were appended.
 * Throws: never.
 */
bool appendBodyBytes(
    String &body,
    const uint8_t *data,
    size_t length,
    size_t maxBytes,
    String &errorMessage) {
  if (body.length() + length > maxBytes) {
    errorMessage = F("HTTP response is larger than expected");
    return false;
  }

  body.reserve(body.length() + length + 1);
  for (size_t index = 0; index < length; ++index) {
    body += static_cast<char>(data[index]);
  }
  return true;
}

/**
 * Releases memory owned by an AudioBuffer.
 *
 * Parameters:
 *   audioBuffer: Buffer to release.
 * Returns: nothing.
 * Throws: never.
 */
void freeAudioBuffer(AudioBuffer &audioBuffer) {
  if (audioBuffer.data != nullptr) {
    free(audioBuffer.data);
  }

  audioBuffer.data = nullptr;
  audioBuffer.length = 0;
  audioBuffer.capacity = 0;
}

/**
 * Ensures that an AudioBuffer can hold at least the requested number of bytes.
 *
 * Parameters:
 *   audioBuffer: Buffer to grow.
 *   requiredCapacity: Required byte capacity.
 *   errorMessage: Output error text.
 * Returns: true when the buffer has enough capacity.
 * Throws: never.
 */
bool ensureAudioBufferCapacity(
    AudioBuffer &audioBuffer,
    size_t requiredCapacity,
    String &errorMessage) {
  if (requiredCapacity <= audioBuffer.capacity) {
    return true;
  }

  if (requiredCapacity > kMaxTtsAudioBytes) {
    errorMessage = F("TTS audio is larger than expected");
    return false;
  }

  size_t newCapacity = audioBuffer.capacity == 0 ? 65536 : audioBuffer.capacity;
  while (newCapacity < requiredCapacity) {
    newCapacity *= 2;
    if (newCapacity > kMaxTtsAudioBytes) {
      newCapacity = kMaxTtsAudioBytes;
      break;
    }
  }

  uint8_t *newData = nullptr;
  if (psramFound()) {
    newData = static_cast<uint8_t *>(
        heap_caps_realloc(audioBuffer.data, newCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }

  if (newData == nullptr) {
    newData = static_cast<uint8_t *>(realloc(audioBuffer.data, newCapacity));
  }

  if (newData == nullptr) {
    errorMessage = F("TTS audio buffer could not be allocated");
    return false;
  }

  audioBuffer.data = newData;
  audioBuffer.capacity = newCapacity;
  return true;
}

/**
 * Appends bytes to an AudioBuffer.
 *
 * Parameters:
 *   audioBuffer: Destination buffer.
 *   data: Bytes to append.
 *   length: Number of bytes to append.
 *   errorMessage: Output error text.
 * Returns: true when bytes were appended.
 * Throws: never.
 */
bool appendAudioBytes(
    AudioBuffer &audioBuffer,
    const uint8_t *data,
    size_t length,
    String &errorMessage) {
  if (!ensureAudioBufferCapacity(audioBuffer, audioBuffer.length + length, errorMessage)) {
    return false;
  }

  memcpy(audioBuffer.data + audioBuffer.length, data, length);
  audioBuffer.length += length;
  return true;
}

/**
 * Reads a complete HTTP response body into a bounded String.
 *
 * Parameters:
 *   client: Connected TLS client.
 *   headers: Parsed HTTP headers.
 *   maxBytes: Maximum accepted body length.
 *   body: Output response body.
 *   errorMessage: Output error text.
 * Returns: true when the body was read successfully.
 * Throws: never.
 */
bool readHttpBodyToString(
    WiFiClientSecure &client,
    const HttpResponseHeaders &headers,
    size_t maxBytes,
    String &body,
    String &errorMessage) {
  body = "";
  uint8_t buffer[512] = {};

  if (headers.isChunked) {
    while (true) {
      String chunkSizeLine;
      if (!readHttpLine(client, chunkSizeLine, kHttpReadTimeoutMs)) {
        errorMessage = F("Chunk size could not be read");
        return false;
      }

      chunkSizeLine.trim();
      const int extensionIndex = chunkSizeLine.indexOf(';');
      if (extensionIndex >= 0) {
        chunkSizeLine = chunkSizeLine.substring(0, extensionIndex);
      }

      const size_t chunkSize = strtoul(chunkSizeLine.c_str(), nullptr, 16);
      if (chunkSize == 0) {
        String trailerLine;
        while (readHttpLine(client, trailerLine, 1000)) {
          trailerLine.trim();
          if (trailerLine.length() == 0) {
            break;
          }
        }
        return true;
      }

      size_t remaining = chunkSize;
      while (remaining > 0) {
        const size_t bytesToRead = min(remaining, sizeof(buffer));
        const size_t bytesRead = readNetworkBytes(client, buffer, bytesToRead, kHttpReadTimeoutMs);
        if (bytesRead == 0) {
          errorMessage = F("Chunk data could not be read");
          return false;
        }
        if (!appendBodyBytes(body, buffer, bytesRead, maxBytes, errorMessage)) {
          return false;
        }
        remaining -= bytesRead;
      }

      String chunkTerminator;
      readHttpLine(client, chunkTerminator, 1000);
    }
  }

  if (headers.contentLength >= 0) {
    size_t remaining = static_cast<size_t>(headers.contentLength);
    while (remaining > 0) {
      const size_t bytesToRead = min(remaining, sizeof(buffer));
      const size_t bytesRead = readNetworkBytes(client, buffer, bytesToRead, kHttpReadTimeoutMs);
      if (bytesRead == 0) {
        errorMessage = F("HTTP body was incomplete");
        return false;
      }
      if (!appendBodyBytes(body, buffer, bytesRead, maxBytes, errorMessage)) {
        return false;
      }
      remaining -= bytesRead;
    }
    return true;
  }

  while (client.connected() || client.available() > 0) {
    const size_t bytesRead = readNetworkBytes(client, buffer, sizeof(buffer), 2000);
    if (bytesRead == 0) {
      break;
    }
    if (!appendBodyBytes(body, buffer, bytesRead, maxBytes, errorMessage)) {
      return false;
    }
  }

  return true;
}

/**
 * Streams a complete HTTP response body into the PCM player.
 *
 * Parameters:
 *   client: Connected TLS client.
 *   headers: Parsed HTTP headers.
 *   errorMessage: Output error text.
 * Returns: true when the stream was played.
 * Throws: never.
 */
bool streamHttpBodyToSpeaker(
    WiFiClientSecure &client,
    const HttpResponseHeaders &headers,
    String &errorMessage) {
  uint8_t buffer[512] = {};
  PcmStreamPlayer player;
  state.playbackActive = true;

  if (headers.isChunked) {
    while (true) {
      String chunkSizeLine;
      if (!readHttpLine(client, chunkSizeLine, kHttpReadTimeoutMs)) {
        errorMessage = F("TTS chunk size could not be read");
        state.playbackActive = false;
        return false;
      }

      chunkSizeLine.trim();
      const int extensionIndex = chunkSizeLine.indexOf(';');
      if (extensionIndex >= 0) {
        chunkSizeLine = chunkSizeLine.substring(0, extensionIndex);
      }

      const size_t chunkSize = strtoul(chunkSizeLine.c_str(), nullptr, 16);
      if (chunkSize == 0) {
        String trailerLine;
        while (readHttpLine(client, trailerLine, 1000)) {
          trailerLine.trim();
          if (trailerLine.length() == 0) {
            break;
          }
        }
        player.finish();
        state.playbackActive = false;
        state.lastPlaybackEndedAtMs = millis();
        return true;
      }

      size_t remaining = chunkSize;
      while (remaining > 0) {
        const size_t bytesToRead = min(remaining, sizeof(buffer));
        const size_t bytesRead = readNetworkBytes(client, buffer, bytesToRead, kHttpReadTimeoutMs);
        if (bytesRead == 0) {
          errorMessage = F("TTS chunk data could not be read");
          state.playbackActive = false;
          return false;
        }
        player.write(buffer, bytesRead);
        remaining -= bytesRead;
      }

      String chunkTerminator;
      readHttpLine(client, chunkTerminator, 1000);
    }
  }

  if (headers.contentLength >= 0) {
    size_t remaining = static_cast<size_t>(headers.contentLength);
    while (remaining > 0) {
      const size_t bytesToRead = min(remaining, sizeof(buffer));
      const size_t bytesRead = readNetworkBytes(client, buffer, bytesToRead, kHttpReadTimeoutMs);
      if (bytesRead == 0) {
        errorMessage = F("TTS body was incomplete");
        state.playbackActive = false;
        return false;
      }
      player.write(buffer, bytesRead);
      remaining -= bytesRead;
    }
    player.finish();
    state.playbackActive = false;
    state.lastPlaybackEndedAtMs = millis();
    return true;
  }

  while (client.connected() || client.available() > 0) {
    const size_t bytesRead = readNetworkBytes(client, buffer, sizeof(buffer), 2000);
    if (bytesRead == 0) {
      break;
    }
    player.write(buffer, bytesRead);
  }

  player.finish();
  state.playbackActive = false;
  state.lastPlaybackEndedAtMs = millis();
  return true;
}

/**
 * Reads a complete HTTP response body into an AudioBuffer.
 *
 * Parameters:
 *   client: Connected TLS client.
 *   headers: Parsed HTTP headers.
 *   audioBuffer: Output audio buffer.
 *   errorMessage: Output error text.
 * Returns: true when the body was downloaded completely.
 * Throws: never.
 */
bool readHttpBodyToAudioBuffer(
    WiFiClientSecure &client,
    const HttpResponseHeaders &headers,
    AudioBuffer &audioBuffer,
    String &errorMessage) {
  uint8_t buffer[1024] = {};

  if (headers.contentLength > static_cast<int>(kMaxTtsAudioBytes)) {
    errorMessage = F("TTS audio content-length exceeded the limit");
    return false;
  }

  if (headers.contentLength > 0 &&
      !ensureAudioBufferCapacity(audioBuffer, static_cast<size_t>(headers.contentLength), errorMessage)) {
    return false;
  }

  if (headers.isChunked) {
    while (true) {
      String chunkSizeLine;
      if (!readHttpLine(client, chunkSizeLine, kHttpReadTimeoutMs)) {
        errorMessage = F("TTS chunk size could not be read");
        return false;
      }

      chunkSizeLine.trim();
      const int extensionIndex = chunkSizeLine.indexOf(';');
      if (extensionIndex >= 0) {
        chunkSizeLine = chunkSizeLine.substring(0, extensionIndex);
      }

      const size_t chunkSize = strtoul(chunkSizeLine.c_str(), nullptr, 16);
      if (chunkSize == 0) {
        String trailerLine;
        while (readHttpLine(client, trailerLine, 1000)) {
          trailerLine.trim();
          if (trailerLine.length() == 0) {
            break;
          }
        }
        return true;
      }

      size_t remaining = chunkSize;
      while (remaining > 0) {
        const size_t bytesToRead = min(remaining, sizeof(buffer));
        const size_t bytesRead = readNetworkBytes(client, buffer, bytesToRead, kHttpReadTimeoutMs);
        if (bytesRead == 0) {
          errorMessage = F("TTS chunk data could not be read");
          return false;
        }
        if (!appendAudioBytes(audioBuffer, buffer, bytesRead, errorMessage)) {
          return false;
        }
        remaining -= bytesRead;
      }

      String chunkTerminator;
      readHttpLine(client, chunkTerminator, 1000);
    }
  }

  if (headers.contentLength >= 0) {
    size_t remaining = static_cast<size_t>(headers.contentLength);
    while (remaining > 0) {
      const size_t bytesToRead = min(remaining, sizeof(buffer));
      const size_t bytesRead = readNetworkBytes(client, buffer, bytesToRead, kHttpReadTimeoutMs);
      if (bytesRead == 0) {
        errorMessage = F("TTS body was incomplete");
        return false;
      }
      if (!appendAudioBytes(audioBuffer, buffer, bytesRead, errorMessage)) {
        return false;
      }
      remaining -= bytesRead;
    }
    return true;
  }

  while (client.connected() || client.available() > 0) {
    const size_t bytesRead = readNetworkBytes(client, buffer, sizeof(buffer), 2000);
    if (bytesRead == 0) {
      break;
    }
    if (!appendAudioBytes(audioBuffer, buffer, bytesRead, errorMessage)) {
      return false;
    }
  }

  return audioBuffer.length > 0;
}

/**
 * Plays a fully downloaded PCM16 audio buffer without network stalls.
 *
 * Parameters:
 *   audioBuffer: Raw PCM16 mono audio.
 * Returns: nothing.
 * Throws: never.
 */
void playBufferedPcmAudio(const AudioBuffer &audioBuffer) {
  PcmStreamPlayer player;
  state.playbackActive = true;
  setActivityStatus(F("speaking"), F("Translation audio is playing."));

  for (size_t offset = 0; offset < audioBuffer.length; offset += kPcmPlaybackChunkBytes) {
    const size_t bytesToPlay = min(kPcmPlaybackChunkBytes, audioBuffer.length - offset);
    player.write(audioBuffer.data + offset, bytesToPlay);
    processControlPanel();
    processSerialInput();
  }

  player.finish();
  state.playbackActive = false;
  state.lastPlaybackEndedAtMs = millis();
  setActivityStatus(F("idle"), F("Translation audio finished."));
}

/**
 * Extracts a concise error string from an OpenRouter JSON response.
 *
 * Parameters:
 *   body: JSON or plaintext response body.
 * Returns: Human-readable error text.
 * Throws: never.
 */
String parseOpenRouterErrorBody(const String &body) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, body);
  if (!error) {
    const char *nestedMessage = document["error"]["message"] | nullptr;
    if (nestedMessage != nullptr && strlen(nestedMessage) > 0) {
      return String(nestedMessage);
    }

    const char *message = document["message"] | nullptr;
    if (message != nullptr && strlen(message) > 0) {
      return String(message);
    }
  }

  String compact = body;
  compact.replace("\r", " ");
  compact.replace("\n", " ");
  compact.trim();
  if (compact.length() > 240) {
    compact = compact.substring(0, 240);
  }
  return compact.length() > 0 ? compact : String("OpenRouter error body is empty");
}

/**
 * Sends a small JSON request and reads a JSON response body.
 *
 * Parameters:
 *   path: OpenRouter API path.
 *   requestBody: JSON request body.
 *   responseBody: Output response body.
 *   errorMessage: Output error text.
 * Returns: true when the HTTP response is successful.
 * Throws: never.
 */
bool postJsonForResponseBody(
    const char *path,
    const String &requestBody,
    String &responseBody,
    String &errorMessage) {
  WiFiClientSecure client;
  if (!connectOpenRouter(client, errorMessage)) {
    return false;
  }

  if (!sendOpenRouterPostHeaders(client, path, requestBody.length())) {
    errorMessage = F("OpenRouter HTTP headers could not be sent");
    client.stop();
    return false;
  }

  client.print(requestBody);

  HttpResponseHeaders headers;
  if (!readHttpResponseHeaders(client, headers, errorMessage)) {
    client.stop();
    return false;
  }

  if (!readHttpBodyToString(client, headers, kJsonBodyLimitBytes, responseBody, errorMessage)) {
    client.stop();
    return false;
  }

  client.stop();

  if (headers.statusCode < 200 || headers.statusCode >= 300) {
    errorMessage = String(F("OpenRouter HTTP ")) + headers.statusCode + String(F(": ")) +
                   parseOpenRouterErrorBody(responseBody);
    return false;
  }

  return true;
}

/**
 * Writes a little-endian unsigned 16-bit value.
 *
 * Parameters:
 *   destination: Output pointer.
 *   value: Value to write.
 * Returns: nothing.
 * Throws: never.
 */
void writeLittleEndian16(uint8_t *destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value & 0xFF);
  destination[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

/**
 * Writes a little-endian unsigned 32-bit value.
 *
 * Parameters:
 *   destination: Output pointer.
 *   value: Value to write.
 * Returns: nothing.
 * Throws: never.
 */
void writeLittleEndian32(uint8_t *destination, uint32_t value) {
  destination[0] = static_cast<uint8_t>(value & 0xFF);
  destination[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  destination[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  destination[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

/**
 * Builds a 44-byte WAV header for mono PCM16 audio.
 *
 * Parameters:
 *   header: Output 44-byte WAV header buffer.
 *   pcmByteLength: Number of raw PCM bytes after the header.
 *   sampleRate: PCM sample rate in Hz.
 * Returns: nothing.
 * Throws: never.
 */
void buildWavHeader(uint8_t header[44], uint32_t pcmByteLength, uint32_t sampleRate) {
  memcpy(header, "RIFF", 4);
  writeLittleEndian32(header + 4, 36 + pcmByteLength);
  memcpy(header + 8, "WAVE", 4);
  memcpy(header + 12, "fmt ", 4);
  writeLittleEndian32(header + 16, 16);
  writeLittleEndian16(header + 20, 1);
  writeLittleEndian16(header + 22, 1);
  writeLittleEndian32(header + 24, sampleRate);
  writeLittleEndian32(header + 28, sampleRate * 2);
  writeLittleEndian16(header + 32, 2);
  writeLittleEndian16(header + 34, 16);
  memcpy(header + 36, "data", 4);
  writeLittleEndian32(header + 40, pcmByteLength);
}

/**
 * Sends the recorded audio to OpenRouter STT and parses transcript text.
 *
 * Parameters:
 *   transcript: Output transcript text.
 *   errorMessage: Output error text.
 * Returns: true when transcription succeeded.
 * Throws: never.
 */
bool transcribeRecordedAudio(String &transcript, String &errorMessage) {
  if (recordedPcmLength < kMinimumRecordedPcmBytes) {
    errorMessage = F("Recording is too short or empty");
    return false;
  }

  const String prefix = String(F("{\"model\":")) + quoteJsonString(settings.sttModel) +
                        String(F(",\"input_audio\":{\"data\":\""));
  const String suffix = settings.sourceLanguage == kAutoDetectLanguageCode
                            ? String(F("\",\"format\":\"wav\"},\"temperature\":0}"))
                            : String(F("\",\"format\":\"wav\"},\"language\":")) +
                                  quoteJsonString(settings.sourceLanguage) +
                                  String(F(",\"temperature\":0}"));
  const size_t wavByteLength = 44 + recordedPcmLength;
  const size_t base64Length = ((wavByteLength + 2) / 3) * 4;
  const size_t bodyLength = prefix.length() + base64Length + suffix.length();

  WiFiClientSecure client;
  if (!connectOpenRouter(client, errorMessage)) {
    return false;
  }

  if (!sendOpenRouterPostHeaders(client, kOpenRouterSttPath, bodyLength)) {
    errorMessage = F("STT HTTP headers could not be sent");
    client.stop();
    return false;
  }

  client.print(prefix);

  uint8_t wavHeader[44] = {};
  buildWavHeader(wavHeader, recordedPcmLength, kAudioSampleRateHz);

  Base64BodyWriter base64Writer(client);
  bool writeSucceeded = base64Writer.write(wavHeader, sizeof(wavHeader));
  writeSucceeded = writeSucceeded && base64Writer.write(recordedPcmBuffer, recordedPcmLength);
  writeSucceeded = writeSucceeded && base64Writer.finish();
  if (!writeSucceeded) {
    errorMessage = F("STT base64 body could not be sent");
    client.stop();
    return false;
  }

  client.print(suffix);

  HttpResponseHeaders headers;
  if (!readHttpResponseHeaders(client, headers, errorMessage)) {
    client.stop();
    return false;
  }

  String responseBody;
  if (!readHttpBodyToString(client, headers, kJsonBodyLimitBytes, responseBody, errorMessage)) {
    client.stop();
    return false;
  }

  client.stop();

  if (headers.statusCode < 200 || headers.statusCode >= 300) {
    errorMessage = String(F("OpenRouter STT HTTP ")) + headers.statusCode + String(F(": ")) +
                   parseOpenRouterErrorBody(responseBody);
    return false;
  }

  JsonDocument document;
  const DeserializationError parseError = deserializeJson(document, responseBody);
  if (parseError) {
    errorMessage = String(F("STT JSON parse error: ")) + parseError.c_str();
    return false;
  }

  const char *text = document["text"] | "";
  transcript = String(text);
  transcript.trim();
  if (transcript.length() == 0) {
    errorMessage = F("STT returned empty text");
    return false;
  }

  return true;
}

/**
 * Translates transcript text through OpenRouter chat completions.
 *
 * Parameters:
 *   sourceText: Transcript to translate.
 *   translatedText: Output translated text.
 *   errorMessage: Output error text.
 * Returns: true when translation succeeded.
 * Throws: never.
 */
bool translateText(const String &sourceText, String &translatedText, String &errorMessage) {
  JsonDocument requestDocument;
  requestDocument["model"] = settings.translationModel;
  requestDocument["temperature"] = 0;

  JsonArray messages = requestDocument["messages"].to<JsonArray>();
  JsonObject systemMessage = messages.add<JsonObject>();
  systemMessage["role"] = "system";
  const String targetLanguageLabel = languageLabelForCode(settings.targetLanguage);
  systemMessage["content"] =
      String(F("You are a precise speech translation engine. Translate the user's text into ")) +
      targetLanguageLabel +
      String(F(". Return only the translated text. Preserve meaning, tone, numbers, names, and punctuation. Do not add explanations."));

  JsonObject userMessage = messages.add<JsonObject>();
  userMessage["role"] = "user";
  userMessage["content"] = sourceText;

  String requestBody;
  serializeJson(requestDocument, requestBody);

  String responseBody;
  if (!postJsonForResponseBody(kOpenRouterChatPath, requestBody, responseBody, errorMessage)) {
    return false;
  }

  JsonDocument responseDocument;
  const DeserializationError parseError = deserializeJson(responseDocument, responseBody);
  if (parseError) {
    errorMessage = String(F("Translation JSON parse error: ")) + parseError.c_str();
    return false;
  }

  const char *content = responseDocument["choices"][0]["message"]["content"] | "";
  translatedText = String(content);
  translatedText.trim();
  if (translatedText.length() == 0) {
    errorMessage = F("Translation model returned empty text");
    return false;
  }

  return true;
}

/**
 * Sends translated text to OpenRouter TTS and streams returned PCM to the speaker.
 *
 * Parameters:
 *   translatedText: Text to synthesize.
 *   errorMessage: Output error text.
 * Returns: true when TTS playback completed.
 * Throws: never.
 */
bool speakTranslatedText(const String &translatedText, String &errorMessage) {
  JsonDocument requestDocument;
  requestDocument["model"] = settings.ttsModel;
  requestDocument["input"] = translatedText;
  requestDocument["voice"] = settings.ttsVoice;
  requestDocument["response_format"] = "pcm";
  requestDocument["speed"] = settings.ttsSpeed;

  JsonObject provider = requestDocument["provider"].to<JsonObject>();
  JsonObject options = provider["options"].to<JsonObject>();
  JsonObject mistral = options["mistral"].to<JsonObject>();
  const String targetLanguageLabel = languageLabelForCode(settings.targetLanguage);
  mistral["instructions"] =
      String(F("Speak naturally and clearly in ")) +
      targetLanguageLabel +
      String(F(". Use a calm, intelligible voice suitable for a small IoT speaker."));

  String requestBody;
  serializeJson(requestDocument, requestBody);

  WiFiClientSecure client;
  if (!connectOpenRouter(client, errorMessage)) {
    return false;
  }

  if (!sendOpenRouterPostHeaders(client, kOpenRouterTtsPath, requestBody.length())) {
    errorMessage = F("TTS HTTP headers could not be sent");
    client.stop();
    return false;
  }

  client.print(requestBody);

  HttpResponseHeaders headers;
  if (!readHttpResponseHeaders(client, headers, errorMessage)) {
    client.stop();
    return false;
  }

  if (headers.statusCode < 200 || headers.statusCode >= 300 || headers.contentType.indexOf("application/json") >= 0) {
    String responseBody;
    if (!readHttpBodyToString(client, headers, kJsonBodyLimitBytes, responseBody, errorMessage)) {
      client.stop();
      return false;
    }
    client.stop();

    if (headers.statusCode >= 200 && headers.statusCode < 300) {
      errorMessage = String(F("TTS returned JSON instead of audio: ")) + parseOpenRouterErrorBody(responseBody);
    } else {
      errorMessage = String(F("OpenRouter TTS HTTP ")) + headers.statusCode + String(F(": ")) +
                     parseOpenRouterErrorBody(responseBody);
      if (responseBody.indexOf("response_format=\\\"mp3\\\"") >= 0 ||
          responseBody.indexOf("only supports response_format") >= 0) {
        errorMessage += F(". This firmware expects raw PCM; use a TTS model that supports PCM. "
                          "Suggestion: ttsmodel openai/gpt-4o-mini-tts-2025-12-15");
      }
    }
    return false;
  }

  if (headers.contentType.indexOf("audio/wav") >= 0 || headers.contentType.indexOf("audio/wave") >= 0) {
    errorMessage = F("TTS returned WAV; firmware currently expects raw PCM");
    client.stop();
    return false;
  }

  if (headers.contentType.indexOf("audio/mpeg") >= 0 || headers.contentType.indexOf("audio/mp3") >= 0) {
    errorMessage = F("TTS returned MP3; this ESP32 firmware does not decode MP3 or download the file into RAM. "
                     "Use a TTS model that supports PCM. Suggestion: ttsmodel openai/gpt-4o-mini-tts-2025-12-15");
    client.stop();
    return false;
  }

  if (headers.contentType.length() > 0 &&
      headers.contentType.indexOf("audio/pcm") < 0 &&
      headers.contentType.indexOf("application/octet-stream") < 0) {
    errorMessage = String(F("Unsupported TTS content-type: ")) + headers.contentType +
                   String(F(". Firmware expects raw PCM."));
    client.stop();
    return false;
  }

  if (state.currentSpeakerSampleRate != settings.ttsPcmRateHz || !state.speakerReady) {
    if (!initializeSpeaker(settings.ttsPcmRateHz)) {
      errorMessage = F("Speaker could not be started at the TTS sample rate");
      client.stop();
      return false;
    }
  }

  Console.println(F("TTS audio streaming..."));
  setActivityStatus(F("thinking"), F("TTS audio streaming..."));
  processControlPanel();
  Console.println(F("Translation audio is playing."));
  setActivityStatus(F("speaking"), F("Translation audio is playing."));
  processControlPanel();
  const bool streamed = streamHttpBodyToSpeaker(client, headers, errorMessage);
  client.stop();

  if (!streamed) {
    return false;
  }

  Console.println(F("Translation audio finished."));
  setActivityStatus(F("idle"), F("Translation audio finished."));
  processControlPanel();
  return true;
}

/**
 * Runs the complete direct OpenRouter translation pipeline.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void processTranslationSession() {
  if (settings.openRouterApiKey.length() == 0) {
    Console.println(F("OpenRouter API key is not set. Command: apikey sk-or-..."));
    setActivityStatus(F("idle"), F("OpenRouter API key is not set."));
    return;
  }

  if (!connectWiFi()) {
    Console.println(F("Translation could not start: Wi-Fi is not connected."));
    setActivityStatus(F("idle"), F("Translation could not start: Wi-Fi is not connected."));
    return;
  }

  String errorMessage;
  String transcript;
  String translatedText;

  Console.println(F("Waiting for OpenRouter STT..."));
  setActivityStatus(F("thinking"), F("Waiting for OpenRouter STT..."));
  processControlPanel();
  if (!transcribeRecordedAudio(transcript, errorMessage)) {
    Console.print(F("STT error: "));
    Console.println(errorMessage);
    setActivityStatus(F("idle"), F("STT error."));
    processControlPanel();
    return;
  }

  state.lastTranscript = transcript;
  Console.print(F("Detected: "));
  Console.println(transcript);

  Console.println(F("Waiting for OpenRouter translation..."));
  setActivityStatus(F("thinking"), F("Waiting for OpenRouter translation..."));
  processControlPanel();
  if (!translateText(transcript, translatedText, errorMessage)) {
    Console.print(F("Translation error: "));
    Console.println(errorMessage);
    setActivityStatus(F("idle"), F("Translation error."));
    processControlPanel();
    return;
  }

  state.lastTranslation = translatedText;
  Console.print(F("Translation: "));
  Console.println(translatedText);

  Console.println(F("Waiting for OpenRouter TTS..."));
  setActivityStatus(F("thinking"), F("Waiting for OpenRouter TTS..."));
  processControlPanel();
  if (!speakTranslatedText(translatedText, errorMessage)) {
    Console.print(F("TTS error: "));
    Console.println(errorMessage);
    setActivityStatus(F("idle"), F("TTS error."));
    processControlPanel();
    return;
  }

  Console.println(F("Session complete. Ready for a new recording."));
  setActivityStatus(F("idle"), F("Session complete. Ready for a new recording."));
  processControlPanel();
}

/**
 * Reads one microphone DMA chunk and converts it to signed PCM16 samples.
 *
 * Parameters:
 *   pcmSamples: Destination buffer for converted PCM16 samples.
 *   maxSamples: Maximum number of samples the destination can hold.
 *   samplesRead: Output number of converted samples.
 * Returns: true when at least one sample was converted.
 * Throws: never.
 */
bool readMicrophoneChunkToPcm(int16_t *pcmSamples, size_t maxSamples, size_t &samplesRead) {
  static int32_t microphoneSamples[kAudioChunkSamples];

  samplesRead = 0;
  if (pcmSamples == nullptr || maxSamples == 0) {
    return false;
  }

  size_t bytesRead = 0;
  const esp_err_t result = i2s_read(
      kMicrophoneI2sPort,
      microphoneSamples,
      sizeof(microphoneSamples),
      &bytesRead,
      pdMS_TO_TICKS(30));

  if (result != ESP_OK || bytesRead == 0) {
    return false;
  }

  const size_t sampleCount = bytesRead / sizeof(microphoneSamples[0]);
  samplesRead = min(sampleCount, maxSamples);
  for (size_t index = 0; index < samplesRead; ++index) {
    const int32_t sample24 = microphoneSamples[index] >> 8;
    const int32_t sample16 = (sample24 >> 8) * kMicrophoneDigitalGain;
    pcmSamples[index] = clampToInt16(sample16);
  }

  return samplesRead > 0;
}

/**
 * Checks whether a PCM16 chunk contains voice activity after removing DC offset.
 *
 * Parameters:
 *   pcmSamples: PCM16 sample buffer.
 *   sampleCount: Number of samples to inspect.
 * Returns: true when RMS and peak thresholds indicate voice activity.
 * Throws: never.
 */
bool pcmChunkHasVoiceActivity(const int16_t *pcmSamples, size_t sampleCount) {
  if (pcmSamples == nullptr || sampleCount == 0) {
    return false;
  }

  int64_t sampleSum = 0;
  for (size_t index = 0; index < sampleCount; ++index) {
    sampleSum += pcmSamples[index];
  }

  const int32_t dcOffset = static_cast<int32_t>(sampleSum / static_cast<int64_t>(sampleCount));
  uint64_t squaredAcSampleSum = 0;
  int32_t peakAcAmplitude = 0;

  for (size_t index = 0; index < sampleCount; ++index) {
    const int32_t acSample = static_cast<int32_t>(pcmSamples[index]) - dcOffset;
    const int32_t absoluteAcSample = acSample < 0 ? -acSample : acSample;
    peakAcAmplitude = max(peakAcAmplitude, absoluteAcSample);
    squaredAcSampleSum += static_cast<uint64_t>(absoluteAcSample) * static_cast<uint64_t>(absoluteAcSample);
  }

  const uint64_t meanSquaredAcSample = squaredAcSampleSum / static_cast<uint64_t>(sampleCount);
  const uint64_t rmsThresholdSquared =
      static_cast<uint64_t>(kVoiceActivityRmsThreshold) * static_cast<uint64_t>(kVoiceActivityRmsThreshold);

  return meanSquaredAcSample >= rmsThresholdSquared && peakAcAmplitude >= kVoiceActivityPeakThreshold;
}

/**
 * Samples the idle microphone path once and checks for voice activity.
 *
 * Parameters: none.
 * Returns: true when the current microphone chunk contains voice activity.
 * Throws: never.
 */
bool detectVoiceActivityFromMicrophone() {
  static int16_t pcmSamples[kAudioChunkSamples];

  if (!state.microphoneReady) {
    return false;
  }

  size_t samplesRead = 0;
  if (!readMicrophoneChunkToPcm(pcmSamples, kAudioChunkSamples, samplesRead)) {
    return false;
  }

  return pcmChunkHasVoiceActivity(pcmSamples, samplesRead);
}

/**
 * Converts and stores one microphone chunk in the recording buffer.
 *
 * Parameters: none.
 * Returns: true when the chunk contains voice activity.
 * Throws: never.
 */
bool captureChunkToBuffer() {
  static int16_t pcmSamples[kAudioChunkSamples];

  if (recordedPcmBuffer == nullptr || recordedPcmLength >= recordedPcmCapacity) {
    return false;
  }

  const size_t writableSamples = min(
      static_cast<size_t>(kAudioChunkSamples),
      (recordedPcmCapacity - recordedPcmLength) / sizeof(int16_t));

  size_t samplesRead = 0;
  if (!readMicrophoneChunkToPcm(pcmSamples, writableSamples, samplesRead)) {
    return false;
  }

  const size_t bytesToStore = samplesRead * sizeof(int16_t);
  memcpy(recordedPcmBuffer + recordedPcmLength, pcmSamples, bytesToStore);
  recordedPcmLength += bytesToStore;

  return pcmChunkHasVoiceActivity(pcmSamples, samplesRead);
}

/**
 * Starts one fixed-length capture session.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void startCaptureSession() {
  if (!state.microphoneReady || state.sessionActive || state.processingActive || state.playbackActive) {
    Console.println(F("Recording could not start: microphone or previous session is not ready."));
    return;
  }

  if (recordedPcmBuffer == nullptr || recordedPcmCapacity == 0) {
    Console.println(F("Recording could not start: audio buffer is missing."));
    return;
  }

  i2s_zero_dma_buffer(kMicrophoneI2sPort);
  recordedPcmLength = 0;
  state.sessionActive = true;
  state.processingActive = false;
  state.playbackActive = false;
  state.voiceDetectedInSession = false;
  const uint32_t now = millis();
  state.sessionStartedAtMs = now;
  state.lastVoiceDetectedAtMs = now;
  state.lastTranscript = "";
  state.lastTranslation = "";
  Console.println(F("Recording started. It will stop automatically after 3 seconds of silence."));
  setActivityStatus(F("listening"), F("Recording started. It will stop automatically after 3 seconds of silence."));
}

/**
 * Finishes the current capture session and starts direct OpenRouter processing.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void finishCaptureSession() {
  state.sessionActive = false;

  Console.print(F("Recording finished. Bytes: "));
  Console.println(recordedPcmLength);
  setActivityStatus(F("thinking"), F("Recording finished. OpenRouter processing is starting."));
  processControlPanel();

  if (!state.voiceDetectedInSession) {
    Console.println(F("Recording cancelled: no speech detected."));
    setActivityStatus(F("idle"), F("Recording cancelled: no speech detected."));
    processControlPanel();
    return;
  }

  state.processingActive = true;
  processTranslationSession();
  state.processingActive = false;
}

/**
 * Plays one PCM16 mono chunk through MAX98357A.
 *
 * Parameters:
 *   payload: Raw little-endian signed 16-bit mono PCM.
 *   length: Number of bytes in payload.
 * Returns: nothing.
 * Throws: never.
 */
void playPcmChunk(const uint8_t *payload, size_t length) {
  static int32_t speakerFrames[kAudioChunkSamples * 2];

  if (!state.speakerReady || length < 2) {
    return;
  }

  size_t offset = 0;
  while (offset + sizeof(int16_t) <= length) {
    const size_t availableSamples = (length - offset) / sizeof(int16_t);
    const size_t inputSamples = min(availableSamples, static_cast<size_t>(kAudioChunkSamples));
    const int16_t *samples = reinterpret_cast<const int16_t *>(payload + offset);

    for (size_t index = 0; index < inputSamples; ++index) {
      const int32_t amplifiedSample = clampToInt16(
          static_cast<int32_t>(static_cast<float>(samples[index]) * settings.playbackVolume));
      const int32_t sample32 = amplifiedSample << 16;
      speakerFrames[index * 2] = sample32;
      speakerFrames[(index * 2) + 1] = sample32;
    }

    size_t bytesWritten = 0;
    i2s_write(
        kSpeakerI2sPort,
        speakerFrames,
        inputSamples * 2 * sizeof(speakerFrames[0]),
        &bytesWritten,
        portMAX_DELAY);

    offset += inputSamples * sizeof(int16_t);
  }
}

/**
 * Checks whether VL53L0X presence detection should start a capture session.
 *
 * Parameters: none.
 * Returns: true when a new session should start.
 * Throws: never.
 */
bool shouldStartFromPresence() {
  if (!settings.autoPresence || !state.distanceSensorReady) {
    return false;
  }

  const uint32_t now = millis();
  if (now - state.lastSensorPollAtMs < kSensorPollIntervalMs) {
    return false;
  }
  state.lastSensorPollAtMs = now;

  VL53L0X_RangingMeasurementData_t measurement = {};
  distanceSensor.rangingTest(&measurement, false);
  if (measurement.RangeStatus == 4) {
    if (state.targetPresent) {
      state.targetPresent = false;
      Console.println(F("Presence cleared: out of range."));
    }
    return false;
  }

  state.lastDistanceMm = measurement.RangeMilliMeter;
  if (state.targetPresent && state.lastDistanceMm > kPresenceClearDistanceMm) {
    state.targetPresent = false;
    Console.print(F("Presence cleared: "));
    Console.print(state.lastDistanceMm);
    Console.println(F(" mm"));
    return false;
  }

  if (state.lastDistanceMm > kPresenceTriggerDistanceMm) {
    return false;
  }

  if (now - state.lastAutoTriggerAtMs < kSensorRearmDelayMs) {
    return false;
  }

  if (!state.targetPresent) {
    state.targetPresent = true;
    state.lastAutoTriggerAtMs = now;
    Console.print(F("Presence detected: "));
    Console.print(state.lastDistanceMm);
    Console.println(F(" mm"));
    return true;
  }

  if (state.lastPlaybackEndedAtMs == 0 ||
      now - state.lastPlaybackEndedAtMs < kPostPlaybackListenDelayMs ||
      !detectVoiceActivityFromMicrophone()) {
    return false;
  }

  state.lastAutoTriggerAtMs = now;
  Console.print(F("Presence and speech detected: "));
  Console.print(state.lastDistanceMm);
  Console.println(F(" mm"));
  return true;
}

/**
 * Prints current device settings and runtime status.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void printStatus() {
  Console.println();
  Console.println(F("Status"));
  Console.print(F("  Wi-Fi SSID: "));
  Console.println(settings.wifiSsid.length() > 0 ? settings.wifiSsid : String("(not set)"));
  Console.print(F("  Wi-Fi: "));
  Console.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("not connected"));
  Console.print(F("  DNS: "));
  if (settings.useDhcpDns) {
    Console.print(F("DHCP "));
    Console.print(WiFi.dnsIP(0));
    Console.print(F(", "));
    Console.println(WiFi.dnsIP(1));
  } else {
    Console.print(settings.primaryDns);
    Console.print(F(", "));
    Console.println(settings.secondaryDns);
  }
  Console.print(F("  OpenRouter IP override: "));
  Console.println(settings.openRouterIpOverride.length() > 0 ? settings.openRouterIpOverride : String("(none)"));
  Console.print(F("  OpenRouter API key: "));
  Console.println(maskSecret(settings.openRouterApiKey));
  Console.print(F("  Language: "));
  Console.print(settings.sourceLanguage);
  Console.print(F(" -> "));
  Console.println(settings.targetLanguage);
  Console.print(F("  STT model: "));
  Console.println(settings.sttModel);
  Console.print(F("  Translation model: "));
  Console.println(settings.translationModel);
  Console.print(F("  TTS model: "));
  Console.println(settings.ttsModel);
  Console.print(F("  TTS voice: "));
  Console.println(settings.ttsVoice);
  Console.print(F("  TTS PCM rate: "));
  Console.println(settings.ttsPcmRateHz);
  Console.print(F("  TTS speed: "));
  Console.println(settings.ttsSpeed, 2);
  Console.print(F("  Playback volume: "));
  Console.println(settings.playbackVolume, 2);
  Console.print(F("  Recording buffer bytes: "));
  Console.print(recordedPcmCapacity);
  Console.print(F(" / last recording: "));
  Console.println(recordedPcmLength);
  Console.print(F("  Microphone: "));
  Console.println(state.microphoneReady ? F("ready") : F("not ready"));
  Console.print(F("  Speaker: "));
  Console.println(state.speakerReady ? F("ready") : F("not ready"));
  Console.print(F("  VL53L0X: "));
  Console.println(state.distanceSensorReady ? F("ready") : F("not ready"));
  Console.print(F("  Automatic presence: "));
  Console.println(settings.autoPresence ? F("on") : F("off"));
  Console.print(F("  Presence trigger distance: "));
  Console.print(kPresenceTriggerDistanceMm);
  Console.println(F(" mm"));
  Console.print(F("  Stop after silence: "));
  Console.print(kSilenceStopDurationMs);
  Console.println(F(" ms"));
  if (state.lastTranscript.length() > 0) {
    Console.print(F("  Last detected: "));
    Console.println(state.lastTranscript);
  }
  if (state.lastTranslation.length() > 0) {
    Console.print(F("  Last translation: "));
    Console.println(state.lastTranslation);
  }
  Console.println();
}

/**
 * Prints serial command help.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void printHelp() {
  Console.println();
  Console.println(F("Commands"));
  Console.println(F("  ssid <WiFi name>"));
  Console.println(F("  pass <WiFi password>"));
  Console.println(F("  wifi <ssid> <password>"));
  Console.println(F("  dns <primary> <secondary>   example: dns 1.1.1.1 8.8.8.8"));
  Console.println(F("  dns auto                    use DHCP DNS"));
  Console.println(F("  orip <OpenRouter IPv4>      fallback when DNS does not work"));
  Console.println(F("  orip clear                  disable IP fallback"));
  Console.println(F("  apikey <OpenRouter sk-or-...>"));
  Console.println(F("  lang <source> <target>      example: lang en tr or lang auto tr"));
  Console.println(F("  sttmodel <OpenRouter model>"));
  Console.println(F("  transmodel <OpenRouter model>"));
  Console.println(F("  ttsmodel <OpenRouter model>"));
  Console.println(F("  voice <TTS voice>"));
  Console.println(F("  ttsrate <8000-48000>        default: 24000"));
  Console.println(F("  ttsspeed <0.25-4.0>         default: 1.0"));
  Console.println(F("  volume <0.1-12.0>           default: 4.0"));
  Console.println(F("  presence on|off"));
  Console.println(F("  r                           start recording; stops after 3 seconds of silence"));
  Console.println(F("  show                        print settings and status"));
  Console.println(F("  clear                       delete saved settings"));
  Console.println(F("  restart                     restart the device"));
  Console.println();
}

/**
 * Stores a language pair after validation.
 *
 * Parameters:
 *   sourceLanguage: Source language code.
 *   targetLanguage: Target language code.
 * Returns: true when both language codes were stored.
 * Throws: never.
 */
bool applyLanguageSettings(String sourceLanguage, String targetLanguage) {
  sourceLanguage = normalizeLanguageCode(sourceLanguage);
  targetLanguage = normalizeLanguageCode(targetLanguage);

  if (!isSupportedPipelineLanguageCode(sourceLanguage, true) ||
      !isSupportedPipelineLanguageCode(targetLanguage, false)) {
    Console.println(F("Language code is not supported. Example: lang en tr or lang auto tr"));
    return false;
  }

  settings.sourceLanguage = sourceLanguage;
  settings.targetLanguage = targetLanguage;
  saveStringSetting("source", settings.sourceLanguage);
  saveStringSetting("target", settings.targetLanguage);

  Console.print(F("Language saved: "));
  Console.print(settings.sourceLanguage);
  Console.print(F(" -> "));
  Console.println(settings.targetLanguage);
  return true;
}

/**
 * Serves the browser control panel HTML.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void handleControlPanelRoot() {
  sendCommonWebHeaders();
  controlServer.send_P(200, PSTR("text/html; charset=utf-8"), kControlPanelHtml);
}

/**
 * Sends the supported language lists used by source and target selectors.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void handleLanguagesApi() {
  JsonDocument document;
  document["ok"] = true;
  document["source"] = F("OpenAI Whisper/TTS supported language intersection");

  JsonArray sourceLanguages = document["sourceLanguages"].to<JsonArray>();
  JsonObject autoLanguage = sourceLanguages.add<JsonObject>();
  autoLanguage["code"] = kAutoDetectLanguageCode;
  autoLanguage["name"] = F("Auto detect");

  JsonArray targetLanguages = document["targetLanguages"].to<JsonArray>();
  for (size_t index = 0; index < kSupportedLanguageCount; ++index) {
    JsonObject sourceLanguage = sourceLanguages.add<JsonObject>();
    sourceLanguage["code"] = kSupportedLanguages[index].code;
    sourceLanguage["name"] = kSupportedLanguages[index].name;

    JsonObject targetLanguage = targetLanguages.add<JsonObject>();
    targetLanguage["code"] = kSupportedLanguages[index].code;
    targetLanguage["name"] = kSupportedLanguages[index].name;
  }

  sendJsonDocument(200, document);
}

/**
 * Sends the current settings and runtime state as JSON.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void handleStatusApi() {
  JsonDocument document;
  document["ok"] = true;

  JsonObject wifi = document["wifi"].to<JsonObject>();
  wifi["ssid"] = settings.wifiSsid;
  wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
  wifi["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  wifi["dns1"] = WiFi.status() == WL_CONNECTED ? WiFi.dnsIP(0).toString() : String("");
  wifi["dns2"] = WiFi.status() == WL_CONNECTED ? WiFi.dnsIP(1).toString() : String("");

  JsonObject settingsJson = document["settings"].to<JsonObject>();
  settingsJson["wifiSsid"] = settings.wifiSsid;
  settingsJson["hasWifiPassword"] = settings.wifiPassword.length() > 0;
  settingsJson["openRouterApiKey"] = maskSecret(settings.openRouterApiKey);
  settingsJson["hasOpenRouterApiKey"] = settings.openRouterApiKey.length() > 0;
  settingsJson["sourceLanguage"] = settings.sourceLanguage;
  settingsJson["targetLanguage"] = settings.targetLanguage;
  settingsJson["sttModel"] = settings.sttModel;
  settingsJson["translationModel"] = settings.translationModel;
  settingsJson["ttsModel"] = settings.ttsModel;
  settingsJson["ttsVoice"] = settings.ttsVoice;
  settingsJson["primaryDns"] = settings.primaryDns;
  settingsJson["secondaryDns"] = settings.secondaryDns;
  settingsJson["openRouterIpOverride"] = settings.openRouterIpOverride;
  settingsJson["useDhcpDns"] = settings.useDhcpDns;
  settingsJson["autoPresence"] = settings.autoPresence;

  JsonObject runtimeJson = document["runtime"].to<JsonObject>();
  runtimeJson["microphoneReady"] = state.microphoneReady;
  runtimeJson["speakerReady"] = state.speakerReady;
  runtimeJson["distanceSensorReady"] = state.distanceSensorReady;
  runtimeJson["sessionActive"] = state.sessionActive;
  runtimeJson["processingActive"] = state.processingActive;
  runtimeJson["playbackActive"] = state.playbackActive;
  runtimeJson["targetPresent"] = state.targetPresent;
  runtimeJson["lastDistanceMm"] = state.lastDistanceMm;
  runtimeJson["activityMode"] = state.activityMode;
  runtimeJson["activityMessage"] = state.activityMessage;
  runtimeJson["lastTranscript"] = state.lastTranscript;
  runtimeJson["lastTranslation"] = state.lastTranslation;
  runtimeJson["uptimeMs"] = millis();

  JsonObject audioJson = document["audio"].to<JsonObject>();
  audioJson["recordedPcmCapacity"] = recordedPcmCapacity;
  audioJson["recordedPcmLength"] = recordedPcmLength;
  audioJson["ttsPcmRateHz"] = settings.ttsPcmRateHz;
  audioJson["ttsSpeed"] = settings.ttsSpeed;
  audioJson["playbackVolume"] = settings.playbackVolume;

  JsonObject memoryJson = document["memory"].to<JsonObject>();
  memoryJson["freeHeap"] = ESP.getFreeHeap();
  memoryJson["freePsram"] = ESP.getFreePsram();

  sendJsonDocument(200, document);
}

/**
 * Validates and stores settings received from the browser panel.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void handleSettingsApi() {
  JsonDocument document;
  String errorMessage;
  if (!readJsonRequest(document, errorMessage)) {
    sendJsonMessage(400, false, errorMessage);
    return;
  }

  DeviceSettings updatedSettings = settings;
  if (!readOptionalJsonString(document, "wifiSsid", updatedSettings.wifiSsid, 64, true, errorMessage) ||
      !readOptionalJsonString(document, "wifiPassword", updatedSettings.wifiPassword, 64, true, errorMessage) ||
      !readOptionalJsonString(document, "openRouterApiKey", updatedSettings.openRouterApiKey, 256, true, errorMessage) ||
      !readOptionalJsonString(document, "sourceLanguage", updatedSettings.sourceLanguage, 17, false, errorMessage) ||
      !readOptionalJsonString(document, "targetLanguage", updatedSettings.targetLanguage, 17, false, errorMessage) ||
      !readOptionalJsonString(document, "sttModel", updatedSettings.sttModel, 160, false, errorMessage) ||
      !readOptionalJsonString(document, "translationModel", updatedSettings.translationModel, 160, false, errorMessage) ||
      !readOptionalJsonString(document, "ttsModel", updatedSettings.ttsModel, 160, false, errorMessage) ||
      !readOptionalJsonString(document, "ttsVoice", updatedSettings.ttsVoice, 64, false, errorMessage) ||
      !readOptionalJsonString(document, "primaryDns", updatedSettings.primaryDns, 15, false, errorMessage) ||
      !readOptionalJsonString(document, "secondaryDns", updatedSettings.secondaryDns, 15, false, errorMessage) ||
      !readOptionalJsonString(document, "openRouterIpOverride", updatedSettings.openRouterIpOverride, 15, true, errorMessage) ||
      !readOptionalJsonBool(document, "useDhcpDns", updatedSettings.useDhcpDns, errorMessage) ||
      !readOptionalJsonBool(document, "autoPresence", updatedSettings.autoPresence, errorMessage) ||
      !readOptionalJsonUInt(document, "ttsPcmRateHz", updatedSettings.ttsPcmRateHz, 8000, 48000, errorMessage) ||
      !readOptionalJsonFloat(document, "ttsSpeed", updatedSettings.ttsSpeed, 0.25F, 4.0F, errorMessage) ||
      !readOptionalJsonFloat(document, "playbackVolume", updatedSettings.playbackVolume, 0.1F, 12.0F, errorMessage)) {
    sendJsonMessage(400, false, errorMessage);
    return;
  }

  updatedSettings.sourceLanguage = normalizeLanguageCode(updatedSettings.sourceLanguage);
  updatedSettings.targetLanguage = normalizeLanguageCode(updatedSettings.targetLanguage);
  if (!isSupportedPipelineLanguageCode(updatedSettings.sourceLanguage, true) ||
      !isSupportedPipelineLanguageCode(updatedSettings.targetLanguage, false)) {
    sendJsonMessage(400, false, F("Language code must be in the supported STT/TTS language list."));
    return;
  }

  if (!updatedSettings.useDhcpDns &&
      (!isValidIpv4Address(updatedSettings.primaryDns, false) ||
       !isValidIpv4Address(updatedSettings.secondaryDns, false))) {
    sendJsonMessage(400, false, F("DNS addresses must be IPv4 addresses."));
    return;
  }

  if (!isValidIpv4Address(updatedSettings.openRouterIpOverride, true)) {
    sendJsonMessage(400, false, F("OpenRouter IP fallback must be an IPv4 address or empty."));
    return;
  }

  settings = updatedSettings;
  saveStringSetting("ssid", settings.wifiSsid);
  saveStringSetting("password", settings.wifiPassword);
  saveStringSetting("orkey", settings.openRouterApiKey);
  saveStringSetting("source", settings.sourceLanguage);
  saveStringSetting("target", settings.targetLanguage);
  saveStringSetting("sttmodel", settings.sttModel);
  saveStringSetting("trmodel", settings.translationModel);
  saveStringSetting("ttsmodel", settings.ttsModel);
  saveStringSetting("voice", settings.ttsVoice);
  saveStringSetting("dns1", settings.primaryDns);
  saveStringSetting("dns2", settings.secondaryDns);
  if (settings.openRouterIpOverride.length() == 0) {
    preferences.remove("orip");
  } else {
    saveStringSetting("orip", settings.openRouterIpOverride);
  }
  preferences.putUInt("ttsrate", settings.ttsPcmRateHz);
  preferences.putFloat("ttsspeed", settings.ttsSpeed);
  preferences.putFloat("volume", settings.playbackVolume);
  preferences.putBool("dnsauto", settings.useDhcpDns);
  preferences.putBool("presence", settings.autoPresence);

  if (WiFi.status() == WL_CONNECTED) {
    applyConfiguredDns();
  }

  sendJsonMessage(200, true, F("Settings saved."));
}

/**
 * Runs one serial-compatible command received through the browser panel.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void handleCommandApi() {
  JsonDocument document;
  String errorMessage;
  if (!readJsonRequest(document, errorMessage)) {
    sendJsonMessage(400, false, errorMessage);
    return;
  }

  String command;
  if (!readOptionalJsonString(document, "command", command, kSerialBufferLength - 1, false, errorMessage)) {
    sendJsonMessage(400, false, errorMessage);
    return;
  }
  command.trim();

  if (command == "restart") {
    sendJsonMessage(200, true, F("Restart scheduled."));
    scheduleDeviceRestart();
    return;
  }

  if (command == "clear") {
    preferences.clear();
    resetSettingsToDefaults();
    sendJsonMessage(200, true, F("Settings cleared; restart scheduled."));
    scheduleDeviceRestart();
    return;
  }

  handleSerialCommand(command);
  sendJsonMessage(200, true, String(F("Command executed: ")) + command);
}

/**
 * Handles explicit restart requests from the browser panel.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void handleRestartApi() {
  sendJsonMessage(200, true, F("Restart scheduled."));
  scheduleDeviceRestart();
}

/**
 * Clears saved settings and schedules a restart from the browser panel.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void handleClearApi() {
  preferences.clear();
  resetSettingsToDefaults();
  sendJsonMessage(200, true, F("Settings cleared; restart scheduled."));
  scheduleDeviceRestart();
}

/**
 * Sends a JSON 404 response for unknown routes.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void handleControlPanelNotFound() {
  sendJsonMessage(404, false, F("Endpoint not found."));
}

/**
 * Starts the HTTP control panel server.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void beginControlPanel() {
  controlServer.on(F("/"), HTTP_GET, handleControlPanelRoot);
  controlServer.on(F("/api/languages"), HTTP_GET, handleLanguagesApi);
  controlServer.on(F("/api/status"), HTTP_GET, handleStatusApi);
  controlServer.on(F("/api/settings"), HTTP_POST, handleSettingsApi);
  controlServer.on(F("/api/command"), HTTP_POST, handleCommandApi);
  controlServer.on(F("/api/restart"), HTTP_POST, handleRestartApi);
  controlServer.on(F("/api/clear"), HTTP_POST, handleClearApi);
  controlServer.onNotFound(handleControlPanelNotFound);
  controlServer.begin(kControlPanelPort);

  Console.print(F("Web control panel ready: http://"));
  Console.print(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("device-ip"));
  Console.print(F(":"));
  Console.println(kControlPanelPort);
}

/**
 * Processes pending browser panel client requests.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void processControlPanel() {
  controlServer.handleClient();
}

/**
 * Handles one complete serial command line.
 *
 * Parameters:
 *   commandLine: Command line without newline.
 * Returns: nothing.
 * Throws: never.
 */
void handleSerialCommand(String commandLine) {
  commandLine.trim();
  if (commandLine.length() == 0) {
    return;
  }

  if (commandLine == "?" || commandLine == "help") {
    printHelp();
    return;
  }

  if (commandLine == "r" || commandLine == "R") {
    startCaptureSession();
    return;
  }

  if (commandLine == "show") {
    printStatus();
    return;
  }

  if (commandLine == "restart") {
    Console.println(F("Restarting..."));
    delay(200);
    ESP.restart();
  }

  if (commandLine == "clear") {
    preferences.clear();
    resetSettingsToDefaults();
    Console.println(F("Settings cleared. Restarting..."));
    delay(200);
    ESP.restart();
  }

  if (commandLine.startsWith("ssid ")) {
    settings.wifiSsid = commandLine.substring(5);
    settings.wifiSsid.trim();
    saveStringSetting("ssid", settings.wifiSsid);
    Console.print(F("SSID saved: "));
    Console.println(settings.wifiSsid);
    return;
  }

  if (commandLine.startsWith("pass ")) {
    settings.wifiPassword = commandLine.substring(5);
    settings.wifiPassword.trim();
    saveStringSetting("password", settings.wifiPassword);
    Console.println(F("Wi-Fi password saved."));
    return;
  }

  if (commandLine.startsWith("wifi ")) {
    const String rest = commandLine.substring(5);
    const int separator = rest.indexOf(' ');
    if (separator <= 0) {
      Console.println(F("Usage: wifi <ssid> <password>"));
      return;
    }
    settings.wifiSsid = rest.substring(0, separator);
    settings.wifiPassword = rest.substring(separator + 1);
    settings.wifiPassword.trim();
    saveStringSetting("ssid", settings.wifiSsid);
    saveStringSetting("password", settings.wifiPassword);
    Console.println(F("Wi-Fi settings saved."));
    return;
  }

  if (commandLine == "dns auto") {
    settings.useDhcpDns = true;
    preferences.putBool("dnsauto", true);
    Console.println(F("DHCP DNS will be used. It takes effect after Wi-Fi reconnects."));
    return;
  }

  if (commandLine.startsWith("dns ")) {
    const String rest = commandLine.substring(4);
    const int separator = rest.indexOf(' ');
    if (separator <= 0) {
      Console.println(F("Usage: dns <primary> <secondary>"));
      return;
    }

    String primaryDns = rest.substring(0, separator);
    String secondaryDns = rest.substring(separator + 1);
    primaryDns.trim();
    secondaryDns.trim();

    IPAddress parsedPrimaryDns;
    IPAddress parsedSecondaryDns;
    if (!parsedPrimaryDns.fromString(primaryDns) || !parsedSecondaryDns.fromString(secondaryDns)) {
      Console.println(F("DNS addresses must be IPv4 addresses. Example: dns 1.1.1.1 8.8.8.8"));
      return;
    }

    settings.primaryDns = primaryDns;
    settings.secondaryDns = secondaryDns;
    settings.useDhcpDns = false;
    saveStringSetting("dns1", settings.primaryDns);
    saveStringSetting("dns2", settings.secondaryDns);
    preferences.putBool("dnsauto", false);
    applyConfiguredDns();
    Console.print(F("DNS saved: "));
    Console.print(settings.primaryDns);
    Console.print(F(", "));
    Console.println(settings.secondaryDns);
    return;
  }

  if (commandLine == "orip clear") {
    settings.openRouterIpOverride = "";
    preferences.remove("orip");
    Console.println(F("OpenRouter IP fallback disabled; DNS will be used again."));
    return;
  }

  if (commandLine.startsWith("orip ")) {
    String openRouterIp = commandLine.substring(5);
    openRouterIp.trim();

    IPAddress parsedOpenRouterIp;
    if (!parsedOpenRouterIp.fromString(openRouterIp)) {
      Console.println(F("OpenRouter IP must be IPv4. Example: orip 104.18.2.115"));
      return;
    }

    settings.openRouterIpOverride = openRouterIp;
    saveStringSetting("orip", settings.openRouterIpOverride);
    Console.print(F("OpenRouter IP fallback saved: "));
    Console.println(settings.openRouterIpOverride);
    return;
  }

  if (commandLine.startsWith("apikey ")) {
    settings.openRouterApiKey = commandLine.substring(7);
    settings.openRouterApiKey.trim();
    if (!settings.openRouterApiKey.startsWith("sk-or-")) {
      Console.println(F("Warning: OpenRouter keys usually start with sk-or-. Saving anyway."));
    }
    saveStringSetting("orkey", settings.openRouterApiKey);
    Console.print(F("OpenRouter API key saved: "));
    Console.println(maskSecret(settings.openRouterApiKey));
    return;
  }

  if (commandLine.startsWith("lang ")) {
    const String rest = commandLine.substring(5);
    const int separator = rest.indexOf(' ');
    if (separator <= 0) {
      Console.println(F("Usage: lang <source> <target>; example: lang en tr or lang auto tr"));
      return;
    }
    applyLanguageSettings(rest.substring(0, separator), rest.substring(separator + 1));
    return;
  }

  if (commandLine.startsWith("sttmodel ")) {
    settings.sttModel = commandLine.substring(9);
    settings.sttModel.trim();
    saveStringSetting("sttmodel", settings.sttModel);
    Console.print(F("STT model saved: "));
    Console.println(settings.sttModel);
    return;
  }

  if (commandLine.startsWith("transmodel ")) {
    settings.translationModel = commandLine.substring(11);
    settings.translationModel.trim();
    saveStringSetting("trmodel", settings.translationModel);
    Console.print(F("Translation model saved: "));
    Console.println(settings.translationModel);
    return;
  }

  if (commandLine.startsWith("ttsmodel ")) {
    settings.ttsModel = commandLine.substring(9);
    settings.ttsModel.trim();
    saveStringSetting("ttsmodel", settings.ttsModel);
    Console.print(F("TTS model saved: "));
    Console.println(settings.ttsModel);
    return;
  }

  if (commandLine.startsWith("voice ")) {
    settings.ttsVoice = commandLine.substring(6);
    settings.ttsVoice.trim();
    saveStringSetting("voice", settings.ttsVoice);
    Console.print(F("TTS voice saved: "));
    Console.println(settings.ttsVoice);
    return;
  }

  if (commandLine.startsWith("ttsrate ")) {
    const uint32_t sampleRate = commandLine.substring(8).toInt();
    if (sampleRate < 8000 || sampleRate > 48000) {
      Console.println(F("TTS sample rate must be between 8000 and 48000."));
      return;
    }
    settings.ttsPcmRateHz = sampleRate;
    preferences.putUInt("ttsrate", settings.ttsPcmRateHz);
    Console.print(F("TTS PCM sample rate saved: "));
    Console.println(settings.ttsPcmRateHz);
    return;
  }

  if (commandLine.startsWith("ttsspeed ")) {
    const float ttsSpeed = commandLine.substring(9).toFloat();
    if (ttsSpeed < 0.25F || ttsSpeed > 4.0F) {
      Console.println(F("TTS speed must be between 0.25 and 4.0."));
      return;
    }
    settings.ttsSpeed = ttsSpeed;
    preferences.putFloat("ttsspeed", settings.ttsSpeed);
    Console.print(F("TTS speed saved: "));
    Console.println(settings.ttsSpeed, 2);
    return;
  }

  if (commandLine.startsWith("volume ")) {
    const float playbackVolume = commandLine.substring(7).toFloat();
    if (playbackVolume < 0.1F || playbackVolume > 12.0F) {
      Console.println(F("Playback volume must be between 0.1 and 12.0."));
      return;
    }
    settings.playbackVolume = playbackVolume;
    preferences.putFloat("volume", settings.playbackVolume);
    Console.print(F("Playback volume saved: "));
    Console.println(settings.playbackVolume, 2);
    return;
  }

  if (commandLine == "presence on") {
    settings.autoPresence = true;
    preferences.putBool("presence", true);
    Console.println(F("Automatic presence trigger enabled."));
    return;
  }

  if (commandLine == "presence off") {
    settings.autoPresence = false;
    preferences.putBool("presence", false);
    Console.println(F("Automatic presence trigger disabled."));
    return;
  }

  Console.print(F("Unknown command: "));
  Console.println(commandLine);
  printHelp();
}

/**
 * Polls serial input and dispatches complete command lines.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void processSerialInput() {
  while (Serial.available() > 0) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\r') {
      continue;
    }
    if (character == '\n') {
      serialBuffer[serialBufferIndex] = '\0';
      handleSerialCommand(String(serialBuffer));
      serialBufferIndex = 0;
      serialBuffer[0] = '\0';
      continue;
    }

    if (serialBufferIndex + 1 < kSerialBufferLength) {
      serialBuffer[serialBufferIndex] = character;
      ++serialBufferIndex;
    }
  }
}

/**
 * Prints startup wiring information.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void printStartupBanner() {
  Console.println();
  Console.println(F("ESP32-S3 IoT Translation Device - Direct OpenRouter"));
  Console.println(F("VL53L0X: SDA=GPIO8, SCL=GPIO9"));
  Console.println(F("INMP441: SCK=GPIO12, WS=GPIO13, SD=GPIO14, L/R=GND"));
  Console.println(F("MAX98357A: BCLK=GPIO15, LRC=GPIO16, DIN=GPIO17, SD=3V3"));
  Console.println(F("GAIN -> GND gives higher volume; leave it floating if distortion occurs."));
  Console.println(F("Note: The API key is stored in device NVS; use a low-limit key for demos."));
  printHelp();
}

}  // namespace

/**
 * Arduino startup entry point.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void setup() {
  Console.begin(kSerialBaudRate);
  Console.waitUntilReady(kSerialReadyTimeoutMs);
  printStartupBanner();

  loadSettings();
  allocateRecordingBuffer();
  state.distanceSensorReady = initializeDistanceSensor();
  state.microphoneReady = initializeMicrophone();
  state.speakerReady = initializeSpeaker(kAudioSampleRateHz);

  connectWiFi();
  beginControlPanel();
  printStatus();
}

/**
 * Arduino main loop.
 *
 * Parameters: none.
 * Returns: nothing.
 * Throws: never.
 */
void loop() {
  processControlPanel();
  processSerialInput();
  processScheduledRestart();

  if (!state.sessionActive && !state.processingActive && !state.playbackActive) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    if (shouldStartFromPresence()) {
      startCaptureSession();
    }
  }

  if (state.sessionActive) {
    const bool hasVoiceActivity = captureChunkToBuffer();
    const uint32_t now = millis();

    if (hasVoiceActivity) {
      state.voiceDetectedInSession = true;
      state.lastVoiceDetectedAtMs = now;
    }

    const bool hasReachedSilenceTimeout = now - state.lastVoiceDetectedAtMs >= kSilenceStopDurationMs;
    const bool hasReachedMaxDuration = now - state.sessionStartedAtMs >= kMaxRecordWindowMs;
    const bool hasFilledRecordingBuffer = recordedPcmLength >= recordedPcmCapacity;

    if (hasReachedSilenceTimeout || hasReachedMaxDuration || hasFilledRecordingBuffer) {
      if (hasReachedSilenceTimeout) {
        Console.println(F("3 seconds of silence detected."));
      } else if (hasReachedMaxDuration) {
        Console.println(F("Maximum recording duration reached."));
      } else {
        Console.println(F("Recording buffer is full."));
      }
      finishCaptureSession();
    }
  }

  delay(1);
}
