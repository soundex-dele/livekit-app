"""Exercise the recording contract with Python optimization enabled by CTest."""
from copy import deepcopy
import unittest

from run_engine_test import check_recording

GOOD = {"streams": [
    {"codec_type": "video", "codec_name": "h264", "width": 1280, "height": 720,
     "nb_read_frames": "90", "duration": "3.00"},
    {"codec_type": "audio", "codec_name": "aac", "channels": 2, "sample_rate": "48000",
     "duration": "3.02"},
]}


class RecordingContract(unittest.TestCase):
    def test_valid_recording(self):
        check_recording(GOOD)

    def test_invalid_streams(self):
        for index, field, value in ((0, "codec_name", "vp9"), (0, "width", 320), (0, "height", 180),
                                    (0, "nb_read_frames", "9"), (1, "channels", 1),
                                    (1, "sample_rate", "44100"), (1, "codec_name", "mp3"),
                                    (1, "duration", "4.0"), (0, "duration", "nan")):
            with self.subTest(field=field, value=value):
                bad = deepcopy(GOOD)
                bad["streams"][index][field] = value
                with self.assertRaises(ValueError):
                    check_recording(bad)


if __name__ == "__main__":
    unittest.main()
