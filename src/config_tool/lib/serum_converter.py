"""
Serum Wavetable to FourSeas Converter

Converts a single Serum-format wavetable into FourSeas' 3D wavetable format.
- X axis: Source wavetable frames interpolated to 8 positions
- Y axis: Configurable spectral morph
- Z axis: Configurable spectral morph

Based on Vital's spectral morph implementations.
"""

from enum import Enum

import numpy as np
import scipy.io.wavfile as wavfile
from scipy import interpolate
from scipy.signal import resample_poly

from config_tool.lib.baseclass import WavetableGeneratorBaseClass


class MorphType(Enum):
    """Available spectral morph types."""
    FORMANT_SCALE = "formant_scale"
    PHASE_DISPERSE = "phase_disperse"
    SMEAR = "smear"
    HARMONIC_STRETCH = "harmonic_stretch"


# Human-readable labels for UI
MORPH_TYPE_LABELS = {
    MorphType.FORMANT_SCALE: "Formant",
    MorphType.PHASE_DISPERSE: "Phase",
    MorphType.SMEAR: "Smear",
    MorphType.HARMONIC_STRETCH: "Stretch",
}


class SerumWavetableConverter(WavetableGeneratorBaseClass):
    """
    Converts a Serum wavetable to FourSeas 3D format with spectral morphing.

    Args:
        num_waves: Number of wavetables per page (fixed at 64 for FourSeas)
        samples: Number of samples per output wavetable (e.g., 2048 for Four Seas, 256 for Piston Honda)
        save_path: Output directory for generated wavetables
        oversample_factor: Oversampling factor for anti-aliasing
        name: Name for the output wavetable set
    """

    # Serum wavetables are always 2048 samples per frame
    SERUM_FRAME_SIZE = 2048

    # Morph parameters (based on Vital's constants)
    PHASE_DISPERSE_CENTER = 24.0  # Center harmonic for phase disperse
    PHASE_DISPERSE_SCALE = 0.05  # Scale factor for phase effect
    MAX_FORMANT_SHIFT = 4.0  # Maximum harmonic scale factor
    MAX_HARMONIC_STRETCH = 12.0  # Maximum inharmonic stretch factor
    FREQUENCY_BINS = 10  # Log2 frequency bins for stretch calculation

    def __init__(
        self,
        num_waves=64,
        samples=2048,
        save_path="output_waves",
        oversample_factor=4,
        name="serum_converted",
    ):
        self.name = name
        self.output_samples = samples  # Target output size

        super().__init__(
            num_waves=num_waves,
            samples=samples,
            save_path=save_path,
            oversample_factor=oversample_factor,
        )

        # Storage for the source wavetable frames (8 frames after resampling)
        self.source_frames = None
        # Cached FFT data for each frame
        self.frame_fft_data = None

        # Configurable morph types for Y and Z axes
        self.y_morph_type = MorphType.FORMANT_SCALE
        self.z_morph_type = MorphType.PHASE_DISPERSE

    def set_y_morph(self, morph_type: MorphType):
        """Set the morph type for the Y axis."""
        self.y_morph_type = morph_type

    def set_z_morph(self, morph_type: MorphType):
        """Set the morph type for the Z axis."""
        self.z_morph_type = morph_type

    def load_serum_wavetable(self, filepath):
        """
        Load a Serum wavetable file and extract individual frames.

        Serum wavetables are stored as stacked single-cycle waveforms in a WAV file.
        Each frame is exactly 2048 samples long.

        Args:
            filepath: Path to the Serum .wav file

        Returns:
            numpy.ndarray: Array of frames with shape (num_frames, 2048)
        """
        sample_rate, data = wavfile.read(filepath)

        # Convert to float and normalize to [-1, 1]
        if data.dtype == np.int16:
            data = data.astype(np.float32) / 32768.0
        elif data.dtype == np.int32:
            data = data.astype(np.float32) / 2147483648.0
        else:
            data = data.astype(np.float32)

        # Handle stereo files by taking first channel
        if len(data.shape) > 1:
            data = data[:, 0]

        # Serum wavetables use 2048 samples per frame
        frame_size = 2048
        num_frames = len(data) // frame_size

        if num_frames == 0:
            raise ValueError(
                f"File {filepath} is too short to contain valid wavetable data"
            )

        # Extract frames
        frames = np.zeros((num_frames, frame_size))
        for i in range(num_frames):
            start_idx = i * frame_size
            end_idx = start_idx + frame_size
            frames[i] = data[start_idx:end_idx]
            # Remove DC offset from each frame
            frames[i] = frames[i] - np.mean(frames[i])

        print(f"Loaded {num_frames} frames from {filepath}")
        return frames

    def resample_frames_to_8(self, frames):
        """
        Intelligently resample arbitrary number of frames to exactly 8 frames.

        Uses linear interpolation in the "frame domain" to smoothly transition
        between frames. Each resampled frame maintains 2048 samples.

        Args:
            frames: numpy.ndarray of shape (num_frames, 2048)

        Returns:
            numpy.ndarray: Resampled frames of shape (8, 2048)
        """
        num_input_frames = frames.shape[0]

        if num_input_frames == 8:
            return frames.copy()

        # Create interpolation indices
        input_indices = np.linspace(0, num_input_frames - 1, num_input_frames)
        output_indices = np.linspace(0, num_input_frames - 1, 8)

        # Interpolate each sample position across frames
        resampled = np.zeros((8, 2048))

        for sample_idx in range(2048):
            sample_values = frames[:, sample_idx]
            interp_func = interpolate.interp1d(
                input_indices, sample_values, kind="linear", assume_sorted=True
            )
            resampled[:, sample_idx] = interp_func(output_indices)

        # Remove any DC offset
        for i in range(8):
            resampled[i] = resampled[i] - np.mean(resampled[i])

        return resampled

    def load_wavetable(self, filepath):
        """
        Load a Serum wavetable and prepare it for 3D morphing.

        Args:
            filepath: Path to the Serum .wav file
        """
        print(f"Loading wavetable from {filepath}...")
        raw_frames = self.load_serum_wavetable(filepath)
        self.source_frames = self.resample_frames_to_8(raw_frames)

        # Pre-compute FFT data for each frame
        self._compute_fft_cache()
        print("Wavetable loaded and FFT cache prepared")

    def _compute_fft_cache(self):
        """
        Pre-compute FFT data (amplitudes and phases) for each source frame.
        """
        num_harmonics = 1024  # 2048 / 2
        self.frame_fft_data = []

        for frame_idx in range(8):
            frame = self.source_frames[frame_idx]

            # Compute FFT
            fft_result = np.fft.rfft(frame)

            # Extract amplitude and phase
            amplitudes = np.abs(fft_result)
            phases = np.angle(fft_result)

            # Normalize phases for later manipulation
            normalized_real = np.cos(phases)
            normalized_imag = np.sin(phases)

            self.frame_fft_data.append({
                'amplitudes': amplitudes,
                'phases': phases,
                'normalized_real': normalized_real,
                'normalized_imag': normalized_imag,
            })

    def formant_scale_morph(self, fft_data, scale):
        """
        Apply formant scale morph to frequency data.

        Shifts harmonics by a scale factor, effectively moving formants
        up (scale > 1) or down (scale < 1).

        Based on Vital's harmonicScaleMorph.

        Args:
            fft_data: Dict with 'amplitudes', 'normalized_real', 'normalized_imag'
            scale: Scale factor (1.0 = no change, >1 = shift up, <1 = shift down)

        Returns:
            numpy.ndarray: Modified FFT coefficients (complex)
        """
        amplitudes = fft_data['amplitudes']
        normalized_real = fft_data['normalized_real']
        normalized_imag = fft_data['normalized_imag']
        num_harmonics = len(amplitudes)

        # Output buffer
        new_real = np.zeros(num_harmonics)
        new_imag = np.zeros(num_harmonics)

        # DC component unchanged
        new_real[0] = amplitudes[0] * normalized_real[0]
        new_imag[0] = amplitudes[0] * normalized_imag[0]

        if scale <= 0:
            scale = 0.001  # Avoid division by zero

        # Calculate how many source harmonics to process
        max_harmonics = min(num_harmonics, int((num_harmonics - 1) / scale + 1))

        for i in range(1, max_harmonics):
            # Calculate destination position (where this harmonic moves to)
            shifted_index = max(1.0, (i - 1) * scale + 1)
            dest_index = int(shifted_index)

            if dest_index >= num_harmonics - 1:
                break

            # Interpolation factor for sub-bin positioning
            t = shifted_index - dest_index

            # Source amplitude and normalized components
            amplitude = amplitudes[i]
            real_amount = normalized_real[i]
            imag_amount = normalized_imag[i]

            # Distribute to destination bins with linear interpolation
            amplitude1 = (1.0 - t) * amplitude
            amplitude2 = t * amplitude

            new_real[dest_index] += amplitude1 * real_amount
            new_imag[dest_index] += amplitude1 * imag_amount
            new_real[dest_index + 1] += amplitude2 * real_amount
            new_imag[dest_index + 1] += amplitude2 * imag_amount

        return new_real + 1j * new_imag

    def phase_disperse_morph(self, fft_data, amount):
        """
        Apply phase dispersion morph to frequency data.

        Creates frequency-dependent phase shifts centered around harmonic 24,
        creating a "swirling" or dispersed sound character.

        Based on Vital's phaseMorph.

        Args:
            fft_data: Dict with 'amplitudes', 'normalized_real', 'normalized_imag'
            amount: Morph amount (0.0 = no effect, higher = more dispersion)

        Returns:
            numpy.ndarray: Modified FFT coefficients (complex)
        """
        amplitudes = fft_data['amplitudes']
        num_harmonics = len(amplitudes)

        # Output buffer
        new_coeffs = np.zeros(num_harmonics, dtype=complex)

        # Calculate the offset so the effect centers around harmonic 24
        center = self.PHASE_DISPERSE_CENTER
        offset = -(center - 1.0) ** 2 * amount

        for i in range(num_harmonics):
            amplitude = amplitudes[i]

            # Phase shift based on distance from center harmonic
            # (i - center)^2 creates a parabolic curve
            delta_phase = (i - center) ** 2 * amount + offset

            # Scale and wrap phase
            phase_shift = delta_phase * self.PHASE_DISPERSE_SCALE

            # Apply phase rotation
            cos_phase = np.cos(phase_shift)
            sin_phase = np.sin(phase_shift)

            # Original normalized direction from cached data
            orig_real = fft_data['normalized_real'][i]
            orig_imag = fft_data['normalized_imag'][i]

            # Rotate phase
            new_real = orig_real * cos_phase - orig_imag * sin_phase
            new_imag = orig_real * sin_phase + orig_imag * cos_phase

            new_coeffs[i] = amplitude * (new_real + 1j * new_imag)

        return new_coeffs

    def smear_morph(self, fft_data, amount):
        """
        Apply smear morph to frequency data.

        Averages amplitude across harmonics, creating a smoother, more diffuse
        spectrum. Higher amounts blur the spectral peaks together.

        Based on Vital's smearMorph.

        Args:
            fft_data: Dict with 'amplitudes', 'normalized_real', 'normalized_imag'
            amount: Smear amount (0.0 = no effect, 1.0 = full smear)

        Returns:
            numpy.ndarray: Modified FFT coefficients (complex)
        """
        amplitudes = fft_data['amplitudes']
        normalized_real = fft_data['normalized_real']
        normalized_imag = fft_data['normalized_imag']
        num_harmonics = len(amplitudes)

        # Output buffer
        new_coeffs = np.zeros(num_harmonics, dtype=complex)

        # First harmonic: amplitude scaled by (1 - amount)
        running_amplitude = amplitudes[0] * (1.0 - amount)
        new_coeffs[0] = running_amplitude * (normalized_real[0] + 1j * normalized_imag[0])

        # For remaining harmonics: interpolate between original and running average
        for i in range(1, num_harmonics):
            original_amplitude = amplitudes[i]
            # Interpolate: (1-amount)*original + amount*running
            running_amplitude = (1.0 - amount) * original_amplitude + amount * running_amplitude

            new_coeffs[i] = running_amplitude * (normalized_real[i] + 1j * normalized_imag[i])

            # Scale factor for next iteration (from Vital)
            running_amplitude *= (i + 0.25) / i

        return new_coeffs

    def harmonic_stretch_morph(self, fft_data, amount):
        """
        Apply inharmonic stretch morph to frequency data.

        Creates non-linear stretching of harmonics based on their octave,
        producing metallic or bell-like tones at higher amounts.

        Based on Vital's inharmonicScaleMorph.

        Args:
            fft_data: Dict with 'amplitudes', 'normalized_real', 'normalized_imag'
            amount: Stretch amount (0.0 = no stretch, 1.0 = maximum stretch)

        Returns:
            numpy.ndarray: Modified FFT coefficients (complex)
        """
        amplitudes = fft_data['amplitudes']
        normalized_real = fft_data['normalized_real']
        normalized_imag = fft_data['normalized_imag']
        num_harmonics = len(amplitudes)

        # Calculate the stretch multiplier (1.0 to MAX_HARMONIC_STRETCH)
        mult = 1.0 + amount * (self.MAX_HARMONIC_STRETCH - 1.0)

        # Output buffer
        new_real = np.zeros(num_harmonics)
        new_imag = np.zeros(num_harmonics)

        # DC component unchanged
        new_real[0] = amplitudes[0] * normalized_real[0]
        new_imag[0] = amplitudes[0] * normalized_imag[0]

        # Pre-compute shifted indices for all harmonics
        for i in range(1, num_harmonics):
            # Calculate octave-based shift (non-linear)
            octave = np.log2(i) if i > 0 else 0
            power = octave / (self.FREQUENCY_BINS - 1.0)
            shift = mult ** power
            shifted_index = max(1.0, shift * (i - 1) + 1)

            dest_index = int(shifted_index)
            if dest_index >= num_harmonics - 1:
                continue

            # Interpolation factor
            t = shifted_index - dest_index

            # Source amplitude and normalized components
            amplitude = amplitudes[i]
            real_amount = normalized_real[i]
            imag_amount = normalized_imag[i]

            # Distribute to destination bins
            amplitude1 = (1.0 - t) * amplitude
            amplitude2 = t * amplitude

            new_real[dest_index] += amplitude1 * real_amount
            new_imag[dest_index] += amplitude1 * imag_amount
            new_real[dest_index + 1] += amplitude2 * real_amount
            new_imag[dest_index + 1] += amplitude2 * imag_amount

        return new_real + 1j * new_imag

    def apply_morph(self, fft_data, morph_type: MorphType, amount: float):
        """
        Apply a morph of the specified type to FFT data.

        Args:
            fft_data: Dict with 'amplitudes', 'normalized_real', 'normalized_imag'
            morph_type: Which morph algorithm to use
            amount: Morph amount (0.0-1.0, scaled internally per morph type)

        Returns:
            numpy.ndarray: Modified FFT coefficients (complex)
        """
        if morph_type == MorphType.FORMANT_SCALE:
            # Scale from 1.0 to MAX_FORMANT_SHIFT
            scale = 1.0 + amount * (self.MAX_FORMANT_SHIFT - 1.0)
            return self.formant_scale_morph(fft_data, scale)

        elif morph_type == MorphType.PHASE_DISPERSE:
            return self.phase_disperse_morph(fft_data, amount)

        elif morph_type == MorphType.SMEAR:
            return self.smear_morph(fft_data, amount)

        elif morph_type == MorphType.HARMONIC_STRETCH:
            return self.harmonic_stretch_morph(fft_data, amount)

        else:
            raise ValueError(f"Unknown morph type: {morph_type}")

    def generate_morphed_wave(self, x, y, z):
        """
        Generate a single waveform with spectral morphing applied.

        Args:
            x: X-axis position (0-7) - selects source frame
            y: Y-axis position (0-7) - Y morph amount (configurable via y_morph_type)
            z: Z-axis position (0-7) - Z morph amount (configurable via z_morph_type)

        Returns:
            numpy.ndarray: Morphed waveform (oversampled)
        """
        if self.source_frames is None:
            raise ValueError("Must call load_wavetable() first")

        # Get the source frame's FFT data
        fft_data = self.frame_fft_data[x]

        # Calculate morph amounts from axis positions (0.0 to 1.0)
        y_amount = y / 7.0
        z_amount = z / 7.0

        # Apply Y-axis morph
        morphed_fft = self.apply_morph(fft_data, self.y_morph_type, y_amount)

        # Create new fft_data dict from morphed result for Z-axis morph
        morphed_amplitudes = np.abs(morphed_fft)
        morphed_phases = np.angle(morphed_fft)
        morphed_fft_data = {
            'amplitudes': morphed_amplitudes,
            'phases': morphed_phases,
            'normalized_real': np.cos(morphed_phases),
            'normalized_imag': np.sin(morphed_phases),
        }

        # Apply Z-axis morph
        morphed_fft = self.apply_morph(morphed_fft_data, self.z_morph_type, z_amount)

        # Convert back to time domain (at Serum's native 2048 samples)
        waveform = np.fft.irfft(morphed_fft, n=self.SERUM_FRAME_SIZE)

        # Remove DC offset
        waveform = waveform - np.mean(waveform)

        # Normalize to prevent clipping
        max_val = np.max(np.abs(waveform))
        if max_val > 0:
            waveform = waveform / max_val

        # Resample to target output size if different from Serum native
        if self.output_samples != self.SERUM_FRAME_SIZE:
            waveform = resample_poly(
                waveform,
                self.output_samples,
                self.SERUM_FRAME_SIZE,
                window=("kaiser", 5.0)
            )

        # Oversample for anti-aliasing
        waveform_oversampled = resample_poly(
            waveform, self.oversample_factor, 1, window=("kaiser", 5.0)
        )

        return waveform_oversampled

    def generate_page(self, z_page):
        """
        Generate a single page (8x8 grid) of wavetables for a given Z page.

        Hardware axis mapping:
        - X axis (fast varying): source frame selection
        - Y axis (slow varying): Y morph amount
        - Z axis (page): Z morph amount

        Args:
            z_page: Z-axis page index (0-7)

        Returns:
            numpy.ndarray: Array of wavetables with shape (64, n_samples)
        """
        page = np.zeros((8, 8, self.n_samples))

        for y in range(8):
            for x in range(8):
                # Store with y as first index (slow varying) and x as second (fast varying)
                # This maps to Hardware X = source frame, Hardware Y = morph
                page[y, x] = self.generate_morphed_wave(x, y, z_page)

        # Reshape to (64, n_samples) as expected by base class
        page = page.reshape((64, self.n_samples))

        # Normalize the entire page
        max_val = np.max(np.abs(page))
        if max_val > 0:
            page = page / max_val

        return page

    def generate_all_pages(self, progress_callback=None):
        """
        Generate all 8 pages (full 8x8x8 cube) and save to disk.

        Creates files named 1.wav through 8.wav in the output directory.

        Args:
            progress_callback: Optional callback for progress updates
        """
        if self.source_frames is None:
            raise ValueError("Must call load_wavetable() first")

        print("Generating 8 pages of wavetables...")
        for i in range(8):
            page = self.generate_page(i)
            self.save_wavetables(page, f"{i + 1}.wav")
            if progress_callback:
                progress_value = int((100.0 / 8) * (i + 1))
                progress_callback(progress_value)

        print(f"Wavetables saved to {self.save_path}/{self.name}/")
