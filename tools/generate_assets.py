#!/usr/bin/env python3
"""
Generates deterministic assets for the Fake Frame Counter Geode mod:
1. logo.png (256x256 RGBA PNG mod icon)
2. resources/ding_1f.wav .. resources/ding_12f.wav (bell chimes pitched from 1F lowest to 12F highest)
"""

import math
import os
import struct
import wave
import zlib


def generate_ding_wav(filepath: str, freq_hz: float, duration_s: float = 0.22, sample_rate: int = 22050) -> None:
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    num_samples = int(sample_rate * duration_s)
    samples = bytearray()

    for i in range(num_samples):
        t = i / sample_rate
        # Smooth 3ms attack to avoid click pop
        attack = min(1.0, t / 0.003) if t < 0.003 else 1.0
        # Smooth tail fade over last 15ms
        tail = max(0.0, (duration_s - t) / 0.015) if (duration_s - t) < 0.015 else 1.0

        # Harmonic bell synthesis: fundamental + octave + 2.756x bell partial
        env1 = math.exp(-13.5 * t)
        env2 = math.exp(-24.0 * t)
        env3 = math.exp(-38.0 * t)

        val = (
            0.68 * env1 * math.sin(2.0 * math.pi * freq_hz * t)
            + 0.24 * env2 * math.sin(2.0 * math.pi * (freq_hz * 2.0) * t)
            + 0.08 * env3 * math.sin(2.0 * math.pi * (freq_hz * 2.756) * t)
        )
        sample = int(max(-1.0, min(1.0, val * attack * tail * 0.90)) * 32767.0)
        samples += struct.pack("<h", sample)

    with wave.open(filepath, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(bytes(samples))


def write_png(filepath: str, width: int, height: int, rgba_bytes: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    raw_rows = bytearray()
    stride = width * 4
    for y in range(height):
        raw_rows.append(0)  # Filter type 0 (None)
        raw_rows.extend(rgba_bytes[y * stride : (y + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png_data = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw_rows), 9))
        + chunk(b"IEND", b"")
    )
    with open(filepath, "wb") as f:
        f.write(png_data)


def generate_logo_png(filepath: str, size: int = 256) -> None:
    buf = bytearray(size * size * 4)
    cx = (size - 1) / 2.0
    cy = (size - 1) / 2.0

    # Concentric ring definitions: (radius, half_thickness, (r, g, b))
    rings = [
        (104.0, 5.5, (50, 173, 230)),   # 10F-12F Blue outer ring
        (86.0,  5.5, (255, 204, 0)),    # 7F-9F Yellow ring
        (68.0,  5.5, (255, 149, 0)),    # 5F-6F Orange ring
        (50.0,  6.5, (255, 59, 48)),    # 1F-4F Red inner ring
    ]

    for y in range(size):
        for x in range(size):
            dx = x - cx
            dy = y - cy
            dist = math.hypot(dx, dy)
            idx = (y * size + x) * 4

            if dist > 118.0:
                # Outside badge: transparent with 1.5px anti-aliasing
                alpha = max(0.0, 1.0 - (dist - 118.0) / 1.5)
                if alpha <= 0.0:
                    continue
                buf[idx : idx + 4] = bytes((18, 22, 34, int(alpha * 255)))
                continue

            # Dark sleek circular background
            bg_r, bg_g, bg_b = 18, 22, 34
            # Subtle radial glow near center
            glow = max(0.0, 1.0 - dist / 118.0)
            r = int(bg_r + glow * 16)
            g = int(bg_g + glow * 20)
            b = int(bg_b + glow * 32)

            # Outer metallic border
            if dist > 112.0:
                edge_t = max(0.0, 1.0 - abs(dist - 115.0) / 3.0)
                r = int(r * (1.0 - edge_t) + 220 * edge_t)
                g = int(g * (1.0 - edge_t) + 230 * edge_t)
                b = int(b * (1.0 - edge_t) + 255 * edge_t)

            # Draw concentric colored rings
            for rad, thick, (rr, rg, rb) in rings:
                d_ring = abs(dist - rad)
                if d_ring <= thick + 1.2:
                    t = max(0.0, min(1.0, 1.0 - max(0.0, d_ring - (thick - 1.0)) / 2.2))
                    r = int(r * (1.0 - t) + rr * t)
                    g = int(g * (1.0 - t) + rg * t)
                    b = int(b * (1.0 - t) + rb * t)

            buf[idx : idx + 4] = bytes((min(255, r), min(255, g), min(255, b), 255))

    # Draw crisp pixel-art "1F" in the center (red/white highlight)
    glyph_1f = [
        "  ###   ####### ",
        " ####   ####### ",
        "  ###   ###     ",
        "  ###   ######  ",
        "  ###   ######  ",
        "  ###   ###     ",
        " #####  ###     ",
        " #####  ###     ",
    ]
    scale = 4
    gw = len(glyph_1f[0]) * scale
    gh = len(glyph_1f) * scale
    ox = int(cx - gw / 2)
    oy = int(cy - gh / 2)

    for gy, row in enumerate(glyph_1f):
        for gx, ch in enumerate(row):
            if ch != "#":
                continue
            for sy in range(scale):
                for sx in range(scale):
                    px = ox + gx * scale + sx
                    py = oy + gy * scale + sy
                    if 0 <= px < size and 0 <= py < size:
                        idx = (py * size + px) * 4
                        buf[idx : idx + 4] = bytes((255, 245, 245, 255))

    write_png(filepath, size, size, bytes(buf))


def main() -> None:
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    logo_path = os.path.join(repo_root, "logo.png")
    generate_logo_png(logo_path)
    print(f"Generated {logo_path} ({os.path.getsize(logo_path)} bytes)")

    # Generate ding_1f.wav (lowest pitch, 440 Hz) through ding_12f.wav (highest pitch, ~1314.4 Hz)
    # Base frequency 440 Hz (A4), stepping up by 2^(1/7) per frame category so 12F is highest and 1F is lowest
    res_dir = os.path.join(repo_root, "resources")
    for frame_num in range(1, 13):
        freq = 440.0 * (2.0 ** ((frame_num - 1) / 7.0))
        wav_path = os.path.join(res_dir, f"ding_{frame_num}f.wav")
        generate_ding_wav(wav_path, freq)
        print(f"Generated {wav_path}: {frame_num}F -> {freq:.2f} Hz ({os.path.getsize(wav_path)} bytes)")


if __name__ == "__main__":
    main()
