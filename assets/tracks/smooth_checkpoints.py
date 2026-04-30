#!/usr/bin/env python3
"""
Smooth and optimize the recorded racing line checkpoints.
- Applies a moving-average filter to remove driving jitter
- Downsamples to ~300 points for cleaner AI navigation
- Preserves the original as _recorded_raw.csv backup
"""
import csv
import shutil
import os

TRACK_DIR = os.path.dirname(os.path.abspath(__file__))
INPUT = os.path.join(TRACK_DIR, "spa_checkpoints.csv")
BACKUP = os.path.join(TRACK_DIR, "spa_recorded_raw.csv")
OUTPUT = os.path.join(TRACK_DIR, "spa_checkpoints.csv")

def read_csv(path):
    points = []
    with open(path, "r") as f:
        reader = csv.reader(f)
        header = next(reader)  # skip header
        for row in reader:
            if len(row) >= 3:
                try:
                    points.append((float(row[0]), float(row[1]), float(row[2])))
                except ValueError:
                    continue
    return points

def smooth(points, window=7):
    """Apply a centered moving average with given window size."""
    n = len(points)
    half = window // 2
    smoothed = []
    for i in range(n):
        sx, sy, sz = 0.0, 0.0, 0.0
        count = 0
        for j in range(i - half, i + half + 1):
            idx = j % n  # wrap around for circular track
            sx += points[idx][0]
            sy += points[idx][1]
            sz += points[idx][2]
            count += 1
        smoothed.append((sx / count, sy / count, sz / count))
    return smoothed

def downsample(points, target_count=300):
    """Evenly sample target_count points from the smoothed data."""
    n = len(points)
    if n <= target_count:
        return points
    step = n / target_count
    result = []
    for i in range(target_count):
        idx = int(i * step) % n
        result.append(points[idx])
    return result

def write_csv(path, points, radius=12):
    with open(path, "w", newline="") as f:
        f.write("x,y,z,radius\n")
        for x, y, z in points:
            f.write(f"{x},{y},{z},{radius}\n")

def main():
    print(f"Reading {INPUT}...")
    raw = read_csv(INPUT)
    print(f"  {len(raw)} raw points")

    # Backup the raw recording
    shutil.copy2(INPUT, BACKUP)
    print(f"  Backed up raw data to {BACKUP}")

    # Smooth with a 7-point moving average (removes micro-jitter)
    smoothed = smooth(raw, window=7)
    print(f"  Smoothed with window=7")

    # Second pass: smooth again with window=5 for extra polish
    smoothed = smooth(smoothed, window=5)
    print(f"  Second smooth pass with window=5")

    # Downsample to ~300 points for clean AI navigation
    final = downsample(smoothed, target_count=300)
    print(f"  Downsampled to {len(final)} points")

    # Write the optimized checkpoints
    write_csv(OUTPUT, final)
    print(f"  Wrote optimized checkpoints to {OUTPUT}")
    print("Done!")

if __name__ == "__main__":
    main()
