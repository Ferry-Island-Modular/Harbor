import numpy as np

from analyze_wavetable_aliasing import (
    DB_FLOOR,
    CellResult,
    alias_ratio_db,
    bandlimit_wave,
    compare_variants,
    render_fourseas_wave,
    select_worst,
)


def harmonic_wave(harmonics: dict[int, float], size: int = 2048) -> np.ndarray:
    phase = 2.0 * np.pi * np.arange(size) / size
    return sum(amplitude * np.sin(phase * harmonic) for harmonic, amplitude in harmonics.items())


def test_sine_has_no_table_harmonics_to_alias() -> None:
    wave = harmonic_wave({1: 1.0})

    assert alias_ratio_db(wave, 4_000.0) == DB_FLOOR


def test_known_out_of_band_harmonic_has_expected_ratio() -> None:
    wave = harmonic_wave({1: 1.0, 100: 0.5})

    # At 1 kHz, only harmonics 1-24 are valid. H100 therefore aliases.
    measured = alias_ratio_db(wave, 1_000.0)
    assert -6.2 < measured < -5.9


def test_bandlimited_reference_removes_detected_alias_power() -> None:
    wave = harmonic_wave({1: 1.0, 20: 0.4, 100: 0.5})
    reference = bandlimit_wave(wave, 1_000.0)

    assert alias_ratio_db(reference, 1_000.0) == DB_FLOOR


def test_fourseas_renderer_preserves_length_and_finite_samples() -> None:
    wave = harmonic_wave({1: 1.0, 7: 0.25})

    rendered = render_fourseas_wave(wave, 440.0, duration=0.1)

    assert len(rendered) == 4_800
    assert np.all(np.isfinite(rendered))


def test_worst_preview_selection_covers_each_pitch_before_filling() -> None:
    results = [
        CellResult("candidate", f"bank_{note}", 0, 0, 0, note, 440.0, float(note))
        for note in (36, 48, 60)
    ]
    results.append(CellResult("candidate", "extra", 0, 0, 0, 60, 440.0, 100.0))

    selected = select_worst(results, "candidate", 3)

    assert {result.midi_note for result in selected} == {36, 48, 60}


def test_paired_comparison_reports_candidate_delta() -> None:
    results = [
        CellResult("legacy", "bank", 0, 0, 0, 60, 261.6, -30.0),
        CellResult("candidate", "bank", 0, 0, 0, 60, 261.6, -20.0),
    ]

    comparison = compare_variants(results, "legacy", "candidate", (60,))

    assert comparison["pitches"]["60"]["median_delta_db"] == 10.0
    assert comparison["pitches"]["60"]["test_at_least_10db_worse_percent"] == 100.0
