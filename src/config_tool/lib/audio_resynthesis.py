from config_tool.lib.baseclass import WavetableGeneratorBaseClass
from scipy.signal import resample, stft, windows
import librosa
import numpy as np
import scipy.io.wavfile as wavfile


class AudioResynthWavetableGenerator(WavetableGeneratorBaseClass):
    def __init__(
        self, num_waves, samples, save_path, oversample_factor=4, name="audio_resynth"
    ):
        self.name = name
        super().__init__(
            num_waves=num_waves,
            samples=samples,
            save_path=save_path,
            oversample_factor=oversample_factor,
        )
        self.cached_spectrum = None
        self.cached_audio_path = None

    def _analyze_audio(self, audio_path, fft_size=2048):
        """Analyze audio file and extract spectral information"""
        # Only reanalyze if we haven't already cached this file
        if self.cached_spectrum is None or audio_path != self.cached_audio_path:
            print(f"Analyzing audio file: {audio_path}")

            # Read audio file
            sample_rate, audio = wavfile.read(audio_path)

            # Convert to mono if stereo
            if len(audio.shape) > 1:
                audio = np.mean(audio, axis=1)

            # Normalize audio
            audio = audio.astype(float) / np.max(np.abs(audio))

            # Apply window for better frequency resolution
            window = windows.hann(fft_size)

            # Compute STFT
            frequencies, times, spectrum = stft(
                audio,
                fs=sample_rate,
                window=window,
                nperseg=fft_size,
                noverlap=fft_size // 2,
            )

            # Cache the results
            self.cached_spectrum = spectrum
            self.cached_audio_path = audio_path
            self.frequencies = frequencies
            self.times = times

            return frequencies, times, spectrum
        else:
            return self.frequencies, self.times, self.cached_spectrum

    def _extract_single_cycle(self, magnitude, phase, start_frame=0):
        """Extract a single-cycle waveform from spectral data"""
        # Use only the specified frame for reconstruction
        spec_frame = magnitude[:, start_frame] * np.exp(1j * phase[:, start_frame])

        # Create full spectrum for ifft
        full_spec = np.zeros(len(spec_frame) * 2 - 1, dtype=complex)
        full_spec[: len(spec_frame)] = spec_frame

        # Mirror for conjugate symmetry (to get real output)
        full_spec[len(spec_frame) :] = np.conj(spec_frame[1:])[::-1]

        # Inverse FFT to get time domain signal
        time_signal = np.fft.ifft(full_spec).real

        # Extract a clean cycle
        cycle_length = self.n_samples

        # Find zero crossings to get a clean cycle
        zero_crossings = np.where(np.diff(np.signbit(time_signal)))[0]

        if len(zero_crossings) > 1:
            # Find a zero crossing with enough samples after it
            for zc in zero_crossings:
                if zc + cycle_length < len(time_signal):
                    start_idx = zc
                    break
            else:
                # If no good zero crossing found, just use the beginning
                start_idx = 0
        else:
            start_idx = 0

        # Extract cycle
        cycle = time_signal[start_idx : start_idx + cycle_length]

        # Resample if necessary
        if len(cycle) != cycle_length:
            cycle = resample(cycle, cycle_length)

        # Apply window to avoid discontinuities
        window = windows.hann(len(cycle))
        cycle = cycle * window

        # Normalize
        if np.max(np.abs(cycle)) > 0:
            cycle = cycle / np.max(np.abs(cycle))

        return cycle

    def _spectral_modifications(self, magnitude, phase, x, y, z):
        """Apply spectral modifications based on x, y, z parameters"""
        x_norm = x / 7.0
        y_norm = y / 7.0
        z_norm = z / 7.0

        # Make a copy of the spectrum to modify
        mag_modified = magnitude.copy()
        phase_modified = phase.copy()

        # 1. Spectral Tilt (y parameter)
        freq_idx = np.arange(len(mag_modified))
        tilt_factor = np.exp(((y_norm * 2) - 1) * freq_idx / len(freq_idx) * 5)
        mag_modified = mag_modified * tilt_factor[:, np.newaxis]

        # 2. Spectral Stretching/Compression (x parameter)
        stretch_amount = 0.5 + x_norm * 1.5  # 0.5 to 2.0

        # Create spectral envelope
        env = np.mean(mag_modified, axis=1)
        # Apply stretching to envelope
        stretched_env = np.zeros_like(env)

        for i in range(len(env)):
            src_idx = i / stretch_amount
            if src_idx < len(env) - 1:
                # Linear interpolation
                idx_floor = int(np.floor(src_idx))
                idx_ceil = int(np.ceil(src_idx))
                fraction = src_idx - idx_floor
                stretched_env[i] = (
                    env[idx_floor] * (1 - fraction) + env[idx_ceil] * fraction
                )

        # Apply stretched envelope
        mag_modified = (
            mag_modified * (stretched_env / (np.mean(env) + 1e-10))[:, np.newaxis]
        )

        # 3. Phase Manipulation (z parameter)
        if z_norm > 0:
            # Gradually randomize phases as z increases
            random_phase = np.random.uniform(0, 2 * np.pi, phase_modified.shape)
            phase_modified = (1 - z_norm) * phase_modified + z_norm * random_phase

            # Add phase coherence between adjacent bins (formant-like effect)
            coherence = z_norm * 0.5
            smoothed_phase = np.zeros_like(phase_modified)
            smoothed_phase[0] = phase_modified[0]

            for i in range(1, len(phase_modified)):
                smoothed_phase[i] = (
                    phase_modified[i] * (1 - coherence)
                    + (smoothed_phase[i - 1] + np.random.uniform(-0.1, 0.1)) * coherence
                )

            phase_modified = smoothed_phase

        return mag_modified, phase_modified

    def _generate_wavetable_from_audio(self, audio_path, x, y, z, frame_selection=None):
        """Generate a single wavetable from audio with x, y, z controls"""
        # Analyze audio if not already cached
        _, times, spectrum = self._analyze_audio(audio_path)

        # Extract magnitude and phase
        magnitude = np.abs(spectrum)
        phase = np.angle(spectrum)

        # Apply spectral modifications
        mag_modified, phase_modified = self._spectral_modifications(
            magnitude, phase, x, y, z
        )

        # Select frame based on x parameter if not specified
        if frame_selection is None:
            frame_selection = int(x / 7.0 * (spectrum.shape[1] - 1))

        # Extract single cycle
        wave = self._extract_single_cycle(mag_modified, phase_modified, frame_selection)

        return wave

    def generate_audio_page(self, z, audio_path):
        """Generate an 8x8 wavetable page from audio file"""
        page = np.zeros((8, 8, self.n_samples))

        # Generate wavetables for each x, y position
        for y in range(8):
            for x in range(8):
                wave = self._generate_wavetable_from_audio(audio_path, x, y, z)
                page[x, y] = wave

        # Reshape to match expected format
        page = page.reshape((64, self.n_samples))
        return page

    def _load_audio(self, audio_path, normalize=True):
        """Load and prepare audio file for resynthesis"""
        try:
            # Load audio file
            y, sr = librosa.load(audio_path, sr=self.sample_rate)

            # Ensure mono
            if len(y.shape) > 1:
                y = np.mean(y, axis=1)

            # Remove DC offset
            y = y - np.mean(y)

            # Normalize if requested
            if normalize:
                y = y / (np.max(np.abs(y)) + 1e-10)

            return y, sr
        except Exception as e:
            print(f"Error loading audio file: {e}")
            # Return fallback sine wave
            t = np.linspace(0, 2, self.sample_rate * 2, endpoint=False)
            return np.sin(2 * np.pi * 440 * t), self.sample_rate

    def _cross_audio_resynth(self, x, y, z, audio_files, max_harmonics=32):
        """
        Create wavetables by interpolating between multiple audio sources.

        Args:
            x (int): Controls balance between audio sources (0-7)
            y (int): Controls harmonic complexity (0-7)
            z (int): Controls which pair of sounds to blend (0-7)
            audio_files (list): List of tuples with (audio, sr) for each source
        """
        # Ensure we have at least 2 audio files
        if len(audio_files) < 2:
            return np.sin(self.t)  # Fallback if not enough files

        # Normalize parameters
        x_norm = x / 7.0
        y_norm = y / 7.0
        z_norm = z / 7.0

        # Determine which pair of audio files to blend based on z
        num_pairs = len(audio_files) - 1
        pair_idx = min(int(z_norm * num_pairs), num_pairs - 1)

        # Get the two audio sources to blend
        audio1, sr1 = audio_files[pair_idx]
        audio2, sr2 = audio_files[pair_idx + 1]

        # Extract spectral features
        stft1 = librosa.stft(audio1, n_fft=1024, hop_length=256)
        stft2 = librosa.stft(audio2, n_fft=1024, hop_length=256)

        magnitude1 = np.abs(stft1)
        magnitude2 = np.abs(stft2)

        # Average across time for spectral profile
        avg_spec1 = np.mean(magnitude1, axis=1)
        avg_spec2 = np.mean(magnitude2, axis=1)

        # Blend spectra based on x parameter
        blended_spectrum = (1 - x_norm) * avg_spec1 + x_norm * avg_spec2

        # Apply harmonic complexity control (y)
        # Higher y = more frequencies included
        threshold = 0.8 - 0.75 * y_norm  # 0.8 to 0.05

        # Find peaks above threshold
        peak_mask = blended_spectrum > (threshold * np.max(blended_spectrum))

        # Generate wave using additive synthesis
        wave = np.zeros(self.n_samples)

        # Get frequency ratios
        freqs = librosa.fft_frequencies(sr=sr1, n_fft=1024)

        # Find and sort peaks
        peak_indices = np.where(peak_mask)[0]
        if len(peak_indices) > max_harmonics:
            # Keep strongest peaks
            peak_strengths = blended_spectrum[peak_indices]
            strongest_indices = np.argsort(peak_strengths)[-max_harmonics:]
            peak_indices = peak_indices[strongest_indices]

        # Create harmonic components from peaks
        for idx in peak_indices:
            if idx < len(freqs) and idx > 0:  # Skip DC
                # Frequency ratio relative to fundamental
                freq_ratio = freqs[idx] / freqs[1]

                # Amplitude from spectrum
                amp = blended_spectrum[idx] / (np.max(blended_spectrum) + 1e-10)

                # Add to wave if significant
                if amp > 0.01:
                    wave += amp * np.sin(freq_ratio * self.t)

        # Normalize output
        return wave / (np.max(np.abs(wave)) + 1e-10)

    def generate_multi_audio_page(self, z, audio_paths):
        """
        Generate wavetables by interpolating between multiple audio files.

        Args:
            z (int): Z-dimension parameter
            audio_paths (list): List of paths to audio files
        """
        # Load all audio files
        audio_files = []
        for path in audio_paths:
            audio, sr = self._load_audio(path)
            audio_files.append((audio, sr))

        # Use cross-synthesis method
        page = self._assemble_wavetable_page_by_fn(
            z=z, f=lambda x, y, z: self._cross_audio_resynth(x, y, z, audio_files)
        )

        # Apply gentle saturation with DC offset removal
        page = np.tanh(page * 1.05)

        # Remove any DC offset from each wave
        for i in range(page.shape[0]):
            page[i] = page[i] - np.mean(page[i])

        return page
