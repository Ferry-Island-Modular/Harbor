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
