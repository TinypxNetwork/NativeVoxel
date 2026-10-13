"""Downloader regression tests; all network responses are local fixtures."""
import hashlib
import http.client
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import download_assets as download


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.target = self.root / "cache/object"
        self.payload = b"verified resource"
        self.digest = hashlib.sha1(self.payload).hexdigest()
        self.url = "https://resources.download.minecraft.net/ab/fixture"

    def fetch(self, **kwargs):
        return download.ensure_file(self.target, self.url, self.digest, len(self.payload), **kwargs)

    def test_verified_cache_never_opens_network(self):
        self.target.parent.mkdir()
        self.target.write_bytes(self.payload)
        with patch.object(download, "open_download") as network:
            self.assertFalse(self.fetch())
            self.assertFalse(self.fetch(offline=True))
            network.assert_not_called()

    def test_download_then_repair_corrupt_cache(self):
        for prior in (None, b"damaged file"):
            if prior is not None:
                self.target.write_bytes(prior)
            with patch.object(download, "open_download", return_value=io.BytesIO(self.payload)):
                self.assertTrue(self.fetch())
            self.assertEqual(self.target.read_bytes(), self.payload)
            self.assertEqual(list(self.target.parent.glob("*.part")), [])

    def test_offline_rejects_missing_and_corrupt_files(self):
        with patch.object(download, "open_download") as network:
            with self.assertRaisesRegex(download.DownloadError, "Offline cache"):
                self.fetch(offline=True)
            self.target.parent.mkdir()
            self.target.write_bytes(b"broken")
            with self.assertRaisesRegex(download.DownloadError, "Offline cache"):
                self.fetch(offline=True)
            network.assert_not_called()

    def test_bad_download_preserves_existing_file_and_cleans_partial(self):
        self.target.parent.mkdir()
        self.target.write_bytes(b"existing")
        for payload in (b"too short", b"x" * len(self.payload), self.payload + b"too long"):
            with patch.object(download, "open_download", side_effect=lambda _: io.BytesIO(payload)), patch.object(download.time, "sleep"):
                with self.assertRaises(download.DownloadError):
                    self.fetch()
            self.assertEqual(self.target.read_bytes(), b"existing")
            self.assertEqual(list(self.target.parent.glob("*.part")), [])

    def test_interrupted_response_is_retried(self):
        class Interrupted(io.BytesIO):
            def read(self, size=-1):
                raise http.client.IncompleteRead(b"partial")
        with patch.object(download, "open_download", side_effect=[Interrupted(), io.BytesIO(self.payload)]) as network, patch.object(download.time, "sleep"):
            self.assertTrue(self.fetch())
            self.assertEqual(network.call_count, 2)
        self.assertEqual(self.target.read_bytes(), self.payload)
        self.assertEqual(list(self.target.parent.glob("*.part")), [])

    def test_rejects_untrusted_urls_and_cache_paths(self):
        for url in ("http://resources.download.minecraft.net/file", "https://example.com/file",
                    "https://user@piston-data.mojang.com/file", "https://piston-data.mojang.com:444/file"):
            with self.assertRaises(download.DownloadError):
                download.official_url(url)
        for name in ("../escape", "/absolute", "C:/escape", "safe/../../escape", "dir\\file", "dir//file"):
            with self.assertRaises(download.DownloadError):
                download.cache_path(self.root, name)
        self.assertEqual(download.cache_path(self.root, "jars/assets/a.json"), self.root / "jars/assets/a.json")

    def test_redirect_to_untrusted_host_is_rejected(self):
        with self.assertRaises(download.DownloadError):
            download.OfficialRedirect().redirect_request(None, None, 302, "Found", {}, "https://example.com/file")

    def test_metadata_client_natives_and_deduplicated_objects(self):
        responses = {}
        def record(name, data):
            url = "https://piston-data.mojang.com/" + name
            responses[url] = data
            return {"url": url, "sha1": hashlib.sha1(data).hexdigest(), "size": len(data)}
        index_data = json.dumps({"objects": {name: {"hash": self.digest, "size": len(self.payload)} for name in ("minecraft/a", "minecraft/b")}}).encode()
        index = dict(record("index", index_data), id="1.8")
        native = dict(record("natives", b"natives"), path="org/lwjgl/windows.jar")
        metadata = {"id": "1.8.9", "assetIndex": index, "downloads": {"client": record("client", b"client jar")},
                    "libraries": [{"name": "org.lwjgl.lwjgl:lwjgl-platform:2.9.4-nightly-20150209", "downloads": {"classifiers": {"natives-windows": native}}}]}
        version = record("version", json.dumps(metadata).encode())
        responses[f"https://resources.download.minecraft.net/{self.digest[:2]}/{self.digest}"] = self.payload
        with patch.object(download, "VERSION_URL", version["url"]), patch.object(download, "VERSION_SHA1", version["sha1"]):
            with patch.object(download, "open_download", side_effect=lambda url: io.BytesIO(responses[url])) as network:
                download.download_assets(self.root, workers=2)
                self.assertEqual(network.call_count, 5)
            with patch.object(download, "open_download") as network:
                download.download_assets(self.root, offline=True)
                network.assert_not_called()
        self.assertEqual((self.root / "jars/versions/1.8.9/1.8.9.jar").read_bytes(), b"client jar")
        self.assertEqual((self.root / "jars/libraries/org/lwjgl/windows.jar").read_bytes(), b"natives")


if __name__ == "__main__":
    unittest.main()
