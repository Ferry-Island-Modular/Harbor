// Single translation unit that instantiates the implementation portions of
// the vendored single-header libraries. Each #define MUST appear exactly
// once in the entire program; this file is the only place they live.

// Match Qt's COM apartment on Windows.
//
// MA_COINIT_VALUE defaults to COINIT_MULTITHREADED, but Qt initialises the GUI
// thread as an STA (it needs OLE for drag-and-drop). miniaudio's WASAPI
// enumeration helper does this, on whichever thread calls it:
//
//     ma_CoInitializeEx(pContext, NULL, MA_COINIT_VALUE);
//     ... CoCreateInstance(MMDeviceEnumerator) ...
//     ma_CoUninitialize(pContext);            // miniaudio.h:21609, unguarded
//
// Requesting MTA on an STA thread returns RPC_E_CHANGED_MODE, which does NOT
// increment the COM reference count — but that CoUninitialize decrements it
// regardless. (The other two call sites, miniaudio.h:41336 and :41361, guard
// on CoInitializeResult == S_OK. This one does not.) Each call therefore eats
// one of Qt's OLE references, and when the count reaches zero the GUI thread's
// apartment is torn down: drag-and-drop silently stops working, native file
// dialogs quietly degrade to Qt's own, and COM calls eventually hang outright.
//
// Asking for APARTMENTTHREADED instead means the call matches the existing
// apartment and returns S_OK, so the reference count is incremented and the
// unguarded CoUninitialize is balanced. No effect on non-Windows builds.
#define MA_COINIT_VALUE 2  // COINIT_APARTMENTTHREADED

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
