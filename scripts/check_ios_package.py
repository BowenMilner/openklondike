#!/usr/bin/env python3
"""Check the assembled device IPA, including iOS 27 scene adoption (macOS)."""
import pathlib
import plistlib
import subprocess
import struct
import sys
import tempfile
import zipfile


def check(path):
    with zipfile.ZipFile(path) as package:
        corrupt = package.testzip()
        if corrupt:
            raise ValueError(f"corrupt ZIP member: {corrupt}")
        plists = [name for name in package.namelist()
                  if name.startswith("Payload/") and name.endswith(".app/Info.plist")
                  and name.count("/") == 2]
        if len(plists) != 1:
            raise ValueError("expected exactly one app bundle")
        info = plistlib.loads(package.read(plists[0]))
        if info.get("CFBundleSupportedPlatforms") != ["iPhoneOS"]:
            raise ValueError("expected an iPhone device build")
        manifest = info.get("UIApplicationSceneManifest", {})
        configs = manifest.get("UISceneConfigurations", {}).get(
            "UIWindowSceneSessionRoleApplication", [])
        if not configs:
            raise ValueError("missing application scene configuration; iOS 27 rejects launch")
        if manifest.get("UIApplicationSupportsMultipleScenes") is not False:
            raise ValueError("the shared game loop requires a single scene")
        binary_name = str(pathlib.PurePosixPath(plists[0]).parent /
                          info["CFBundleExecutable"])
        with tempfile.TemporaryDirectory(prefix="solitaire-package-") as directory:
            binary = pathlib.Path(directory) / "app"
            binary.write_bytes(package.read(binary_name))
            symbols = subprocess.check_output(["xcrun", "nm", str(binary)], text=True)
        symbol_names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
        for config in configs:
            delegate = config.get("UISceneDelegateClassName", "")
            if not delegate or "_OBJC_CLASS_$_" + delegate not in symbol_names:
                raise ValueError(f"scene delegate is absent from the executable: {delegate!r}")
            callback = f"-[{delegate} scene:willConnectToSession:options:]"
            if callback not in symbols:
                raise ValueError(f"scene delegate has no window connection callback: {delegate}")
        if "_OBJC_CLASS_$_SolitaireViewController" not in symbol_names:
            raise ValueError("native solitaire controller is missing from the executable")
        resource_root = str(pathlib.PurePosixPath(plists[0]).parent / "Cards")
        if any(name.startswith(resource_root + "/Cards/") for name in package.namelist()):
            raise ValueError("duplicate nested card resources from a repeated build")
        faces = set()
        for name in [f"{suit}{rank:02}.png" for suit in "cdhs" for rank in range(1, 14)] + ["back.png"]:
            pixels = package.read(resource_root + "/" + name)
            if pixels[:8] != b"\x89PNG\r\n\x1a\n" or len(pixels) < 512:
                raise ValueError(f"invalid card image: {name}")
            width, height = struct.unpack(">II", pixels[16:24])
            if width < 300 or not 1.35 < height / width < 1.5:
                raise ValueError(f"incorrect card size/ratio: {name}: {width}x{height}")
            if name != "back.png":
                faces.add(pixels)
        if len(faces) != 52:
            raise ValueError("the deck must contain 52 different card faces")
        for resource in ["felt.png", "LICENSE"]:
            if not package.read(resource_root + "/" + resource):
                raise ValueError(f"missing card resource: {resource}")
    print(f"PASS: IPA integrity, device platform, scene lifecycle, native controller and 52-card deck "
          f"(version {info['CFBundleShortVersionString']}, build {info['CFBundleVersion']})")


if __name__ == "__main__":
    try:
        check(sys.argv[1])
    except (IndexError, ValueError, KeyError, OSError, zipfile.BadZipFile,
            subprocess.CalledProcessError) as error:
        sys.exit(f"IPA check failed: {error}")
