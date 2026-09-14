#include "PhoenixSerial.h"
#include "PhoenixUSBStorage.h"
#include <SD.h>
#include <SPI.h>
#include <string.h>
#include "sd_diskio.h"
extern "C" {
  // FatFs scalar types (BYTE, WORD, DWORD, UINT) are declared in ff.h.
  // ESP32 Arduino Core 2.0.16 requires ff.h before diskio.h, matching
  // the include order used by its own SD implementation.
  #include "ff.h"
  #include "diskio.h"
}

namespace {
  static const uint8_t PHX_SD_CS = 9;
  static const uint32_t PHX_SD_SPI_HZ = 4000000UL;
  static const size_t PHX_MSC_SCRATCH_SIZE = 512U;
}

#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
  #include "USB.h"
  #include "USBMSC.h"

  static USBMSC gPhoenixMSC;
  static PhoenixUSBStorage *gPhoenixUSBStorage = nullptr;
  static uint32_t gPhoenixMscBlockCount = 0;
  static uint16_t gPhoenixMscBlockSize = 0;
  static uint8_t gPhoenixMscPdrv = 0xFF;
  static volatile bool gPhoenixMscWritable = false;
  static uint8_t gPhoenixMscScratch[PHX_MSC_SCRATCH_SIZE];

  // Arduino-ESP32 2.0.16 has no USBMSC::isWritable() setter. TinyUSB asks
  // this callback before issuing WRITE10, so report the selected mode here.
  #if !defined(ESP_ARDUINO_VERSION_MAJOR) || (ESP_ARDUINO_VERSION_MAJOR < 3)
  extern "C" bool tud_msc_is_writable_cb(uint8_t) {
    return gPhoenixMscWritable;
  }
  #endif

  static bool phxRawReadSector(uint8_t *dst, uint32_t sector) {
    return gPhoenixMscPdrv != 0xFF && dst && sd_read_raw(gPhoenixMscPdrv, dst, sector);
  }

  static bool phxRawWriteSector(uint8_t *src, uint32_t sector) {
    return gPhoenixMscPdrv != 0xFF && src && sd_write_raw(gPhoenixMscPdrv, src, sector);
  }

  static int32_t phxMscRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    if (!buffer || bufsize == 0U || gPhoenixMscBlockSize == 0U ||
        gPhoenixMscBlockCount == 0U || gPhoenixMscPdrv == 0xFF) {
      if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(false, bufsize, false);
      return -1;
    }
    if (gPhoenixMscBlockSize > sizeof(gPhoenixMscScratch)) {
      if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(false, bufsize, false);
      return -1;
    }

    const uint64_t mediumBytes = (uint64_t)gPhoenixMscBlockCount * (uint64_t)gPhoenixMscBlockSize;
    const uint64_t firstByte = (uint64_t)lba * (uint64_t)gPhoenixMscBlockSize + (uint64_t)offset;
    if (firstByte >= mediumBytes || (uint64_t)bufsize > (mediumBytes - firstByte)) {
      if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(false, bufsize, false);
      return -1;
    }

    uint8_t *dst = static_cast<uint8_t*>(buffer);
    uint32_t remaining = bufsize;
    uint32_t sector = (uint32_t)(firstByte / gPhoenixMscBlockSize);
    uint32_t inSector = (uint32_t)(firstByte % gPhoenixMscBlockSize);

    while (remaining > 0U) {
      const uint32_t available = (uint32_t)gPhoenixMscBlockSize - inSector;
      const uint32_t chunk = (remaining < available) ? remaining : available;

      if (inSector == 0U && chunk == gPhoenixMscBlockSize) {
        if (!phxRawReadSector(dst, sector)) {
          if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(false, bufsize, false);
          return -1;
        }
      } else {
        if (!phxRawReadSector(gPhoenixMscScratch, sector)) {
          if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(false, bufsize, false);
          return -1;
        }
        memcpy(dst, gPhoenixMscScratch + inSector, chunk);
      }

      dst += chunk;
      remaining -= chunk;
      ++sector;
      inSector = 0U;
    }

    if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(false, bufsize, true);
    return (int32_t)bufsize;
  }

  static int32_t phxMscWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    if (!gPhoenixMscWritable || !buffer || bufsize == 0U ||
        gPhoenixMscBlockSize == 0U || gPhoenixMscBlockCount == 0U ||
        gPhoenixMscPdrv == 0xFF) {
      if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(true, bufsize, false);
      return -1;
    }
    if (gPhoenixMscBlockSize > sizeof(gPhoenixMscScratch)) {
      if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(true, bufsize, false);
      return -1;
    }

    const uint64_t mediumBytes = (uint64_t)gPhoenixMscBlockCount * (uint64_t)gPhoenixMscBlockSize;
    const uint64_t firstByte = (uint64_t)lba * (uint64_t)gPhoenixMscBlockSize + (uint64_t)offset;
    if (firstByte >= mediumBytes || (uint64_t)bufsize > (mediumBytes - firstByte)) {
      if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(true, bufsize, false);
      return -1;
    }

    uint8_t *src = buffer;
    uint32_t remaining = bufsize;
    uint32_t sector = (uint32_t)(firstByte / gPhoenixMscBlockSize);
    uint32_t inSector = (uint32_t)(firstByte % gPhoenixMscBlockSize);

    while (remaining > 0U) {
      const uint32_t available = (uint32_t)gPhoenixMscBlockSize - inSector;
      const uint32_t chunk = (remaining < available) ? remaining : available;

      if (inSector == 0U && chunk == gPhoenixMscBlockSize) {
        if (!phxRawWriteSector(src, sector)) {
          if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(true, bufsize, false);
          return -1;
        }
      } else {
        // USB can legally send a partial sector. Preserve the untouched bytes
        // with a read-modify-write transaction.
        if (!phxRawReadSector(gPhoenixMscScratch, sector)) {
          if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(true, bufsize, false);
          return -1;
        }
        memcpy(gPhoenixMscScratch + inSector, src, chunk);
        if (!phxRawWriteSector(gPhoenixMscScratch, sector)) {
          if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(true, bufsize, false);
          return -1;
        }
      }

      src += chunk;
      remaining -= chunk;
      ++sector;
      inSector = 0U;
    }

    if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostActivity(true, bufsize, true);
    return (int32_t)bufsize;
  }

  static bool phxMscStartStop(uint8_t, bool start, bool load_eject) {
    if (gPhoenixUSBStorage) gPhoenixUSBStorage->notifyHostStartStop(start, load_eject);
    return true;
  }
#endif

PhoenixUSBStorage::PhoenixUSBStorage()
: _active(false), _ejected(false), _hostConnected(false),
  _mode(MODE_READ_ONLY), _writeOccurred(false), _remountOk(true),
  _bytesRead(0), _bytesWritten(0), _ioErrors(0), _rawPdrv(0xFF) {}

bool PhoenixUSBStorage::beginReadOnly() {
  return begin(MODE_READ_ONLY);
}

bool PhoenixUSBStorage::beginReadWrite() {
  return begin(MODE_READ_WRITE);
}

bool PhoenixUSBStorage::startRawCard() {
  // The FAT/VFS mount must not coexist with a writable USB host. SD.end()
  // closes Phoenix' filesystem view and releases the Arduino SD driver.
  SD.end();
  delay(20);

  _rawPdrv = sdcard_init(PHX_SD_CS, &SPI, PHX_SD_SPI_HZ);
  if (_rawPdrv == 0xFF) return false;

  // sdcard_init() only registers the physical drive. Initialize the card
  // explicitly because no FAT mount is performed in USB raw mode.
  const DSTATUS initStatus = disk_initialize(_rawPdrv);
  if (initStatus & STA_NOINIT) {
    stopRawCard();
    return false;
  }

  const uint32_t sectors = sdcard_num_sectors(_rawPdrv);
  const uint32_t sectorSize = sdcard_sector_size(_rawPdrv);
  if (sectors == 0U || sectorSize == 0U || sectorSize > PHX_MSC_SCRATCH_SIZE) {
    stopRawCard();
    return false;
  }

#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
  gPhoenixMscPdrv = _rawPdrv;
  gPhoenixMscBlockCount = sectors;
  gPhoenixMscBlockSize = (uint16_t)sectorSize;
  if (!phxRawReadSector(gPhoenixMscScratch, 0U)) {
    stopRawCard();
    return false;
  }
#endif
  return true;
}

void PhoenixUSBStorage::stopRawCard() {
#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
  gPhoenixMscPdrv = 0xFF;
  gPhoenixMscBlockCount = 0;
  gPhoenixMscBlockSize = 0;
#endif
  if (_rawPdrv != 0xFF) {
    sdcard_uninit(_rawPdrv);
    _rawPdrv = 0xFF;
    delay(20);
  }
}

bool PhoenixUSBStorage::begin(Mode mode) {
  if (_active) return true;

#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
  _mode = mode;
  _ejected = false;
  _hostConnected = false;
  _writeOccurred = false;
  _remountOk = false;
  _bytesRead = 0;
  _bytesWritten = 0;
  _ioErrors = 0;

  if (!startRawCard()) {
    // Best effort: restore normal Phoenix access when raw startup fails.
    _remountOk = SD.begin(PHX_SD_CS, SPI);
    Serial.println(F("C028e USB MSC raw SD start failed"));
    return false;
  }

  gPhoenixUSBStorage = this;
  gPhoenixMscWritable = (_mode == MODE_READ_WRITE);

  gPhoenixMSC.mediaPresent(false);
  gPhoenixMSC.vendorID("RTAL");
  gPhoenixMSC.productID(_mode == MODE_READ_WRITE ? "PHOENIX SD RW" : "PHOENIX SD RO");
  gPhoenixMSC.productRevision("1.1");
  gPhoenixMSC.onRead(phxMscRead);
  gPhoenixMSC.onWrite(phxMscWrite);
  gPhoenixMSC.onStartStop(phxMscStartStop);

  if (!gPhoenixMSC.begin(gPhoenixMscBlockCount, gPhoenixMscBlockSize)) {
    gPhoenixUSBStorage = nullptr;
    gPhoenixMscWritable = false;
    stopRawCard();
    _remountOk = SD.begin(PHX_SD_CS, SPI);
    return false;
  }

  USB.begin();
  gPhoenixMSC.mediaPresent(true);
  _active = true;

  PHX_INFO_PRINTF("C028e USB MSC %s sectors=%lu sectorSize=%u bootSig=%02X%02X\n",
                _mode == MODE_READ_WRITE ? "READ/WRITE" : "READ ONLY",
                (unsigned long)gPhoenixMscBlockCount,
                (unsigned)gPhoenixMscBlockSize,
                (unsigned)gPhoenixMscScratch[510],
                (unsigned)gPhoenixMscScratch[511]);
  return true;
#else
  (void)mode;
  Serial.println(F("C028e USB MSC unavailable: select USB Mode = USB-OTG (TinyUSB)"));
  return false;
#endif
}

bool PhoenixUSBStorage::endAndRemount() {
  const bool hadWrites = _writeOccurred;
  end();
  _remountOk = SD.begin(PHX_SD_CS, SPI);
  PHX_INFO_PRINTF("C028e USB MSC exit remount=%s writes=%s readKB=%lu writeKB=%lu errors=%lu\n",
                _remountOk ? "OK" : "FAIL",
                hadWrites ? "YES" : "NO",
                (unsigned long)readKilobytes(),
                (unsigned long)writtenKilobytes(),
                (unsigned long)_ioErrors);
  return _remountOk;
}

void PhoenixUSBStorage::end() {
#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
  if (_active) {
    // Flush the raw SD driver before taking the medium away from TinyUSB.
    // WRITE10 callbacks are synchronous, but CTRL_SYNC also covers any driver
    // side buffering before Phoenix remounts FAT.
    if (_mode == MODE_READ_WRITE && _rawPdrv != 0xFF) {
      const DRESULT syncResult = disk_ioctl(_rawPdrv, CTRL_SYNC, nullptr);
      if (syncResult != RES_OK) ++_ioErrors;
    }
    gPhoenixMSC.mediaPresent(false);
    delay(120);
    gPhoenixMSC.end();
  }
  gPhoenixUSBStorage = nullptr;
  gPhoenixMscWritable = false;
#endif
  stopRawCard();
  _active = false;
  _hostConnected = false;
  _ejected = false;
}

void PhoenixUSBStorage::notifyHostStartStop(bool start, bool loadEject) {
  PHX_INFO_PRINTF("C028e USB MSC STARTSTOP start=%u loadEject=%u\n",
                start ? 1U : 0U, loadEject ? 1U : 0U);
  if (start) {
    _hostConnected = true;
    _ejected = false;
  } else {
    // Windows may issue START STOP UNIT with START=0 but LoEj=0 when Explorer
    // removes the logical drive. Treat every host STOP as a completed release;
    // waiting only for LoEj caused a permanent 8LOCK with Core 2.0.16.
    _hostConnected = false;
    _ejected = true;
#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
    gPhoenixMSC.mediaPresent(false);
#endif
  }
}

void PhoenixUSBStorage::notifyHostActivity(bool writeAccess, uint32_t bytes, bool ok) {
  _hostConnected = true;
  if (!ok) {
    ++_ioErrors;
    return;
  }
  if (writeAccess) {
    _writeOccurred = true;
    _bytesWritten += bytes;
  } else {
    _bytesRead += bytes;
  }
}

const char* PhoenixUSBStorage::statusText() const {
  if (!_active) return _remountOk ? "USB STORAGE READY" : "SD REMOUNT FAILED";
  if (_ejected) return "EJECTED - F8 EXIT";
  if (_ioErrors) return "USB I/O ERROR";
  if (_mode == MODE_READ_WRITE) return _hostConnected ? "RW HOST CONNECTED" : "READ/WRITE ACTIVE";
  return _hostConnected ? "RO HOST CONNECTED" : "READ ONLY ACTIVE";
}
