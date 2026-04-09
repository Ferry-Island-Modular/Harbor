# Packaging scripts

## `package-macos.sh`

Builds and packages an Apple Silicon `.dmg` of Harbor for
internal team distribution. Bundles Qt frameworks via `macdeployqt` and
applies an ad-hoc code signature so Gatekeeper can parse the bundle.

**Not** notarized. First-time launch on a recipient machine will be
blocked by Gatekeeper with an "app is damaged" or "unidentified
developer" message — testers need the workaround below.

### Usage

```bash
# Build + package (run from repo root)
cpp/scripts/package-macos.sh

# Skip the cmake build step (use existing cpp/build/Harbor.app)
cpp/scripts/package-macos.sh --skip-build
```

Output: `cpp/build/dist/FIM-Config-Tool-<git-version>.dmg`

### Tester instructions

Send the recipient this snippet along with the `.dmg`:

> 1. Open the `.dmg` and drag **Harbor** to your Applications folder.
> 2. Open **Terminal** and run:
>    ```
>    xattr -dr com.apple.quarantine "/Applications/Harbor.app"
>    ```
> 3. Launch from Applications normally.
>
> Alternative (no Terminal): right-click the app in Applications → Open
> → click **Open** in the warning dialog. You only need to do this once;
> macOS remembers it after.

### Why the workaround is needed

The build is signed with an *ad-hoc* signature (`codesign --sign -`),
which is structurally valid but not issued by an Apple Developer ID
certificate. macOS Gatekeeper will refuse to launch unsigned or
ad-hoc-signed apps that arrived over the network (download, AirDrop,
email attachment, USB stick mounted as a network volume) until the
quarantine extended attribute is removed.

For a real public release we'd need:
1. Apple Developer Program membership ($99/yr)
2. A "Developer ID Application" certificate
3. `codesign` with that cert + hardened runtime
4. Submit to Apple's notary service via `notarytool`
5. `xcrun stapler staple` the result

This is tracked as a future improvement in `docs/followups.md`.

### Apple Silicon only

The script targets only the host architecture. On an arm64 dev machine
that means the `.dmg` runs on M1+ Macs only — Intel testers would need
a separate build (or a universal binary, which requires building twice
and `lipo`-merging the executables and Qt frameworks).
