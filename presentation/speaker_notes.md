# Understand Any Language (UAL) - Speaker Notes

These notes are not placed in the PDF; they are for presentation support and question prep.

## Main Message

The project is an IoT data chain:

```text
VL53L0X distance sensor + INMP441 microphone sensor
        |
ESP32-S3 microcontroller
        |
Wi-Fi / TCP-IP / TLS / HTTPS REST
        |
OpenRouter cloud AI chain
STT -> translation LLM -> TTS
        |
MAX98357A amplifier + speaker actuator
```

## Slide 1 - Cover

One sentence: The device collects data from two sensors, sends it to the internet with ESP32-S3, processes it in cloud AI, and plays the result through the speaker.

## Slide 2 - System Components

Clearly separate the roles of sensor, microcontroller, internet layer, cloud AI, and actuator.

## Slide 3 - Pipeline

Flow: sensors -> ESP32-S3 -> HTTPS -> cloud AI -> speaker.

## Slide 4 - Sensor Rules

- VL53L0X is read every 150 ms.
- Presence triggers at 1000 mm or less.
- Presence clears above 1200 mm.
- There is a 3.5-second retrigger delay.
- INMP441 sends the speech signal over I2S.

## Slide 5 - Audio Format

- The microphone does not create a file; ESP32 reads the sample stream.
- Recording is stored as 16 kHz mono signed PCM16.
- 320 samples are about a 20 ms audio chunk.
- A 44-byte WAV header is prepended for STT.
- WAV data is written into the JSON body as base64.

## Slide 6 - Internet Protocols

Network chain: Wi-Fi -> DNS -> TCP/IP -> TLS -> HTTPS REST.

OpenRouter endpoints:

- `/api/v1/audio/transcriptions`: STT.
- `/api/v1/chat/completions`: translation.
- `/api/v1/audio/speech`: TTS.

## Slide 7 - Cloud AI Models

- STT: `openai/whisper-large-v3`, converts speech to text.
- Translation: `mistralai/mistral-small-2603`, translates the transcript into the target language.
- TTS: `openai/gpt-4o-mini-tts-2025-12-15`, turns text into raw PCM speech with neural TTS.

STT = Speech to Text. TTS = Text to Speech.

## Slide 8 - Speaker Transfer

The TTS response is not downloaded as a file. Firmware reads PCM bytes from the HTTP body through a 512-byte network buffer, preserves PCM16 alignment, converts samples into 32-bit I2S frames, and sends them to the MAX98357A amplifier.

## Slide 9 - Control Logic

State flow:

```text
Ready -> Detect -> Listen -> Send to cloud -> Speak -> Ready
```

## Slide 10 - Web Panel

The panel shows connection, sensor, recording, transcript, and translation states. Wi-Fi, language, model, and audio settings can also be managed there.

## Slide 11 - Closing

Closing sentence:

"ESP32-S3 works as an end-to-end IoT prototype that sends real-world data from two sensors to cloud AI over HTTPS and plays the result through a speaker actuator."

## Possible Short Answers

### Does it work without internet?

No. STT, translation, and TTS run in the cloud, so internet access is required.

### Why cloud AI?

ESP32-S3 is not suitable for running large STT/LLM/TTS models locally. Cloud processing gives better model quality and updatability.

### Why raw PCM?

Raw PCM can be streamed directly over I2S to the speaker. MP3 would require an additional decoder.

### What is the weakest point?

The system depends on internet access and API quota. Productization would require web panel authentication, production-grade TLS verification, and stronger privacy measures.
