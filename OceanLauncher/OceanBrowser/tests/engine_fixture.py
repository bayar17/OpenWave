"""Prepare and serve native WPE fixtures on the host (requires Pillow and GStreamer tools).

After build-openwave browser-engine deps, run this script, then launch
browser-engine-probe http://10.0.2.2:8899/index.html 500 inside the QA VM.
Fixture results are available as window.engineProbeResult in the probe log.
"""
import argparse
import hashlib
import shutil
import subprocess
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]


def prepare(destination):
    destination.mkdir(parents=True, exist_ok=True)
    image = Image.new("RGB", (360, 200), "#00bedd")
    ImageDraw.Draw(image).rectangle((30, 30, 330, 170), fill="#9659dc")
    for extension in ("png", "jpg", "webp", "gif"):
        image.save(destination / f"test.{extension}")
    avif = ROOT / "third_party/libavif/tests/data"
    for source, filename in (
        (avif / "paris_icc_exif_xmp.avif", "test.avif"),
        (avif / "colors-animated-8bpc-alpha-exif-xmp.avif", "animated.avif"),
        (avif / "paris_icc_exif_xmp.avif", "mobile.jpg"),
        (avif / "paris_icc_exif_xmp.avif", "mobile.png"),
        (Path(__file__).with_name("engine_fixture.html"), "index.html"),
    ):
        shutil.copyfile(source, destination / filename)
    # The upstream demuxer fixture also contains an unsupported subtitle codec.
    # Remux only VP9/Opus for the browser's Media Source playback test.
    subprocess.run([
        "gst-launch-1.0", "-q", "filesrc",
        "location=" + str(ROOT / "third_party/gstreamer/subprojects/gst-plugins-bad/tests/files/mse.webm"),
        "!", "matroskademux", "name=d", "webmmux", "name=m", "streamable=true",
        "!", "filesink", "location=" + str(destination / "mse.webm"),
        "d.video_0", "!", "queue", "!", "m.video_0",
        "d.audio_0", "!", "queue", "!", "m.audio_0",
        "d.subtitle_0", "!", "fakesink",
    ], check=True)
    video = destination / "test.mp4"
    if not video.exists():
        urllib.request.urlretrieve(
            "https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4", video)
    if hashlib.sha256(video.read_bytes()).hexdigest() != "0cd83d944a6ca7822b4a8306cecc60a36e859b041f6702c6a1ad9ead78924451":
        raise ValueError("Unexpected MP4 fixture checksum")


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/launch':
            phase_file = Path(self.directory) / 'phase.txt'
            phase = phase_file.read_text().strip() if phase_file.exists() else 'all'
            if phase not in ('images', 'video', 'mse', 'rtc', 'all'):
                self.send_error(400, 'Unknown fixture phase')
                return
            self.send_response(302)
            self.send_header('Location', '/index.html?test=' + phase)
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            return
        super().do_GET()

    def guess_type(self, path):
        # Reproduce sites serving AVIF bytes behind JPEG/PNG URL extensions.
        if Path(path).name in ("mobile.jpg", "mobile.png"):
            return "image/avif"
        return super().guess_type(path)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "out/browser-engine-fixture")
    parser.add_argument("--port", type=int, default=8899)
    args = parser.parse_args()
    prepare(args.directory)
    server = ThreadingHTTPServer(("127.0.0.1", args.port),
        lambda *a, **kw: Handler(*a, directory=str(args.directory), **kw))
    print(f"Fixtures: http://127.0.0.1:{args.port}/index.html", flush=True)
    server.serve_forever()
