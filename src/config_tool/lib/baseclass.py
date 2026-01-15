import os.path

import numpy as np
import scipy.io.wavfile as wavfile
from scipy.signal import resample_poly


class WavetableGeneratorBaseClass:
    def __init__(
        self,
        num_waves=64,
        samples=256,
        save_path="output_waves",
        sample_rate=44100,
        oversample_factor=8,
    ):
        """
        Initialize wavetable generator

        Args:
        - num_waves: Number of wavetables to generate
        - samples: Number of samples per wavetable
        """
        self.num_waves = num_waves
        self.save_path = save_path
        self.oversample_factor = oversample_factor
        self.n_samples = self.oversample_factor * samples

        self.sample_rate = sample_rate
        self.t = np.linspace(0, 2 * np.pi, self.n_samples, endpoint=False)

    def _assemble_wavetable_page_by_fn(self, z, f, *args, **kwargs):
        """Generates an 8x8 wavetable page based on transfer function f.

        Args:
            z (int): Z index for the wavetable page
            f (function): Function that generates wavetables. Signature is f(x, y, z, num_harmonics) => numpy.ndarray(n_samples * oversample_factor)

        Returns:
            numpy.ndarray: Array of wavetables with shape (64, n_samples * oversample_factor)
        """
        page = np.zeros((8, 8, self.n_samples))

        for x in range(8):
            for y in range(8):
                wave = f(y, x, z, *args, **kwargs)
                page[x, y] = wave

        page = page.reshape((64, self.n_samples))
        return page

    def save_wavetables(self, wavetables, filename="wavetables.wav"):
        """
        Save wavetables to a WAV file
        """

        wavetables_resampled = np.zeros(
            (self.num_waves, self.n_samples // self.oversample_factor)
        )

        for i in range(self.num_waves):
            wavetables_resampled[i] = resample_poly(
                wavetables[i],
                1,
                self.oversample_factor,
                window=("kaiser", 5.0),  # Explicit Kaiser window for better filter
            )

        # Normalize wavetables
        wavetables_resampled = wavetables_resampled / np.max(
            np.abs(wavetables_resampled)
        )
        wavetables = wavetables_resampled

        # Convert to 16-bit signed integers
        wavetables_16bit = (wavetables * 32767).astype(np.int16)

        # Flatten the 2D array to 1D for WAV file
        flattened_wavetables = wavetables_16bit.flatten()

        # Write as single WAV file
        os.makedirs(os.path.abspath(self.save_path), exist_ok=True)
        wavfile.write(
            os.path.join(os.path.abspath(self.save_path), filename),
            self.sample_rate,
            flattened_wavetables,
        )

    def save_wavetable_set(self, z_range, fn):
        for i in range(z_range):
            wavetables = fn(i)
            self.save_wavetables(wavetables, f"{i + 1}.wav")
