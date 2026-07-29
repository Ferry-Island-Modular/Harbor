# Harbor beta testing

Thank you for testing Harbor. This is prerelease software, so please keep a
copy of any source audio and generated wavetable banks that matter to you.

## Install

### macOS

The public beta disk image should be Developer ID signed and notarized.

1. Open the `.dmg`.
2. Drag **Harbor** to **Applications**.
3. Launch Harbor from Applications.

The current beta supports Apple Silicon Macs. If macOS reports that the app
cannot be verified, stop and report the exact message rather than disabling
system security.

### Windows

Harbor's private-beta Windows packages are not code-signed yet.

- **Installer:** open the setup `.exe`. If Microsoft Defender SmartScreen
  appears, confirm the file came directly from Ferry Island Modular, select
  **More info**, then **Run anyway**.
- **Portable:** extract the entire ZIP before opening `Harbor.exe`. Do not move
  the executable away from its adjacent DLL and plug-in folders.

The installer is per-user and can be removed from Windows Settings. It may
request administrator approval only if Microsoft's Visual C++ runtime needs to
be installed.

## Verify the download

Each beta release includes `SHA256SUMS.txt`. Verification is optional for the
private beta, but useful when diagnosing a damaged or incomplete download.

macOS:

```bash
shasum -a 256 Harbor-*.dmg
```

Windows PowerShell:

```powershell
Get-FileHash .\Harbor-*-windows-x64-setup.exe -Algorithm SHA256
```

Compare the displayed hash with the matching entry in `SHA256SUMS.txt`.

## Smoke-test checklist

Please note anything surprising, even if Harbor does not crash.

- [ ] Harbor launches and the text, icons, and window layout look correct.
- [ ] The desired audio device can be selected.
- [ ] Preview playback starts, stops, changes pitch, and changes volume.
- [ ] **Single WAV** accepts an ordinary recording and generates a bank.
- [ ] **Serum WAV** accepts a Serum-format wavetable and generates a bank.
- [ ] **Three WAVs** accepts three recordings and generates a bank.
- [ ] Moving X, Y, and Z in the preview produces smooth, audible changes.
- [ ] Four Seas export creates eight WAV page files.
- [ ] WaveEdit export creates eight correctly sized WAV page files.
- [ ] A generated Four Seas bank loads and plays correctly on hardware, if
      hardware is available.
- [ ] Closing and reopening Harbor preserves appropriate settings.
- [ ] Windows installer upgrades/reinstalls cleanly and can be uninstalled.

Do not spend hours completing every combination. A normal session followed by
the relevant checklist items is more valuable than exhaustive clicking.

## Report a problem

Open an issue at:

<https://github.com/jgoney/fim-config-tool/issues/new/choose>

Include:

- Harbor version from **Harbor → About Harbor**;
- operating system and computer architecture;
- which input mode and axis options were selected;
- clear reproduction steps;
- what happened and what you expected;
- the input WAV and generated bank when you have permission to share them; and
- screenshots or screen recordings when the issue is visual.

For audio problems, mention the selected audio device and whether other audio
applications worked at the same time. Please do not upload private recordings
or copyrighted source material you are not allowed to share.
