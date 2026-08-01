HARBOR BETA FOR WINDOWS
=======================

Harbor is a desktop tool for generating wavetable banks for Ferry Island
Modular hardware and other wavetable synth hosts.


INSTALLER
---------

Run the Harbor setup executable. This beta is not code-signed, so Microsoft
Defender SmartScreen may show "Windows protected your PC":

1. Confirm that the installer came directly from Ferry Island Modular.
2. Click "More info".
3. Confirm the publisher is shown as "Unknown publisher".
4. Click "Run anyway".

You can then choose where Harbor is installed:

  Anyone who uses this computer   Installs to Program Files for all users.
                                  Requires administrator rights.

  Only for me                     Installs to %LocalAppData%\Programs\Harbor.
                                  No administrator rights needed.

If you are signed in as an administrator, Windows shows a User Account Control
prompt when the installer starts, before you pick a mode. That is expected.

The installer creates Start Menu and optional Desktop shortcuts. Remove Harbor
later through Windows Settings > Apps > Installed apps.


PORTABLE ZIP
------------

Extract the entire ZIP to a normal folder before running Harbor.exe. Do not run
Harbor directly from inside the ZIP, and do not move Harbor.exe away from its
DLL and plugin folders.

The Microsoft Visual C++ runtime ships alongside Harbor.exe, so no separate
redistributable needs installing.


REPORTING BUGS
--------------

Please file issues on the project's GitHub issue tracker.
