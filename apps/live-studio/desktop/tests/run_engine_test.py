"""Exercise the engine, then independently decode the generated recording."""
import json
import math
from pathlib import Path
import subprocess
import sys


def check_recording(data):
    video, = (s for s in data["streams"] if s["codec_type"] == "video")
    audio, = (s for s in data["streams"] if s["codec_type"] == "audio")
    if (video["codec_name"], video["width"], video["height"]) != ("h264", 1280, 720):
        raise ValueError("expected 1280x720 H.264 video")
    if int(video["nb_read_frames"]) < 10:
        raise ValueError("fewer than ten decoded video frames")
    if (audio["codec_name"], audio["channels"], audio["sample_rate"]) != ("aac", 2, "48000"):
        raise ValueError("expected stereo 48 kHz AAC audio")
    times = [float(s["duration"]) for s in (video, audio)]
    if not all(math.isfinite(t) and t > 0 for t in times) or abs(times[0] - times[1]) >= 0.35:
        raise ValueError(f"invalid or mismatched stream durations: {times}")


def main(engine, directory, ffprobe, ffmpeg):
    result = subprocess.run([engine, directory], capture_output=True, text=True, timeout=40)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    if result.returncode:
        return result.returncode
    recording = result.stdout.split("recording=", 1)[1].strip()
    probe = subprocess.run([ffprobe, "-v", "error", "-count_frames", "-show_streams",
                            "-show_format", "-of", "json", recording], check=True,
                           capture_output=True, text=True, timeout=15)
    data = json.loads(probe.stdout)
    check_recording(data)
    subprocess.run([ffmpeg, "-v", "error", "-xerror", "-i", recording,
                    "-map", "0:v:0", "-map", "0:a:0", "-f", "null", "-"], check=True, timeout=20)
    Path(directory, "recording-probe.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
    print("Recorded H.264/AAC streams decoded; A/V duration difference < 350 ms")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
