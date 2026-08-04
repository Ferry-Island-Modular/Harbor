# Third-party notices

Harbor is released under the MIT License (see `LICENSE`). It builds on the
components below, each under its own terms.

## Qt

Harbor uses the Qt framework (Core, Widgets, Gui) under the **GNU Lesser
General Public License version 3**.

Qt is not modified. The distributed packages link against Qt dynamically and
ship it as separate shared libraries, so a recipient may replace the Qt
libraries with their own compatible build:

- **macOS** — `Harbor.app/Contents/Frameworks/`
- **Windows** — the `Qt6*.dll` files and plug-in folders beside `Harbor.exe`
- **Linux** — inside the AppImage, under `usr/lib/` and `usr/plugins/`
  (extract with `./Harbor-*.AppImage --appimage-extract`)

The LGPL v3 text is at <https://www.gnu.org/licenses/lgpl-3.0.html>, and Qt's
own licensing overview is at <https://doc.qt.io/qt-6/licensing.html>.

## Bundled in the application

| Component | Version | License |
|---|---|---|
| [miniaudio](https://github.com/mackron/miniaudio) | 0.11.22 | Public domain or MIT-0 |
| [dr_wav](https://github.com/mackron/dr_libs) | wav-0.14.5 | Public domain or MIT-0 |
| [PFFFT](https://bitbucket.org/jpommier/pffft) | 09796885cd5b | BSD-style (FFTPACK derived) |
| [libsamplerate](https://github.com/libsndfile/libsamplerate) | 0.2.2 | BSD-2-Clause |
| [Inter](https://github.com/rsms/inter) | variable | SIL Open Font License 1.1 |
| Four Seas engine | submodule | See the [Four-Seas repository](https://github.com/Ferry-Island-Modular/Four-Seas) |

The Inter license text ships with the source at
`src/resources/fonts/LICENSE-Inter.txt`.

## Build and test only

Not distributed as part of Harbor:

| Component | Version | License |
|---|---|---|
| [Catch2](https://github.com/catchorg/Catch2) | v3.5.4 | Boost Software License 1.0 |
| [dart-sass](https://sass-lang.com/dart-sass) | invoked via `npx` | MIT |

## Notes

Vendored library versions are tracked in `src/third_party/README.md`; keep the
two in step when updating a dependency.
