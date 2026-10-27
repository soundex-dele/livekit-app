"""Exercise the engine, then independently decode the generated recording."""
import json
from pathlib import Path
import subprocess
import sys

result = subprocess.run([sys.argv[1], sys.argv[2]], capture_output=True, text=True, timeout=40)
print(result.stdout, end="")
print(result.stderr, end="", file=sys.stderr)
if result.returncode:
    sys.exit(result.returncode)
recording = result.stdout.split("recording=", 1)[1].strip()
probe = subprocess.run([sys.argv[3], "-v", "error", "-count_frames", "-show_streams",
                        "-show_format", "-of", "json", recording], check=True,
                       capture_output=True, text=True, timeout=15)
data = json.loads(probe.stdout)
video = next(s for s in data["streams"] if s["codec_type"] == "video")
audio = next(s for s in data["streams"] if s["codec_type"] == "audio")
assert video["codec_name"] == "h264" and (video["width"], video["height"]) == (1280, 720)
assert int(video["nb_read_frames"]) >= 10
assert audio["codec_name"] == "aac" and audio["channels"] == 2 and audio["sample_rate"] == "48000"
assert abs(float(video["duration"]) - float(audio["duration"])) < 0.35, "A/V duration mismatch"
subprocess.run([sys.argv[4], "-v", "error", "-xerror", "-i", recording,
                "-f", "null", "-"], check=True, timeout=20)
Path(sys.argv[2], "recording-probe.json").write_text(json.dumps(data, indent=2))
print("Recorded H.264/AAC streams decoded; A/V duration difference < 350 ms")
