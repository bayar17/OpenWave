#!/usr/bin/env python3
import argparse
from array import array
import math
import sys
import wave


def check_audio(path):
    found = set()
    with wave.open(str(path), 'rb') as recording:
        if recording.getsampwidth() != 2:
            raise ValueError('Expected a 16-bit PCM QEMU recording')
        rate, channels = recording.getframerate(), recording.getnchannels()
        frames = rate // 4
        basis = {frequency: [(math.cos(2 * math.pi * frequency * n / rate),
                              math.sin(2 * math.pi * frequency * n / rate))
                             for n in range(frames)]
                 for frequency in (440, 880)}
        while block := recording.readframes(frames):
            samples = array('h', block)
            if sys.byteorder != 'little':
                samples.byteswap()
            samples = samples[::channels]
            if len(samples) != frames:
                break
            energy = sum(sample * sample for sample in samples) / frames
            if energy < 10000:
                continue
            for frequency in basis.keys() - found:
                real = sum(sample * pair[0] for sample, pair in zip(samples, basis[frequency]))
                imaginary = sum(sample * pair[1] for sample, pair in zip(samples, basis[frequency]))
                amplitude_squared = 4 * (real * real + imaginary * imaginary) / (frames * frames)
                if amplitude_squared / (2 * energy) > 0.7:
                    found.add(frequency)
    missing = {440, 880} - found
    if missing:
        raise ValueError('Missing audible fixture tones: ' + ', '.join(map(str, sorted(missing))))
    print('PASS: HTML media (440 Hz) and Opus MediaSource (880 Hz) reached the QEMU sound device')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('recording', help='WAV file produced by QEMU -audiodev wav')
    args = parser.parse_args()
    check_audio(args.recording)
