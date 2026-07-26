#!/usr/bin/env python3
"""Compare generated Four Seas wavetable evaluation sets.

This is a thin aggregation layer over the analysis tools in the Four-Seas
resources repository. Run it with that project's uv environment, for example:

    uv run python /path/to/analyze-wavetable-evaluations.py \
        --analysis-root /path/to/Four-Seas/resources/generators \
        --variant legacy=/path/to/legacy/banks \
        --variant candidate=/path/to/candidate/banks \
        --output comparison.json
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np


def parse_variant(value: str) -> tuple[str, Path]:
    label, separator, path = value.partition("=")
    if not separator or not label or not path:
        raise argparse.ArgumentTypeError("variants must use LABEL=PATH")
    return label, Path(path).resolve()


def high_harmonic_energy(wave: np.ndarray, first_rejected_bin: int) -> float:
    normalized = wave.astype(np.float64)
    peak = float(np.max(np.abs(normalized)))
    if peak > 1.0:
        normalized /= 32767.0
    power = np.abs(np.fft.rfft(normalized)) ** 2
    power[0] = 0.0
    total = float(np.sum(power))
    if total <= 1.0e-20 or first_rejected_bin >= len(power):
        return 0.0
    return float(np.sum(power[first_rejected_bin:]) / total)


def summarize_bank(bank_dir: Path, modules: dict) -> dict:
    WavetableBank = modules["WavetableBank"]
    audit_bank = modules["audit_bank"]
    compute_features = modules["compute_features"]

    bank = WavetableBank.load(bank_dir, samples_per_wave=2048)
    report = audit_bank(bank_dir, samples_per_wave=2048)
    spectral = defaultdict(list)
    energy_above_harmonic_50 = []
    energy_above_harmonic_400 = []

    for z in range(bank.num_pages):
        for y in range(bank.grid_size):
            for x in range(bank.grid_size):
                wave = bank.get_wave(x, y, z)
                # A sample rate equal to cycle length expresses frequency
                # features in harmonic-bin units rather than arbitrary Hz.
                features = compute_features(wave, sample_rate=len(wave))
                spectral["centroid_harmonic"].append(features.spectral_centroid)
                spectral["rolloff_harmonic"].append(features.spectral_rolloff)
                spectral["flatness"].append(features.spectral_flatness)
                spectral["zero_crossing_rate"].append(features.zero_crossing_rate)
                spectral["crest_factor"].append(features.crest_factor)
                energy_above_harmonic_50.append(high_harmonic_energy(wave, 51))
                energy_above_harmonic_400.append(high_harmonic_energy(wave, 401))

    step_counts = defaultdict(int)
    step_axis_counts = defaultdict(int)
    for issue in report.step_issues:
        step_counts[issue.metric] += 1
        step_axis_counts[f"{issue.axis}_{issue.metric}"] += 1

    regularity = {}
    if report.regularity is not None:
        for axis, values in report.regularity.axis_maps.items():
            regularity[f"{axis}_mean"] = float(np.mean(values))
            regularity[f"{axis}_max"] = float(np.max(values))

    return {
        "bank": bank_dir.name,
        "waves": bank.grid_size * bank.grid_size * bank.num_pages,
        **{name: float(np.mean(values)) for name, values in spectral.items()},
        "energy_above_harmonic_50": float(np.mean(energy_above_harmonic_50)),
        "energy_above_harmonic_400": float(np.mean(energy_above_harmonic_400)),
        "silent_segments": len(report.silent_segments),
        "dead_segments": len(report.dead_segments),
        "wrap_failures": len(report.wrap_failures),
        **{f"step_{name}": count for name, count in step_counts.items()},
        **{f"step_{name}": count for name, count in step_axis_counts.items()},
        **{f"regularity_{name}": value for name, value in regularity.items()},
    }


def aggregate_banks(label: str, bank_results: list[dict]) -> dict:
    numeric_keys = sorted(
        {
            key
            for bank in bank_results
            for key, value in bank.items()
            if key not in {"bank", "waves"} and isinstance(value, (int, float))
        }
    )
    aggregate = {"variant": label, "banks": len(bank_results)}
    for key in numeric_keys:
        values = [float(bank.get(key, 0.0)) for bank in bank_results]
        if key.endswith(("segments", "failures")) or key.startswith("step_"):
            aggregate[f"{key}_total"] = int(sum(values))
        else:
            aggregate[f"{key}_mean"] = float(np.mean(values))
    return aggregate


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--analysis-root", type=Path, required=True)
    parser.add_argument("--variant", type=parse_variant, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    analysis_root = args.analysis_root.resolve()
    sys.path.insert(0, str(analysis_root))
    from analysis.loader import WavetableBank
    from analysis.spectral import compute_features
    from audit import audit_bank

    modules = {
        "WavetableBank": WavetableBank,
        "compute_features": compute_features,
        "audit_bank": audit_bank,
    }
    result = {"variants": [], "banks": {}}
    for label, bank_root in args.variant:
        bank_dirs = sorted(path for path in bank_root.iterdir() if path.is_dir())
        print(f"Analyzing {label}: {len(bank_dirs)} banks", flush=True)
        bank_results = [summarize_bank(path, modules) for path in bank_dirs]
        result["variants"].append(aggregate_banks(label, bank_results))
        result["banks"][label] = bank_results

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Analysis written to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
