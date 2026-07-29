# Packaging scripts

## macOS

`package-macos.sh` bundles Qt into a staging copy of Harbor and creates a
drag-to-install `.dmg`. It never modifies the development `.app`.

With no signing options it produces an ad-hoc-signed internal build. With a
Developer ID identity and `--notarize`, it enables the hardened runtime, adds
secure timestamps, submits the DMG to Apple's notary service, waits for
acceptance, staples the ticket, and checks it with Gatekeeper.

### Usage

```bash
# Internal build using the default src/build directory.
src/scripts/package-macos.sh

# Package the repository-root build used by local Codex sessions.
src/scripts/package-macos.sh --skip-build --build-dir build

# Public release after completing the one-time setup below.
export APPLE_DEVELOPER_ID="Developer ID Application: Ferry Island Modular Oy (TEAMID)"
export APPLE_NOTARY_KEYCHAIN_PROFILE="harbor-notary"
src/scripts/package-macos.sh --notarize
```

Output: `<build-dir>/dist/Harbor-<git-version>.dmg`

Run `src/scripts/package-macos.sh --help` for all overrides.

### One-time Apple setup

1. Enroll in the [Apple Developer Program][apple-membership]. Organization
   enrollment is preferable if Gatekeeper should display Ferry Island Modular
   rather than an individual's legal name.
2. Install a **Developer ID Application** certificate and its private key in
   the login Keychain. Xcode can create this from Settings → Accounts →
   Manage Certificates.
3. Create an app-specific password for the Apple Account, then store the
   notarization credentials in Keychain:

   ```bash
   xcrun notarytool store-credentials "harbor-notary" \
       --apple-id "developer@example.com" \
       --team-id "TEAMID"
   ```

   `notarytool` securely prompts for the app-specific password. The release
   script receives only the Keychain profile name; no password is placed in
   the repository, environment, or shell history.
4. Confirm the certificate identity:

   ```bash
   security find-identity -v -p codesigning
   ```

The certificate name must begin with `Developer ID Application`. A Mac
Development, Apple Development, Mac Distribution, or ad-hoc identity will not
satisfy Apple's notarization requirements.

[apple-membership]: https://developer.apple.com/programs/enroll/

### Tester instructions

For a notarized release:

> 1. Open the `.dmg` and drag **Harbor** to your Applications folder.
> 2. Launch Harbor from Applications.

For an ad-hoc internal build, Gatekeeper may require right-click → Open, or:

```bash
xattr -dr com.apple.quarantine "/Applications/Harbor.app"
```

### Apple Silicon only

The script targets only the host architecture. On an arm64 dev machine
that means the `.dmg` runs on M1+ Macs only — Intel testers would need
a separate build (or a universal binary, which requires building twice
and `lipo`-merging the executables and Qt frameworks).

## Windows beta packages

`package-windows.ps1` builds a self-contained Qt deployment folder and emits:

- a per-user NSIS installer with upgrade/uninstall support; and
- a portable ZIP for testers who prefer not to install.

The installer does not require administrator access for Harbor itself. It
bundles Microsoft's official Visual C++ Redistributable, which may request
elevation if the runtime needs to be installed.

Run from a Windows PowerShell session with Qt, Visual Studio, and NSIS on
`PATH`:

```powershell
.\src\scripts\package-windows.ps1

# Package an existing Release build.
.\src\scripts\package-windows.ps1 -SkipBuild -BuildDir src\build

# Produce only the portable ZIP when NSIS is unavailable.
.\src\scripts\package-windows.ps1 -SkipBuild -SkipInstaller
```

Outputs are written to `<build-dir>\dist`. CI publishes both files in the
`fim-config-tool-windows` artifact.

These beta packages are unsigned. SmartScreen may require the tester to click
**More info → Run anyway**, after confirming that the file came directly from
Ferry Island Modular. Self-signing would not remove that warning.

## Windows signing without an annual certificate

The preferred low-cost path is an MSIX published through the Microsoft Store.
New individual and company developer accounts have no registration fee, and
the Store signs MSIX packages, hosts them, and supplies automatic updates.

For direct downloads, Microsoft Artifact Signing is the managed alternative to
buying and protecting a traditional OV certificate. Its Basic tier is
currently $9.99/month. Neither Artifact Signing nor an OV/EV certificate
guarantees that a brand-new download avoids SmartScreen reputation prompts;
Microsoft specifically recommends Store distribution when avoiding that prompt
is the priority.

- [Microsoft Store developer registration][store-account]
- [Windows code-signing options][windows-signing]
- [Artifact Signing pricing][artifact-signing]

[store-account]: https://learn.microsoft.com/windows/apps/publish/partner-center/open-a-developer-account
[windows-signing]: https://learn.microsoft.com/windows/apps/package-and-deploy/code-signing-options
[artifact-signing]: https://learn.microsoft.com/azure/artifact-signing/how-to-change-sku

## Draft beta releases

The `beta-release` GitHub Actions workflow packages all supported platforms and
creates a durable **draft prerelease** when a beta tag is pushed. Tags must:

- use the form `v<project-version>-beta.<number>`, for example
  `v0.1.0-beta.1`;
- match the numeric version in `src/CMakeLists.txt`; and
- point to a commit already merged into `master`.

Before creating a tag, run the workflow manually from its Actions page. A
manual run performs the complete build, test, and packaging matrix but does not
create a tag or release.

```bash
git switch master
git pull --ff-only
git tag -a v0.1.0-beta.1 -m "Harbor v0.1.0 beta 1"
git push origin v0.1.0-beta.1
```

The resulting GitHub release remains a draft. Its CI-generated macOS DMG is
ad-hoc signed and is an internal placeholder. Before publishing the draft:

1. build a Developer ID signed and notarized DMG from the tagged commit;
2. replace the placeholder DMG;
3. regenerate and replace `SHA256SUMS.txt`;
4. complete the checklist in `docs/BETA_TESTING.md`; and
5. replace the draft notes with user-facing changes and known issues.

The Windows installer remains unsigned for the private beta and should be
distributed with the SmartScreen instructions in the tester guide.
