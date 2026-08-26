#!/usr/bin/env python3
from pathlib import Path
import math
import wave

ROOT = Path(__file__).resolve().parents[1]
AUDIO = ROOT / "audio"
RATE = 11025


def write_wav(name, freqs, duration, volume=0.45):
    AUDIO.mkdir(exist_ok=True)
    frames = bytearray()
    total = max(1, int(RATE * duration))

    for i in range(total):
        t = i / RATE
        env = 1.0 - i / total
        freq = freqs[min(len(freqs) - 1, int(i * len(freqs) / total))]
        sample = 128 + int(127 * volume * env * (1 if math.sin(t * freq * math.tau) >= 0 else -1))
        frames.append(max(0, min(255, sample)))

    with wave.open(str(AUDIO / name), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(1)
        out.setframerate(RATE)
        out.writeframes(frames)


def main():
    write_wav("jump.wav", [660, 880, 1046], 0.14)
    write_wav("block_hit.wav", [220, 180], 0.08)
    write_wav("brick_break.wav", [176, 352, 140, 280], 0.16, 0.55)
    write_wav("trap_trigger.wav", [988, 494, 247], 0.18)
    write_wav("death.wav", [330, 247, 165, 110], 0.42)


if __name__ == "__main__":
    main()
