"""Synthesizes resources/click.wav: a mechanical keyboard click (clicky switch).
The sharp transient is at sample 0 so the sound lines up with the cue.
Run: python docs/make_click.py  (needs numpy)."""
import wave
from pathlib import Path

import numpy as np

RATE = 44100
LENGTH = 0.09
rng = np.random.default_rng(7)
t = np.arange(int(RATE * LENGTH)) / RATE


def ring(freq, decay, start=0.0, amp=1.0):
    """Damped sine, like a small part of the switch or case resonating."""
    x = np.clip(t - start, 0, None)
    return np.where(t >= start, amp * np.sin(2 * np.pi * freq * x) * np.exp(-x / decay), 0.0)


def burst(decay, start=0.0, amp=1.0, smooth=1):
    """Noise burst; `smooth` > 1 low-passes it with a moving average."""
    x = np.clip(t - start, 0, None)
    n = rng.standard_normal(len(t))
    if smooth > 1:
        n = np.convolve(n, np.ones(smooth) / smooth, mode="same")
    return np.where(t >= start, amp * n * np.exp(-x / decay), 0.0)


def highpass(x):
    return np.diff(x, prepend=0.0)


# The click: switch leaf snapping, short and bright
click = highpass(burst(0.0012, amp=1.0)) * 0.9 + ring(4300, 0.0025, amp=0.5) + ring(6100, 0.0015, amp=0.3)

# Bottom-out a few ms later: keycap hitting the plate, lower and rounder
bottom = 0.0065
thock = (burst(0.004, bottom, amp=0.8, smooth=6)
         + ring(1850, 0.008, bottom, amp=0.45)
         + ring(950, 0.012, bottom, amp=0.35)
         + ring(230, 0.018, bottom, amp=0.25))

# A bit of case / desk ring
tail = burst(0.02, 0.003, amp=0.06, smooth=12)

sound = click + thock + tail
fade = np.ones_like(t)
fade[-200:] = np.linspace(1, 0, 200)
sound = sound * fade
sound = sound / np.max(np.abs(sound)) * 0.9

path = Path(__file__).resolve().parent.parent / "resources" / "click.wav"
path.parent.mkdir(exist_ok=True)
with wave.open(str(path), "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes((sound * 32767).astype("<i2").tobytes())
print("saved", path)
