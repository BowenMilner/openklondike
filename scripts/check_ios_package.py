#!/usr/bin/env python3
"""Check the assembled device IPA, including iOS 27 scene adoption (macOS)."""
import pathlib
import plistlib
import subprocess
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
    print(f"PASS: IPA integrity, device platform, scene manifest and compiled delegate "
          f"(version {info['CFBundleShortVersionString']}, build {info['CFBundleVersion']})")


if __name__ == "__main__":
    try:
        check(sys.argv[1])
    except (IndexError, ValueError, KeyError, OSError, zipfile.BadZipFile,
            subprocess.CalledProcessError) as error:
        sys.exit(f"IPA check failed: {error}")
