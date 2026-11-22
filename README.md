# VitalGauge — Advanced Health Monitor

**Current source version:** 2.5.0. **Platform:** Emery (Pebble Time 2).

VitalGauge is a watch-local health and guided-breathing app. Health processing stays on the watch; the current manifest requests the Pebble Health capability.

## Using VitalGauge

Open the app to view health-related status and available monitoring screens. Hold **Select** for watch-side settings; the Pebble phone app also provides settings. Current options include dark mode, haptic intensity (off/low/high), scan backlight, and breathing patterns:

- Box: 4–4–4–4
- Relax: 4–7–8
- Sleep: 4–8

Health-derived values depend on available watch sensor history and are **wellness indicators, not medical diagnostics**.

## Health estimates

Energy Reserve carries across midnight. It uses completed minutes of Pebble Health history for
activity and native sleep, along with recent validated stress scans and a personal resting HR
baseline when available. Missing coverage leaves the last estimate in place with its timestamp.
The Contributors screen shows the changes applied in the latest update.

Morning Readiness starts with the main sleep session and adds personal resting HR and HRV
comparisons as baselines become available. The Sleep view tracks naps separately and distinguishes
Pebble-reported, inferred, awake, and unknown time. The Stress view keeps optical scans, heart-rate
history, and guided breathing available while calibration runs. To help build a resting HRV baseline,
run a scan while sitting quietly within two hours of waking.

Background monitoring is optional and uses Pebble's shared worker slot. It checks for new history
about every ten minutes. Automatic stress prompts are an additional experimental option and default
to off. In Trends, Select opens older scoring history when it exists.

## Build and source

Run `python3 tests/hub-and-stress.test.py` and `python3 tests/health-model.test.py` before
building. Host tests exercise synthetic data and watch SDK calls are mocked where appropriate.
Physical Time 2 testing is needed to characterize optical sampling and battery usage.

Native C modules in `src/c/` implement health data, drawing, audio, settings and screens. `worker_src/c/worker.c` contains the background worker. Use the [PBW build workflow](.github/workflows/direct-pbw.yml) or a compatible Pebble SDK. `package.json` defines version, supported watches and message keys.

## Support

Include the version, watch platform and reproduction steps when filing issues; omit personal health records. https://ko-fi.com/ericmccormick
