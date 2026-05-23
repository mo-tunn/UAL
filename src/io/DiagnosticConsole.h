#pragma once

#include <Arduino.h>

class DiagnosticConsole {
 public:
  /**
   * Starts both ESP32-S3 diagnostic serial outputs.
   *
   * Parameters:
   *   baudRate: Serial baud rate used by the PlatformIO monitor.
   * Returns: nothing.
   * Throws: never.
   */
  void begin(uint32_t baudRate) {
    Serial.begin(baudRate);
    Serial0.begin(baudRate);
  }

  /**
   * Waits for native USB CDC to attach without blocking forever.
   *
   * Parameters:
   *   timeoutMs: Maximum wait time in milliseconds.
   * Returns: nothing.
   * Throws: never.
   */
  void waitUntilReady(uint32_t timeoutMs) {
    const uint32_t startedAtMs = millis();
    while (!Serial && (millis() - startedAtMs < timeoutMs)) {
      delay(10);
    }
    delay(250);
  }

  /**
   * Prints a value to USB CDC and UART0.
   *
   * Parameters:
   *   value: Value accepted by Arduino Print::print.
   * Returns: nothing.
   * Throws: never.
   */
  template <typename Value>
  void print(const Value &value) {
    Serial.print(value);
    Serial0.print(value);
  }

  /**
   * Prints a formatted value to USB CDC and UART0.
   *
   * Parameters:
   *   value: Value accepted by Arduino Print::print.
   *   format: Arduino numeric format or decimal precision.
   * Returns: nothing.
   * Throws: never.
   */
  template <typename Value>
  void print(const Value &value, int format) {
    Serial.print(value, format);
    Serial0.print(value, format);
  }

  /**
   * Prints a line to USB CDC and UART0.
   *
   * Parameters:
   *   value: Value accepted by Arduino Print::println.
   * Returns: nothing.
   * Throws: never.
   */
  template <typename Value>
  void println(const Value &value) {
    Serial.println(value);
    Serial0.println(value);
  }

  /**
   * Prints a formatted line to USB CDC and UART0.
   *
   * Parameters:
   *   value: Value accepted by Arduino Print::println.
   *   format: Arduino numeric format or decimal precision.
   * Returns: nothing.
   * Throws: never.
   */
  template <typename Value>
  void println(const Value &value, int format) {
    Serial.println(value, format);
    Serial0.println(value, format);
  }

  /**
   * Prints an empty line to USB CDC and UART0.
   *
   * Parameters: none.
   * Returns: nothing.
   * Throws: never.
   */
  void println() {
    Serial.println();
    Serial0.println();
  }
};
