#!/usr/bin/env python3
"""Fetch a small public-domain real-recording corpus for local evaluation."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


COMMONS_API = "https://commons.wikimedia.org/w/api.php"
USER_AGENT = "HarborWavetableEvaluation/0.1 (https://github.com/Ferry-Island-Modular/Harbor)"
SOURCES = (
    {
        "slug": "voice_hello_everyone",
        "category": "voice",
        "title": "File:Hello, everyone!.ogg",
        "author": "VOA Learning English",
        "license": "Public Domain Mark 1.0",
    },
    {
        "slug": "instrument_flute",
        "category": "acoustic_instrument",
        "title": "File:Flute.ogg",
        "author": "hokuspokus",
        "license": "Public domain",
    },
    {
        "slug": "percussion_plinking_loop",
        "category": "percussion_loop",
        "title": "File:Plinking rythm loop.ogg",
        "author": "stephan",
        "license": "Public domain",
    },
    {
        "slug": "field_rain",
        "category": "field_recording",
        "title": "File:Rain (1).ogg",
        "author": "ezwa",
        "license": "Public domain",
    },
    {
        "slug": "mix_aleatoric",
        "category": "dense_mix",
        "title": "File:Aleatoric music with 5 pdsounds.ogg",
        "author": "stephan",
        "license": "Public domain",
    },
)


def request_json(url: str) -> dict[str, Any]:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def commons_metadata(title: str) -> dict[str, str]:
    query = urllib.parse.urlencode(
        {
            "action": "query",
            "format": "json",
            "formatversion": "2",
            "prop": "imageinfo",
            "iiprop": "url|extmetadata",
            "titles": title,
        }
    )
    payload = request_json(f"{COMMONS_API}?{query}")
    pages = payload.get("query", {}).get("pages", [])
    if len(pages) != 1 or "imageinfo" not in pages[0]:
        raise RuntimeError(f"Wikimedia Commons did not return media metadata for {title}")
    info = pages[0]["imageinfo"][0]
    extmetadata = info.get("extmetadata", {})
    return {
        "download_url": info["url"],
        "description_url": info["descriptionurl"],
        "license_reported": extmetadata.get("LicenseShortName", {}).get("value", ""),
    }


def download(url: str, destination: Path) -> None:
    temporary = destination.with_suffix(destination.suffix + ".part")
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=120) as response, temporary.open("wb") as output:
        shutil.copyfileobj(response, output)
    temporary.replace(destination)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def convert_to_wav(source: Path, destination: Path) -> None:
    temporary = destination.with_suffix(".part.wav")
    subprocess.run(
        [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-i",
            str(source),
            "-t",
            "20",
            "-ac",
            "1",
            "-ar",
            "44100",
            "-c:a",
            "pcm_s16le",
            str(temporary),
        ],
        check=True,
    )
    temporary.replace(destination)


def fetch(output_root: Path) -> None:
    if shutil.which("ffmpeg") is None:
        raise RuntimeError("ffmpeg is required to create normalized WAV fixtures")

    originals = output_root / "originals"
    real = output_root / "real"
    originals.mkdir(parents=True, exist_ok=True)
    real.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, Any] = {
        "purpose": "Local qualitative wavetable evaluation; audio files are not committed.",
        "conversion": "First 20 seconds, mono, 44.1 kHz, signed 16-bit PCM WAV.",
        "sources": [],
    }

    for source in SOURCES:
        print(f"Fetching {source['slug']}...", flush=True)
        metadata = commons_metadata(source["title"])
        reported_license = metadata["license_reported"].lower()
        if "public domain" not in reported_license and "cc0" not in reported_license:
            raise RuntimeError(
                f"Refusing {source['title']}: Commons currently reports "
                f"license {metadata['license_reported']!r}"
            )

        extension = Path(urllib.parse.urlparse(metadata["download_url"]).path).suffix
        original_path = originals / f"{source['slug']}{extension}"
        wav_path = real / f"{source['slug']}.wav"
        download(metadata["download_url"], original_path)
        convert_to_wav(original_path, wav_path)
        manifest["sources"].append(
            {
                **source,
                **metadata,
                "original_path": str(original_path.relative_to(output_root)),
                "original_sha256": sha256(original_path),
                "wav_path": str(wav_path.relative_to(output_root)),
                "wav_sha256": sha256(wav_path),
            }
        )

    manifest_path = output_root / "manifest.json"
    temporary_manifest = manifest_path.with_suffix(".json.part")
    temporary_manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    temporary_manifest.replace(manifest_path)
    print(f"Real-recording corpus written to {output_root}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "output_directory",
        nargs="?",
        type=Path,
        default=Path("evaluation-corpus"),
    )
    args = parser.parse_args()
    try:
        fetch(args.output_directory.resolve())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
