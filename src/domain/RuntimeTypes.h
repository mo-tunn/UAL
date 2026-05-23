#pragma once

#include <Arduino.h>
#include "config/FirmwareConfig.h"

struct DeviceSettings {
  String wifiSsid;
  String wifiPassword;
  String openRouterApiKey;
  String sourceLanguage;
  String targetLanguage;
  String sttModel;
  String translationModel;
  String ttsModel;
  String ttsVoice;
  String primaryDns;
  String secondaryDns;
  String openRouterIpOverride;
  uint32_t ttsPcmRateHz;
  float ttsSpeed;
  float playbackVolume;
  bool useDhcpDns;
  bool autoPresence;
};

struct RuntimeState {
  bool microphoneReady = false;
  bool speakerReady = false;
  bool distanceSensorReady = false;
  bool sessionActive = false;
  bool processingActive = false;
  bool playbackActive = false;
  bool targetPresent = false;
  bool voiceDetectedInSession = false;
  uint32_t sessionStartedAtMs = 0;
  uint32_t lastVoiceDetectedAtMs = 0;
  uint32_t lastPlaybackEndedAtMs = 0;
  uint32_t lastWiFiAttemptAtMs = 0;
  uint32_t lastSensorPollAtMs = 0;
  uint32_t lastAutoTriggerAtMs = 0;
  uint32_t currentSpeakerSampleRate = kAudioSampleRateHz;
  uint16_t lastDistanceMm = 8190;
  String activityMode = "idle";
  String activityMessage = "Ready";
  String lastTranscript;
  String lastTranslation;
};

struct HttpResponseHeaders {
  int statusCode = 0;
  int contentLength = -1;
  bool isChunked = false;
  String contentType;
};

struct AudioBuffer {
  uint8_t *data = nullptr;
  size_t length = 0;
  size_t capacity = 0;
};
