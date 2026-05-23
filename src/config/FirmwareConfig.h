#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include <stdint.h>

constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kSerialReadyTimeoutMs = 5000;

constexpr int kVl53l0xSdaPin = 8;
constexpr int kVl53l0xSclPin = 9;
constexpr uint8_t kVl53l0xI2cAddress = 0x29;
constexpr uint32_t kI2cClockHz = 400000;
constexpr uint16_t kI2cTimeoutMs = 100;

constexpr i2s_port_t kMicrophoneI2sPort = I2S_NUM_0;
constexpr int kMicrophoneSckPin = 12;
constexpr int kMicrophoneWsPin = 13;
constexpr int kMicrophoneSdPin = 14;

constexpr i2s_port_t kSpeakerI2sPort = I2S_NUM_1;
constexpr int kSpeakerBclkPin = 15;
constexpr int kSpeakerLrcPin = 16;
constexpr int kSpeakerDinPin = 17;

constexpr uint32_t kAudioSampleRateHz = 16000;
constexpr uint16_t kAudioChunkSamples = 320;
constexpr uint32_t kMaxRecordWindowMs = 30000;
constexpr uint32_t kSilenceStopDurationMs = 3000;
constexpr uint32_t kMaxRecordedPcmBytes = (kAudioSampleRateHz * 2 * kMaxRecordWindowMs) / 1000;
constexpr uint32_t kMinimumRecordedPcmBytes = 3200;
constexpr uint32_t kWiFiConnectTimeoutMs = 20000;
constexpr uint32_t kWiFiReconnectIntervalMs = 10000;
constexpr uint32_t kSensorPollIntervalMs = 150;
constexpr uint32_t kSensorRearmDelayMs = 3500;
constexpr uint32_t kPostPlaybackListenDelayMs = 400;
constexpr uint16_t kPresenceTriggerDistanceMm = 1000;
constexpr uint16_t kPresenceClearDistanceMm = 1200;
constexpr uint32_t kSerialBufferLength = 512;
constexpr int32_t kMicrophoneDigitalGain = 4;
constexpr int32_t kVoiceActivityRmsThreshold = 900;
constexpr int32_t kVoiceActivityPeakThreshold = 2500;
constexpr float kDefaultPlaybackVolume = 4.0F;
constexpr uint16_t kHttpLineLimit = 1024;
constexpr uint32_t kHttpReadTimeoutMs = 45000;
constexpr uint32_t kJsonBodyLimitBytes = 32768;
constexpr uint32_t kMaxTtsAudioBytes = 1024 * 1024;
constexpr uint16_t kControlPanelPort = 80;
constexpr uint32_t kWebRequestBodyLimitBytes = 8192;
constexpr uint32_t kWebRestartDelayMs = 750;
constexpr size_t kBase64InputChunkBytes = 768;
constexpr size_t kBase64OutputChunkBytes = ((kBase64InputChunkBytes + 2) / 3) * 4;
constexpr size_t kPcmPlaybackChunkBytes = kAudioChunkSamples * sizeof(int16_t);

constexpr char kPreferencesNamespace[] = "translator";
constexpr char kOpenRouterHost[] = "openrouter.ai";
constexpr uint16_t kOpenRouterPort = 443;
constexpr char kOpenRouterSttPath[] = "/api/v1/audio/transcriptions";
constexpr char kOpenRouterChatPath[] = "/api/v1/chat/completions";
constexpr char kOpenRouterTtsPath[] = "/api/v1/audio/speech";
constexpr char kDefaultSourceLanguage[] = "en";
constexpr char kDefaultTargetLanguage[] = "tr";
constexpr char kDefaultSttModel[] = "openai/whisper-large-v3";
constexpr char kDefaultTranslationModel[] = "mistralai/mistral-small-2603";
constexpr char kDefaultTtsModel[] = "openai/gpt-4o-mini-tts-2025-12-15";
constexpr char kMp3OnlyMistralTtsModel[] = "mistralai/voxtral-mini-tts-2603";
constexpr char kDefaultTtsVoice[] = "alloy";
constexpr char kDefaultPrimaryDns[] = "1.1.1.1";
constexpr char kDefaultSecondaryDns[] = "8.8.8.8";
constexpr uint32_t kDefaultTtsPcmRateHz = 24000;
constexpr float kDefaultTtsSpeed = 1.0F;
