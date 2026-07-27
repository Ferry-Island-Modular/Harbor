#!/usr/bin/env python3
"""Measure pitch-dependent aliasing risk in Four Seas wavetable banks.

The Four Seas oscillator plays 2048-sample single-cycle tables at 48 kHz with
linear interpolation and no pitch-dependent bandlimiting. For each table and
pitch, this tool splits interpolation-weighted harmonic power at Nyquist:

    alias ratio = power above Nyquist / power at or below Nyquist

The result is reported in dB. This measures deterministic harmonic foldback,
not whether the result is aesthetically objectionable.

Run with the Python environment from Four-Seas/resources/generators:

    uv run python /path/to/analyze_wavetable_aliasing.py \
        --analysis-root /path/to/Four-Seas/resources/generators \
        --variant legacy=/path/to/legacy/banks \
        --variant candidate=/path/to/candidate/banks \
        --output /path/to/alias-analysis.json \
        --preview-output /path/to/alias-previews
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.io import wavfile

SAMPLE_RATE = 48_000
TABLE_SIZE = 2048
DB_FLOOR = -300.0
DB_CEILING = 300.0
DEFAULT_MIDI_NOTES = (36, 48, 60, 72, 84, 96, 108)
THRESHOLDS_DB = (-40.0, -30.0, -20.0)


@dataclass(frozen=True)
class CellResult:
    variant: str
    bank: str
    x: int
    y: int
    z: int
    midi_note: int
    frequency_hz: float
    alias_db: float


def parse_variant(value: str) -> tuple[str, Path]:
    label, separator, path = value.partition("=")
    if not separator or not label or not path:
        raise argparse.ArgumentTypeError("variants must use LABEL=PATH")
    resolved = Path(path).resolve()
    if not resolved.is_dir():
        raise argparse.ArgumentTypeError(f"variant bank root does not exist: {resolved}")
    return label, resolved


def midi_to_frequency(note: int) -> float:
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def normalize_wave(wave: np.ndarray) -> np.ndarray:
    normalized = wave.astype(np.float64)
    if np.issubdtype(wave.dtype, np.integer) or np.max(np.abs(normalized), initial=0.0) > 1.0:
        normalized /= 32768.0
    return normalized


def interpolation_weighted_power(wave: np.ndarray) -> np.ndarray:
    """Return power per table harmonic after linear-interpolation attenuation."""
    normalized = normalize_wave(wave)
    spectrum = np.fft.rfft(normalized)
    harmonics = np.arange(len(spectrum), dtype=np.float64)

    # Periodic linear interpolation is convolution with a triangular kernel.
    # Its amplitude response is sinc², hence sinc⁴ in the power domain.
    interpolation_power = np.sinc(harmonics / len(normalized)) ** 4
    power = np.abs(spectrum) ** 2 * interpolation_power
    power[0] = 0.0
    return power


def alias_ratio_db_from_power(
    power: np.ndarray,
    frequency_hz: float,
    sample_rate: int = SAMPLE_RATE,
) -> float:
    highest_valid_harmonic = int((sample_rate * 0.5) // frequency_hz)
    split = min(highest_valid_harmonic + 1, len(power))
    valid_power = float(np.sum(power[1:split]))
    alias_power = float(np.sum(power[split:]))

    if alias_power <= 1.0e-30:
        return DB_FLOOR
    if valid_power <= 1.0e-30:
        return DB_CEILING
    return float(np.clip(10.0 * np.log10(alias_power / valid_power), DB_FLOOR, DB_CEILING))


def alias_ratio_db(
    wave: np.ndarray,
    frequency_hz: float,
    sample_rate: int = SAMPLE_RATE,
) -> float:
    return alias_ratio_db_from_power(
        interpolation_weighted_power(wave), frequency_hz, sample_rate
    )


def bandlimit_wave(
    wave: np.ndarray,
    frequency_hz: float,
    sample_rate: int = SAMPLE_RATE,
) -> np.ndarray:
    normalized = normalize_wave(wave)
    spectrum = np.fft.rfft(normalized)
    highest_valid_harmonic = int((sample_rate * 0.5) // frequency_hz)
    if highest_valid_harmonic + 1 < len(spectrum):
        spectrum[highest_valid_harmonic + 1 :] = 0.0
    return np.fft.irfft(spectrum, n=len(normalized))


def render_fourseas_wave(
    wave: np.ndarray,
    frequency_hz: float,
    duration: float = 2.0,
    sample_rate: int = SAMPLE_RATE,
) -> np.ndarray:
    """Render one fixed table with Four Seas' phase direction and linear lookup."""
    normalized = normalize_wave(wave)
    sample_count = int(round(duration * sample_rate))
    increment = frequency_hz / sample_rate

    # WavetableOscillator advances downward, wraps, then indexes at phase*N-1.
    phase = np.mod(-increment * np.arange(1, sample_count + 1, dtype=np.float64), 1.0)
    table_position = phase * len(normalized) - 1.0
    integral = np.floor(table_position).astype(np.int64)
    fractional = table_position - integral
    index_a = np.mod(integral, len(normalized))
    index_b = np.mod(integral + 1, len(normalized))
    return normalized[index_a] + (normalized[index_b] - normalized[index_a]) * fractional


def summarize(values: np.ndarray) -> dict:
    summary = {
        "cells": int(len(values)),
        "median_db": float(np.median(values)),
        "p95_db": float(np.percentile(values, 95.0)),
        "worst_db": float(np.max(values)),
    }
    for threshold in THRESHOLDS_DB:
        key = f"at_or_above_{abs(int(threshold))}db_percent"
        summary[key] = float(np.mean(values >= threshold) * 100.0)
    return summary


def compare_variants(
    all_results: list[CellResult],
    baseline_variant: str,
    test_variant: str,
    midi_notes: tuple[int, ...],
) -> dict:
    def key(result: CellResult) -> tuple:
        return (result.bank, result.x, result.y, result.z, result.midi_note)

    baseline = {
        key(result): result.alias_db
        for result in all_results
        if result.variant == baseline_variant
    }
    test = {
        key(result): result.alias_db for result in all_results if result.variant == test_variant
    }
    shared = sorted(baseline.keys() & test.keys())
    pitches = {}
    for note in midi_notes:
        differences = np.asarray(
            [test[item] - baseline[item] for item in shared if item[-1] == note]
        )
        pitches[str(note)] = {
            "frequency_hz": midi_to_frequency(note),
            "paired_cells": int(len(differences)),
            "median_delta_db": float(np.median(differences)),
            "p95_delta_db": float(np.percentile(differences, 95.0)),
            "test_at_least_3db_worse_percent": float(np.mean(differences >= 3.0) * 100.0),
            "test_at_least_10db_worse_percent": float(np.mean(differences >= 10.0) * 100.0),
            "test_at_least_3db_better_percent": float(np.mean(differences <= -3.0) * 100.0),
        }
    return {
        "baseline": baseline_variant,
        "test": test_variant,
        "pitches": pitches,
    }


def analyze_variant(
    label: str,
    bank_root: Path,
    midi_notes: tuple[int, ...],
    WavetableBank,
) -> tuple[dict, list[CellResult]]:
    bank_results = []
    all_results: list[CellResult] = []
    aggregate_by_note = {note: [] for note in midi_notes}

    bank_dirs = sorted(path for path in bank_root.iterdir() if path.is_dir())
    print(f"Analyzing {label}: {len(bank_dirs)} banks", flush=True)
    for bank_dir in bank_dirs:
        bank = WavetableBank.load(bank_dir, samples_per_wave=TABLE_SIZE)
        per_note = {note: [] for note in midi_notes}
        for z in range(bank.num_pages):
            for y in range(bank.grid_size):
                for x in range(bank.grid_size):
                    power = interpolation_weighted_power(bank.get_wave(x, y, z))
                    for note in midi_notes:
                        frequency = midi_to_frequency(note)
                        result = CellResult(
                            variant=label,
                            bank=bank_dir.name,
                            x=x,
                            y=y,
                            z=z,
                            midi_note=note,
                            frequency_hz=frequency,
                            alias_db=alias_ratio_db_from_power(power, frequency),
                        )
                        all_results.append(result)
                        per_note[note].append(result.alias_db)
                        aggregate_by_note[note].append(result.alias_db)

        bank_results.append(
            {
                "bank": bank_dir.name,
                "pitches": {
                    str(note): {
                        "frequency_hz": midi_to_frequency(note),
                        **summarize(np.asarray(per_note[note])),
                    }
                    for note in midi_notes
                },
            }
        )

    return (
        {
            "variant": label,
            "banks": len(bank_dirs),
            "cells_per_pitch": len(aggregate_by_note[midi_notes[0]]),
            "pitches": {
                str(note): {
                    "frequency_hz": midi_to_frequency(note),
                    **summarize(np.asarray(aggregate_by_note[note])),
                }
                for note in midi_notes
            },
            "bank_results": bank_results,
        },
        all_results,
    )


def write_pcm16(path: Path, samples: np.ndarray, scale: float) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pcm = np.clip(samples * scale, -1.0, 1.0)
    wavfile.write(path, SAMPLE_RATE, np.round(pcm * 32767.0).astype(np.int16))


def select_worst(results: list[CellResult], variant: str, count: int) -> list[CellResult]:
    candidates = sorted(
        (result for result in results if result.variant == variant),
        key=lambda result: result.alias_db,
        reverse=True,
    )
    selected = []
    seen_banks = set()
    seen_notes = set()

    # Include the worst case at each tested pitch so the listening set shows
    # how foldback develops across the register instead of collapsing to C8.
    for result in candidates:
        if result.midi_note in seen_notes:
            continue
        selected.append(result)
        seen_notes.add(result.midi_note)
        seen_banks.add(result.bank)
        if len(selected) == count:
            return selected

    # Fill any remaining slots with globally worst cases from new banks.
    for result in candidates:
        if result.bank in seen_banks:
            continue
        selected.append(result)
        seen_banks.add(result.bank)
        if len(selected) == count:
            break
    return selected


def render_worst_previews(
    selected: list[CellResult],
    variants: dict[str, Path],
    comparison_variant: str,
    output_root: Path,
    WavetableBank,
) -> list[dict]:
    loaded_banks = {}
    manifest = []
    for rank, result in enumerate(selected, start=1):
        key = (result.variant, result.bank)
        if key not in loaded_banks:
            loaded_banks[key] = WavetableBank.load(
                variants[result.variant] / result.bank, samples_per_wave=TABLE_SIZE
            )
        comparison_key = (comparison_variant, result.bank)
        if comparison_key not in loaded_banks:
            loaded_banks[comparison_key] = WavetableBank.load(
                variants[comparison_variant] / result.bank, samples_per_wave=TABLE_SIZE
            )

        source_wave = loaded_banks[key].get_wave(result.x, result.y, result.z)
        comparison_wave = loaded_banks[comparison_key].get_wave(result.x, result.y, result.z)
        reference_wave = bandlimit_wave(source_wave, result.frequency_hz)

        source_audio = render_fourseas_wave(source_wave, result.frequency_hz)
        reference_audio = render_fourseas_wave(reference_wave, result.frequency_hz)
        comparison_audio = render_fourseas_wave(comparison_wave, result.frequency_hz)
        isolated_alias = source_audio - reference_audio
        reference_rms = float(np.sqrt(np.mean(reference_audio**2)))
        alias_rms = float(np.sqrt(np.mean(isolated_alias**2)))
        if alias_rms <= 1.0e-15:
            rendered_alias_db = DB_FLOOR
        elif reference_rms <= 1.0e-15:
            rendered_alias_db = DB_CEILING
        else:
            rendered_alias_db = float(
                np.clip(20.0 * math.log10(alias_rms / reference_rms), DB_FLOOR, DB_CEILING)
            )

        stem = (
            f"{rank:02d}_{result.bank}_x{result.x}_y{result.y}_z{result.z}"
            f"_midi{result.midi_note}"
        )
        shared_peak = max(
            float(np.max(np.abs(source_audio))),
            float(np.max(np.abs(reference_audio))),
            float(np.max(np.abs(comparison_audio))),
            1.0e-12,
        )
        shared_scale = 0.9 / shared_peak
        alias_peak = max(float(np.max(np.abs(isolated_alias))), 1.0e-12)

        source_path = output_root / f"{stem}_{result.variant}_raw.wav"
        reference_path = output_root / f"{stem}_{result.variant}_bandlimited.wav"
        comparison_path = output_root / f"{stem}_{comparison_variant}_raw.wav"
        alias_path = output_root / f"{stem}_{result.variant}_alias_isolated.wav"
        write_pcm16(source_path, source_audio, shared_scale)
        write_pcm16(reference_path, reference_audio, shared_scale)
        write_pcm16(comparison_path, comparison_audio, shared_scale)
        write_pcm16(alias_path, isolated_alias, 0.9 / alias_peak)

        manifest.append(
            {
                **result.__dict__,
                "rendered_alias_db": rendered_alias_db,
                "raw": source_path.name,
                "bandlimited_reference": reference_path.name,
                "comparison_raw": comparison_path.name,
                "normalized_isolated_alias": alias_path.name,
            }
        )

    (output_root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--analysis-root", type=Path, required=True)
    parser.add_argument("--variant", type=parse_variant, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preview-output", type=Path)
    parser.add_argument("--preview-variant", default="candidate")
    parser.add_argument("--comparison-variant", default="legacy")
    parser.add_argument("--preview-count", type=int, default=8)
    parser.add_argument(
        "--midi-notes",
        default=",".join(str(note) for note in DEFAULT_MIDI_NOTES),
        help="Comma-separated MIDI notes (default: C2 through C8 by octaves)",
    )
    args = parser.parse_args()

    midi_notes = tuple(int(value) for value in args.midi_notes.split(","))
    if not midi_notes or any(note < 0 or note > 127 for note in midi_notes):
        parser.error("--midi-notes must contain MIDI note numbers from 0 through 127")
    variants = dict(args.variant)
    if len(variants) != len(args.variant):
        parser.error("variant labels must be unique")

    analysis_root = args.analysis_root.resolve()
    sys.path.insert(0, str(analysis_root))
    from analysis.loader import WavetableBank

    reports = []
    all_results = []
    for label, bank_root in args.variant:
        report, results = analyze_variant(label, bank_root, midi_notes, WavetableBank)
        reports.append(report)
        all_results.extend(results)

    output = {
        "method": {
            "sample_rate": SAMPLE_RATE,
            "table_size": TABLE_SIZE,
            "metric": "10*log10(interpolation-weighted power above Nyquist / power at or below Nyquist)",
            "thresholds_db": THRESHOLDS_DB,
            "midi_notes": midi_notes,
        },
        "variants": reports,
    }
    if args.comparison_variant in variants and args.preview_variant in variants:
        output["comparison"] = compare_variants(
            all_results,
            args.comparison_variant,
            args.preview_variant,
            midi_notes,
        )

    if args.preview_output:
        if args.preview_variant not in variants:
            parser.error(f"unknown --preview-variant: {args.preview_variant}")
        if args.comparison_variant not in variants:
            parser.error(f"unknown --comparison-variant: {args.comparison_variant}")
        selected = select_worst(all_results, args.preview_variant, args.preview_count)
        output["worst_previews"] = render_worst_previews(
            selected,
            variants,
            args.comparison_variant,
            args.preview_output.resolve(),
            WavetableBank,
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(f"Alias analysis written to {args.output}")
    if args.preview_output:
        print(f"Worst-case previews written to {args.preview_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
