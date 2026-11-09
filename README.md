# VitalGauge — Advanced Health Monitor

**Current source version:** 2.3.3. **Platform:** Emery (Pebble Time 2).

VitalGauge is a watch-local health and guided-breathing app. Health processing stays on the watch; the current manifest requests the Pebble Health capability.

## Using VitalGauge

Open the app to view health-related status and available monitoring screens. Hold **Select** for watch-side settings; the Pebble phone app also provides settings. Current options include dark mode, haptic intensity (off/low/high), scan backlight, and breathing patterns:

- Box: 4–4–4–4
- Relax: 4–7–8
- Sleep: 4–8

Health-derived values depend on available watch sensor history and are **wellness indicators, not medical diagnostics**.

## Build and source

Native C modules in `src/c/` implement health data, drawing, audio, settings and screens. `worker_src/c/worker.c` contains the background worker. Use the [PBW build workflow](.github/workflows/direct-pbw.yml) or a compatible Pebble SDK. `package.json` defines version, supported watches and message keys.

## Support

Include the version, watch platform and reproduction steps when filing issues; omit personal health records. https://ko-fi.com/ericmccormick
