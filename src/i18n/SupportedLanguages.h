#pragma once

#include <Arduino.h>
#include <stddef.h>

struct SupportedLanguage {
  const char *code;
  const char *name;
};

extern const SupportedLanguage kSupportedLanguages[] PROGMEM;
extern const size_t kSupportedLanguageCount;
extern const char kAutoDetectLanguageCode[];
