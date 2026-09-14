# Project Phoenix v1.0.12a — Robust Last Bank Restore

## Release status

Hardware-tested maintenance release based on **v1.0.12 — Active Bank Persistence & Encoder Bank Manager**.

## Fixed

- Hardened encoder-button service-mode detection. Service mode is entered only when GPIO42 remains LOW throughout the debounce/hold sampling window.
- Added verification of NVS session writes by reading the stored value back.
- Added redundant active-bank persistence in `/PHOENIX/LASTBANK.CFG`.
- Boot restore now uses NVS first and SD-card fallback when the NVS entry is unavailable or invalid.
- If NVS points to an unavailable bank and the SD fallback contains a different valid bank, Phoenix can fall back to the SD entry.
- A successful restore re-synchronizes both persistence stores.
- Boot status messages for the last-bank restore remain visible long enough to diagnose the restore path.

## Retained from v1.0.12

- Separation between the **active bank** and the **Disk Utilities bank-selection cursor**.
- Encoder-based LOAD BANK / SAVE BANK / DELETE BANK workflow.
- Browsing a bank number alone does not change the active bank.
- Only a successful LOAD or SAVE changes the persistent active-bank state.

## Compatibility

```text
BANK_VERSION        = 15
MULTISAMPLE_VERSION = 10
```

No audio-engine, sequencer or bank-format change was introduced by v1.0.12a.

## Recommended verification

1. Load a known bank, for example BANK37.
2. Reset or power-cycle Phoenix without holding the encoder button.
3. Verify that the display reports the last bank and reloads BANK37.
4. Enter Disk Utilities and browse to another bank number without loading it.
5. Leave with F8 and reset again.
6. Verify that the previously loaded bank is still restored.

The above restore behavior has been confirmed on the target hardware for v1.0.12a.
