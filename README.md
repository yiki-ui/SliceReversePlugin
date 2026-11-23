# SliceReverse (VST3)

Cuts incoming audio on the host's bar/beat grid and reverses **each slice in place**
instead of reversing the whole clip. No manual chopping or rearranging.

## Parameters
- **Slice Size**: 1/32, 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars (bar uses the project time signature)
- **Reverse Every Nth Slice**: 1 = every slice, 2 = alternate (the others play forward)
- **Mix**: reversed vs. normal (both are delayed identically, so they stay aligned)
- **Edge Fade (ms)**: short fade on reversed slice edges to prevent clicks
- **Delay Compensation**: reports one slice of latency so the host shifts the result back on the grid

## Build (Windows, no local setup)
1. Create a GitHub repo and push this folder.
2. Open the Actions tab, run "Build SliceReverse".
3. Download `SliceReverse-Windows-VST3` and copy `SliceReverse.vst3` into
   `C:\Program Files\Common Files\VST3`.
4. Rescan plugins in FL Studio.

## Build locally
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

## FL Studio notes
- Put it on a mixer insert. It follows tempo/position from the playlist.
- Keep the plugin's "Delay compensation" on, and make sure FL's mixer PDC is enabled.
- When the transport is stopped it free-runs on the current tempo.
