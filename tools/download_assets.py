#!/usr/bin/env python3
"""Download and verify the Minecraft 1.8.9 files used by NativeVoxel."""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import http.client
import json
import os
from pathlib import Path, PurePosixPath
import re
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CACHE = ROOT / ".cache/minecraft"
VERSION = "1.8.9"
# Pinned from Mojang's version_manifest_v2.json. The digest also verifies cached metadata.
VERSION_URL = "https://piston-meta.mojang.com/v1/packages/d546f1707a3f2b7d034eece5ea2e311eda875787/1.8.9.json"
VERSION_SHA1 = "d546f1707a3f2b7d034eece5ea2e311eda875787"
OFFICIAL_HOSTS = {
    "piston-meta.mojang.com", "piston-data.mojang.com",
    "launchermeta.mojang.com", "launcher.mojang.com",
    "resources.download.minecraft.net", "libraries.minecraft.net",
}


class DownloadError(RuntimeError):
    pass


def official_url(url: str) -> str:
    parsed = urllib.parse.urlsplit(url)
    if (parsed.scheme != "https" or parsed.hostname not in OFFICIAL_HOSTS
            or parsed.username or parsed.password or parsed.port not in (None, 443)):
        raise DownloadError(f"Not an official HTTPS download URL: {url}")
    return url


class OfficialRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return super().redirect_request(req, fp, code, msg, headers, official_url(newurl))


def open_download(url: str):
    request = urllib.request.Request(official_url(url), headers={"User-Agent": "NativeVoxel/0.1 (asset setup)"})
    return urllib.request.build_opener(OfficialRedirect()).open(request, timeout=30)


def verified(path: Path, digest: str, size: int | None) -> bool:
    if not path.is_file() or (size is not None and path.stat().st_size != size):
        return False
    hasher = hashlib.sha1()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest() == digest


def ensure_file(path: Path, url: str, digest: str, size: int | None = None, *, offline=False) -> bool:
    """Keep verified files; atomically replace a missing/corrupt file after verification."""
    official_url(url)
    if not re.fullmatch(r"[0-9a-f]{40}", digest) or (size is not None and (type(size) is not int or size < 0)):
        raise DownloadError(f"Invalid download checksum/size for {path.name}")
    if verified(path, digest, size):
        return False
    if offline:
        raise DownloadError(f"Offline cache is missing or corrupt: {path}. Run setup again without --offline.")
    path.parent.mkdir(parents=True, exist_ok=True)
    for attempt in range(3):
        temporary = None
        try:
            hasher, length = hashlib.sha1(), 0
            with open_download(url) as response, tempfile.NamedTemporaryFile(
                dir=path.parent, prefix=path.name + ".", suffix=".part", delete=False
            ) as target:
                temporary = Path(target.name)
                while chunk := response.read(256 * 1024):
                    length += len(chunk)
                    if length > (size if size is not None else 8 * 1024 * 1024):
                        raise DownloadError(f"Download exceeds expected size: {path.name}")
                    hasher.update(chunk)
                    target.write(chunk)
            if hasher.hexdigest() != digest or (size is not None and length != size):
                raise DownloadError(f"SHA-1/size mismatch: {path.name}")
            os.replace(temporary, path)
            return True
        except (OSError, DownloadError, urllib.error.URLError, http.client.HTTPException) as error:
            if attempt == 2:
                raise DownloadError(f"Cannot download {url}: {error}. Rerun to reuse completed files.") from error
            time.sleep(attempt + 1)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    return False


def cache_path(root: Path, name: str) -> Path:
    if (not re.fullmatch(r"[A-Za-z0-9_./-]+", name) or PurePosixPath(name).is_absolute()
            or any(part in ("", ".", "..") for part in name.split("/"))):
        raise DownloadError(f"Invalid cache path: {name!r}")
    target = (root / name).resolve()
    if not target.is_relative_to(root.resolve()):
        raise DownloadError(f"Cache path escapes its root: {name!r}")
    return target


def download_assets(cache: Path = DEFAULT_CACHE, *, offline=False, workers=8) -> Path:
    cache = Path(cache).resolve()
    if not 1 <= workers <= 32:
        raise DownloadError("Download workers must be between 1 and 32")
    version_path = cache_path(cache, f"jars/versions/{VERSION}/{VERSION}.json")
    print(f"Checking Minecraft {VERSION} files in {cache}", flush=True)
    downloaded = int(ensure_file(version_path, VERSION_URL, VERSION_SHA1, offline=offline))
    metadata = json.loads(version_path.read_bytes())
    if metadata["id"] != VERSION:
        raise DownloadError("Unexpected Minecraft version in metadata")
    index = metadata["assetIndex"]
    if index["id"] != "1.8":
        raise DownloadError("Unexpected Minecraft asset index")
    index_path = cache_path(cache, "jars/assets/indexes/1.8.json")
    downloaded += ensure_file(index_path, index["url"], index["sha1"], index["size"], offline=offline)
    client = metadata["downloads"]["client"]
    jobs = [(cache_path(cache, f"jars/versions/{VERSION}/{VERSION}.jar"), client["url"], client["sha1"], client["size"])]
    # Only this archive is needed: it contains the OpenAL64.dll used by native audio.
    library = next(lib for lib in metadata["libraries"] if lib["name"] == "org.lwjgl.lwjgl:lwjgl-platform:2.9.4-nightly-20150209")
    native = library["downloads"]["classifiers"]["natives-windows"]
    jobs.append((cache_path(cache, "jars/libraries/" + native["path"]), native["url"], native["sha1"], native["size"]))
    objects = {}
    for entry in json.loads(index_path.read_bytes())["objects"].values():
        digest, size = entry["hash"], entry["size"]
        if not re.fullmatch(r"[0-9a-f]{40}", digest) or type(size) is not int or size < 0:
            raise DownloadError("Invalid object hash/size in asset index")
        if digest in objects and objects[digest] != size:
            raise DownloadError("Conflicting object sizes in asset index")
        objects[digest] = size
    for digest, size in sorted(objects.items()):
        relative = f"{digest[:2]}/{digest}"
        jobs.append((cache_path(cache, "jars/assets/objects/" + relative),
                     "https://resources.download.minecraft.net/" + relative, digest, size))
    total = sum(job[3] for job in jobs)
    print(f"Verifying {len(jobs)} files ({total / 1024**2:.1f} MiB); downloading only missing or damaged files.", flush=True)
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(ensure_file, *job, offline=offline) for job in jobs]
        try:
            for done, future in enumerate(as_completed(futures), 1):
                downloaded += future.result()
                if done % 50 == 0 or done == len(jobs):
                    print(f"  {done}/{len(jobs)} verified", flush=True)
        except BaseException:
            for future in futures:
                future.cancel()
            raise
    print(f"Minecraft files ready ({downloaded} downloaded, remaining files reused).", flush=True)
    return cache


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--offline", action="store_true", help="Verify the cache without making network requests")
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()
    try:
        download_assets(args.cache, offline=args.offline, workers=args.workers)
    except (DownloadError, OSError, ValueError, KeyError, StopIteration) as error:
        parser.exit(1, f"Asset download failed: {error}\n")


if __name__ == "__main__":
    main()
