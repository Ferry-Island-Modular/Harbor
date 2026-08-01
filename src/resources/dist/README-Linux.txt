HARBOR BETA FOR LINUX
=====================

Harbor is a desktop tool for generating wavetable banks for Ferry Island
Modular hardware and other wavetable synth hosts.


REQUIRED SYSTEM PACKAGES
------------------------

AppImages deliberately do not bundle OpenGL libraries, because those have to
match the graphics driver already installed on your machine. A stock Ubuntu
desktop does not always ship them, so install these first:

    sudo apt install libopengl0 libglx0 libegl1

Without them Harbor exits immediately with:

    error while loading shared libraries: libOpenGL.so.0

The equivalent packages on Fedora are mesa-libGL and mesa-libEGL, and on Arch
they are part of libglvnd.


RUNNING
-------

1. Make the AppImage executable:

       chmod +x Harbor-*.AppImage

2. Run it:

       ./Harbor-*.AppImage

If your distribution has no FUSE 2 runtime, either install it (libfuse2t64 on
Ubuntu 24.04) or extract and run the AppImage directly:

    ./Harbor-*.AppImage --appimage-extract
    ./squashfs-root/AppRun


WAYLAND AND X11
---------------

Harbor runs on both. To force one or the other:

    QT_QPA_PLATFORM=wayland ./Harbor-*.AppImage
    QT_QPA_PLATFORM=xcb ./Harbor-*.AppImage

Under XWayland on a HiDPI display, scaling is controlled by the usual Qt
environment variables, for example QT_SCALE_FACTOR=1.


REPORTING BUGS
--------------

Please file issues on the project's GitHub issue tracker. Including your
distribution, desktop environment, and the output of `echo $XDG_SESSION_TYPE`
run from a terminal in your desktop session is very helpful.
