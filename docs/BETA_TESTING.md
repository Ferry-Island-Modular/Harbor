# Harbor beta testing

Thank you for testing Harbor. This is prerelease software, so please keep a
copy of any source audio and generated wavetable banks that matter to you.

## Install

### macOS

Public beta disk images should be Developer ID signed and notarized. A private
beta may instead be ad-hoc signed while Apple enrollment is pending. In that
case, macOS Gatekeeper cannot verify the developer and may block the first
launch.

1. Open the `.dmg`.
2. Drag **Harbor** to **Applications**.
3. Launch Harbor from Applications.

The current beta supports Apple Silicon Macs.

#### If Gatekeeper blocks the private beta

Only continue if the disk image came from the Harbor beta release you expected.
Verify its checksum against `SHA256SUMS.txt` as described below before
overriding the warning.

1. Try to launch Harbor once, then dismiss the warning.
2. Open **System Settings → Privacy & Security** and scroll to **Security**.
3. Find the message that Harbor was blocked and click **Open Anyway**. This
   option is normally available for about an hour after the failed launch.
4. Authenticate with your Mac login password if asked, then confirm **Open**.

This creates an exception for Harbor only. It does not disable Gatekeeper.
Apple documents this process in
[Open a Mac app from an unknown developer][apple-open-unknown].

If **Open Anyway** does not appear, Control-click Harbor in the Applications
folder, choose **Open**, and confirm **Open** if macOS offers that choice.

As a last resort for a checksum-verified private beta, open Terminal and remove
the downloaded-file quarantine attribute from Harbor only:

```bash
xattr -dr com.apple.quarantine "/Applications/Harbor.app"
```

Never disable Gatekeeper or System Integrity Protection globally. If these
steps do not work, report the exact warning and macOS version; a managed Mac may
have an organization policy that prevents local overrides.

### Windows

Harbor's private-beta Windows packages are not code-signed yet.

- **Installer:** open the setup `.exe`.
- **Portable:** extract the entire ZIP before opening `Harbor.exe`. Do not move
  the executable away from its adjacent DLL and plug-in folders.

The installer is per-user and can be removed from Windows Settings. It may
request administrator approval only if Microsoft's Visual C++ runtime needs to
be installed.

#### If Microsoft Defender SmartScreen blocks the private beta

An unsigned beta has no publisher reputation, so Windows may display **Windows
protected your PC**. Only continue if the package came from the Harbor beta
release you expected. Verify its checksum against `SHA256SUMS.txt` first.

1. In the SmartScreen dialog, confirm the app name is the Harbor installer or
   `Harbor.exe`.
2. Select **More info**.
3. Select **Run anyway**.

Microsoft's developer guidance describes this warning as expected for unsigned
downloads and documents the per-file **Run anyway** choice in
[SmartScreen reputation for Windows apps][microsoft-smartscreen].

Do not turn off SmartScreen, Microsoft Defender Antivirus, or real-time
protection globally. If **Run anyway** is unavailable, report the exact warning
and Windows version; an administrator or organization policy may forbid local
overrides.

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

<https://github.com/Ferry-Island-Modular/Harbor/issues/new/choose>

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

[apple-open-unknown]: https://support.apple.com/guide/mac-help/open-a-mac-app-from-an-unknown-developer-mh40616/mac
[microsoft-smartscreen]: https://learn.microsoft.com/windows/apps/package-and-deploy/smartscreen-reputation
