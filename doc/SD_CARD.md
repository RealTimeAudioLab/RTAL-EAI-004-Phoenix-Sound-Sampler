# SD Card Layout

Phoenix uses `/PHOENIX` as the application root.

```text
/PHOENIX/
├── BANKS/
│   ├── BANK01/
│   │   ├── BANK.CFG
│   │   ├── MULTISAMPLE.CFG   (when used)
│   │   └── sample data / slot files
│   ├── BANK02/
│   └── ...
├── WAV/
├── EXPORT/
├── CONFIG.TXT
└── LASTBANK.CFG
```

`LASTBANK.CFG` was added to the robust v1.0.12a restore path as an SD-card fallback for the active-bank information kept in ESP32 NVS.

Current format versions:

```text
BANK_VERSION        = 16
MULTISAMPLE_VERSION = 10
```
