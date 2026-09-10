#pragma once
#include <Arduino.h>

class PhoenixUSBStorage {
public:
  enum Mode : uint8_t {
    MODE_READ_ONLY = 0,
    MODE_READ_WRITE = 1
  };

  PhoenixUSBStorage();

  bool beginReadOnly();
  bool beginReadWrite();
  bool begin(Mode mode);

  // Stops MSC, releases the raw SD driver and mounts FAT back into Phoenix.
  // Returns true only when the SD filesystem is available to Phoenix again.
  bool endAndRemount();

  // Emergency shutdown without remount. Normally use endAndRemount().
  void end();

  bool active() const { return _active; }
  bool ejected() const { return _ejected; }
  bool hostConnected() const { return _hostConnected; }
  bool writable() const { return _mode == MODE_READ_WRITE; }
  bool writeOccurred() const { return _writeOccurred; }
  bool remountOk() const { return _remountOk; }
  uint32_t readKilobytes() const { return (uint32_t)(_bytesRead >> 10); }
  uint32_t writtenKilobytes() const { return (uint32_t)(_bytesWritten >> 10); }
  uint32_t ioErrors() const { return _ioErrors; }
  const char* statusText() const;

  // Called only by TinyUSB MSC callbacks.
  void notifyHostStartStop(bool start, bool loadEject);
  void notifyHostActivity(bool writeAccess, uint32_t bytes, bool ok);

private:
  bool startRawCard();
  void stopRawCard();

  bool _active;
  volatile bool _ejected;
  volatile bool _hostConnected;
  Mode _mode;
  volatile bool _writeOccurred;
  bool _remountOk;
  volatile uint64_t _bytesRead;
  volatile uint64_t _bytesWritten;
  volatile uint32_t _ioErrors;
  uint8_t _rawPdrv;
};
