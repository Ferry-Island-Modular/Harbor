#pragma once

#include <QString>
#include <filesystem>

namespace fim::app {

// Atomically publish a fully generated staging directory. If destination
// already exists it is moved aside first and restored if publication fails.
// Staging and destination must be siblings on the same filesystem.
bool PublishStagedBank(const std::filesystem::path& staging,
                       const std::filesystem::path& destination);

// Copies an 8-page wavetable bank from a source directory to a destination
// directory, optionally downsampling each cycle from 2048 samples to
// target_samples_per_cycle (e.g. 256 for Waveedit-target banks).
//
// The source directory is expected to contain 1.wav .. 8.wav, each of
// which is a sequence of 64 single-cycle waveforms concatenated. Each
// cycle is 2048 samples; the function decimates per-cycle so the total
// frame count of each output file is `64 * target_samples_per_cycle`.
//
// If target_samples_per_cycle == 2048, the source files are copied
// verbatim (no resample, no quality loss).
//
// Resampling uses libsamplerate's SRC_SINC_MEDIUM_QUALITY converter via
// fim::dsp::ResampleTo.
//
// Returns false on any I/O or DSP error. Creates the destination directory
// if it does not exist.
bool WriteBankToExportDir(const QString& source_dir, const QString& dest_dir,
                          int target_samples_per_cycle);

}  // namespace fim::app
