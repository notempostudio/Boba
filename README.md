# Boba (VST3 / AU, macOS Apple Silicon)

C++/JUCE port of the Max for Live device **Boba.amxd**: a spectral (per-FFT-bin) sidechain compressor.
The algorithm was read straight out of the device's patchers and reimplemented; no Max or Ableton is needed.

## Instalación (macOS)

1. Descarga `Boba-x.y.z.dmg` desde la pestaña **Releases** del repositorio.
2. Abre el DMG y arrastra `Boba.vst3` sobre la carpeta **VST3** y `Boba.component` sobre **Components**
   (macOS pedirá la contraseña de administrador).
3. Como el plugin no está notarizado por Apple, macOS puede bloquearlo. En Terminal ejecuta:

       xattr -cr /Library/Audio/Plug-Ins/VST3/Boba.vst3
       xattr -cr /Library/Audio/Plug-Ins/Components/Boba.component

4. Reinicia tu DAW y vuelve a escanear plugins.

Instalación manual por usuario (sin contraseña): copia `Boba.vst3` a `~/Library/Audio/Plug-Ins/VST3/`
y `Boba.component` a `~/Library/Audio/Plug-Ins/Components/`.

## Compilar desde el código (macOS)

    cmake -B build -DCMAKE_BUILD_TYPE=Release            # universal arm64 + x86_64 por defecto
    cmake --build build --config Release --target Boba_VST3 Boba_AU -j8
    ./make_dmg.sh 1.0.0                                  # opcional: genera el DMG

Las salidas quedan en `build/Boba_artefacts/**`. Solo arm64: añade `-DCMAKE_OSX_ARCHITECTURES=arm64`.
Sin Mac: sube una etiqueta (`git tag v1.0.0 && git push origin v1.0.0`) y GitHub Actions compila y publica el DMG.

## Licencia

Este proyecto usa JUCE bajo su licencia AGPLv3, por lo que se distribuye bajo **AGPLv3** (ver `LICENSE`).

## What the engine does (per FFT frame, per channel)

1. Hann-windowed FFT of the main signal and of the sidechain (unnormalised, like Max's `fftin~`).
2. Threshold per bin = `Threshold + tilt(bin)`, where the tilt falls linearly from +20 dB (lowest bin) to -15 dB (top bin).
3. Reduction (dB) = `-Depth * max(0, binLevel - threshold)`.
4. Per-bin smoothing of the gain over time (attack = slide down, release = slide up), applied to the main spectrum, inverse FFT, overlap-add.

Time-domain around it: 18 Hz DC blockers, 5-band emphasis EQ on the sidechain, optional M/S,
Stereo Link (constant-power blend of each channel with the L/R average, 50 ms ramp), Delta (delayed dry minus wet),
Solo Side Chain (outputs the EQ'd sidechain).

## Verified here

`tests/test_core.cpp` (plain C++, run with `g++ -O2 -std=c++17 tests/test_core.cpp -o t && ./t`):
transparent to within 2e-6 for every FFT size 128-4096 and overlap 2-32 when nothing crosses the threshold;
a 1 kHz sidechain tone ducks only the 1 kHz part of the main signal (about -47 dB at 100 % depth, 5 kHz untouched);
Depth scales the reduction; Delta equals delayed dry minus wet; M/S round trip is transparent.

## NOT verified - please check

* **No A/B against the original.** I could not run the .amxd. Two things most likely to differ:
  * Threshold calibration: `kFftMagnitudeScale` in `BobaCore.h` (1.0 = unnormalised FFT, which matches the device's
    -70..+30 dB range). A constant dB offset against the original means changing this one number.
  * Emphasis EQ filter shapes: I mapped the `filtergraph~` types to RBJ biquads (HP, 3 peaks, LP). The defaults
    come from the device; the exact curve of Max's filters may differ slightly.

## Deliberate differences / things you will notice

* **Attack/Release (Time Mode = Original).** In the device, the ms value is divided by 1000 and then converted
  ms->samples, and the result is used as the smoothing factor in FFT *frames*. That makes it roughly `ms * 0.044` frames
  at 44.1 kHz, so anything below ~11 ms (including the defaults 9.6 / 26.7 ms) has no smoothing effect.
  `Original` reproduces this. `Accurate` treats the knobs as real milliseconds (time constant = ms / hop).
* **Latency** is `FFT size - 1` samples and is reported to the host (the device reported a fixed 1984).
  Changing FFT size/overlap changes it, and reallocates once on the audio thread.
* External sidechain uses the standard plugin sidechain input instead of Ableton's track-routing browser.
* Not ported: the spectrum/curve display.
