#!/usr/bin/env python3
"""Unit checks for the release tool's asset discovery, provenance, notes and helpers.

    python3 -m unittest maintainer/test/test_release.py -v

Synthetic files only: nothing is built, published or read from a real checkout.
"""
import importlib.util
import os
import pathlib
import sys
import tempfile
import types
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
MAINT = HERE.parent
sys.path.insert(0, str(MAINT))

import release  # noqa: E402
import skylanders as sk  # noqa: E402


def touch(path, mtime=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"x")
    if mtime is not None:
        os.utime(path, (mtime, mtime))


def packaged(path, version):
    """An unversioned tarball with the sidecar the packaging step writes."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"archive " + path.name.encode())
    sk.write_provenance(path, version)
    return path


class AssetDiscovery(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_linux_game_assets(self):
        packaged(self.root / "dist" / "SSARecomp-linux-amd64.tar.gz", "1.0.0")
        assets = sk.release_assets("SSARecomp", "1.0.0", ["linux"], root=self.root)
        self.assertEqual(assets.platforms, {"linux"})
        self.assertEqual(assets.arches, {"amd64"})
        self.assertEqual([p.name for p in assets], ["SSARecomp-linux-amd64.tar.gz"])

    def test_tarball_without_sidecar_is_rejected(self):
        touch(self.root / "dist" / "SSARecomp-linux-amd64.tar.gz")
        with self.assertRaises(SystemExit):
            sk.release_assets("SSARecomp", "1.0.0", ["linux"], root=self.root)

    def test_tarball_from_another_version_is_rejected(self):
        packaged(self.root / "dist" / "SSARecomp-linux-amd64.tar.gz", "0.9.0")
        with self.assertRaises(SystemExit):
            sk.release_assets("SSARecomp", "1.0.0", ["linux"], root=self.root)

    def test_changed_tarball_is_rejected(self):
        tarball = packaged(self.root / "dist" / "SSARecomp-linux-amd64.tar.gz", "1.0.0")
        with open(tarball, "ab") as f:
            f.write(b"changed after packaging")
        with self.assertRaises(SystemExit):
            sk.release_assets("SSARecomp", "1.0.0", ["linux"], root=self.root)

    def test_mtime_does_not_matter_with_a_sidecar(self):
        tarball = packaged(self.root / "dist" / "SSARecomp-linux-amd64.tar.gz", "1.0.0")
        os.utime(tarball, (1000, 1000))  # older than any version file
        assets = sk.release_assets("SSARecomp", "1.0.0", ["linux"], root=self.root)
        self.assertEqual(assets.arches, {"amd64"})

    def test_empty_set_is_rejected(self):
        with self.assertRaises(SystemExit):
            sk.release_assets("SSARecomp", "1.0.0", ["linux"], root=self.root)

    def test_mixed_os_keeps_only_present_architectures(self):
        out = self.root / "installer" / "Output"
        for name in ("CoreOfLightSetup-1.0.0.exe", "CoreOfLightPortable-1.0.0.zip"):
            touch(out / name)
        packaged(out / "CoreOfLight-linux-amd64.tar.gz", "1.0.0")
        touch(out / "CoreOfLight-1.0.0-x86_64.AppImage")
        assets = sk.release_assets(sk.HUB, "1.0.0", ["windows", "linux"], root=self.root)
        self.assertEqual(assets.platforms, {"windows", "linux"})
        self.assertEqual(assets.arches, {"amd64"})
        notes = release.footer(sk.HUB, "1.0.0", assets)
        self.assertIn("CoreOfLightSetup-1.0.0.exe", notes)
        self.assertIn("CoreOfLight-linux-amd64.tar.gz", notes)
        self.assertNotIn("CoreOfLight-linux-arm64.tar.gz", notes)

    def test_partial_linux_architecture_is_described_honestly(self):
        out = self.root / "installer" / "Output"
        packaged(out / "CoreOfLight-linux-amd64.tar.gz", "1.0.0")  # AppImage not built
        assets = sk.release_assets(sk.HUB, "1.0.0", ["linux"], root=self.root)
        self.assertEqual(assets.arches, {"amd64"})
        notes = release.footer(sk.HUB, "1.0.0", assets)
        self.assertIn("CoreOfLight-linux-amd64.tar.gz", notes)
        self.assertNotIn("AppImage", notes)

    def test_versioned_appimage_needs_no_sidecar(self):
        touch(self.root / "installer" / "Output" / "CoreOfLight-1.0.0-x86_64.AppImage")
        assets = sk.release_assets(sk.HUB, "1.0.0", ["linux"], root=self.root)
        self.assertEqual(assets.arches, {"amd64"})

    def test_unknown_platform_is_rejected(self):
        with self.assertRaises(SystemExit):
            sk.platform_list(["beos"])


class Provenance(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_roundtrip_records_version_and_digest(self):
        archive = packaged(self.root / "SSARecomp-linux-amd64.tar.gz", "1.0.0")
        self.assertEqual(sk.read_provenance(archive),
                         ("1.0.0", sk.sha256_file(archive)))
        self.assertIsNone(sk.provenance_problem(archive, "1.0.0"))

    def test_missing_sidecar_has_no_provenance(self):
        archive = self.root / "SSARecomp-linux-amd64.tar.gz"
        touch(archive)
        self.assertIsNone(sk.read_provenance(archive))
        self.assertIsNotNone(sk.provenance_problem(archive, "1.0.0"))

    def test_problem_names_the_other_version_and_a_changed_digest(self):
        archive = packaged(self.root / "SSARecomp-linux-amd64.tar.gz", "0.9.0")
        self.assertIn("0.9.0", sk.provenance_problem(archive, "1.0.0"))
        archive.write_bytes(b"not the packaged bytes")
        self.assertIn("digest", sk.provenance_problem(archive, "0.9.0"))


class BuildProvenance(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_stamps_the_freshly_built_tarball(self):
        archive = self.root / "dist" / "SSARecomp-linux-amd64.tar.gz"
        archive.parent.mkdir(parents=True)
        archive.write_bytes(b"the newly packaged archive")
        stamped = release.stamp_package_provenance("SSARecomp", self.root, "1.0.0", arch="amd64")
        self.assertEqual(stamped, ["SSARecomp-linux-amd64.tar.gz"])
        self.assertEqual(sk.read_provenance(archive), ("1.0.0", sk.sha256_file(archive)))

    def test_removes_only_this_architectures_output(self):
        amd = packaged(self.root / "dist" / "SSARecomp-linux-amd64.tar.gz", "0.9.0")
        arm = packaged(self.root / "dist" / "SSARecomp-linux-arm64.tar.gz", "0.9.0")
        release.remove_stale_package_output("SSARecomp", self.root, "amd64")
        self.assertFalse(amd.exists())
        self.assertFalse(sk.provenance_path(amd).exists())
        self.assertTrue(arm.exists())
        self.assertTrue(sk.provenance_path(arm).exists())

    def test_does_not_stamp_another_architecture(self):
        arm = packaged(self.root / "dist" / "SSARecomp-linux-arm64.tar.gz", "0.9.0")
        stamped = release.stamp_package_provenance("SSARecomp", self.root, "1.0.0", arch="amd64")
        self.assertEqual(stamped, [])
        self.assertEqual(sk.read_provenance(arm)[0], "0.9.0")

    def test_no_tarball_means_nothing_to_stamp(self):
        self.assertEqual(release.stamp_package_provenance("SSARecomp", self.root, "1.0.0", arch="amd64"), [])


class Notes(unittest.TestCase):
    def test_windows_only_footer_names_the_setup(self):
        assets = sk.Assets([pathlib.Path("CoreOfLightSetup-1.0.0.exe")], {"windows"}, set())
        notes = release.footer(sk.HUB, "1.0.0", assets)
        self.assertIn("CoreOfLightSetup-1.0.0.exe", notes)
        self.assertNotIn("CoreOfLight-linux", notes)

    def test_game_footer_lists_only_built_architectures(self):
        assets = sk.Assets([pathlib.Path("SSARecomp-linux-amd64.tar.gz")], {"linux"}, {"amd64"})
        notes = release.footer("SSARecomp", "1.0.0", assets)
        self.assertIn("SSARecomp-linux-amd64.tar.gz", notes)
        self.assertNotIn("SSARecomp-linux-arm64.tar.gz", notes)


class LeakPatterns(unittest.TestCase):
    def test_paths_match_but_prose_does_not(self):
        import re
        patterns = release.user_path_patterns("root")
        for text in ("/home/root/project", r"C:\Users\root\project", "Users/root/x"):
            self.assertTrue(any(re.search(p, text) for p in patterns), text)
        self.assertFalse(any(re.search(p, "the root directory") for p in patterns))


class Compilers(unittest.TestCase):
    def test_versioned_clang_is_found(self):
        def which(name):
            return f"/usr/bin/{name}" if name in ("clang-20", "clang++-20") else None

        with mock.patch.object(release.shutil, "which", side_effect=which), \
                mock.patch.object(release.subprocess, "run",
                                  return_value=types.SimpleNamespace(stdout="clang version 20.1.0")):
            flags = release.linux_compilers(for_games=True)
        self.assertIn("/usr/bin/clang-20", flags[0])
        self.assertIn("/usr/bin/clang++-20", flags[1])


class DiscCheckArch(unittest.TestCase):
    def load(self):
        path = MAINT.parent / "tests" / "check_discs.py"
        spec = importlib.util.spec_from_file_location("check_discs", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_unknown_architecture_is_rejected(self):
        module = self.load()
        with mock.patch.object(module.platform, "machine", return_value="riscv64"):
            with self.assertRaises(SystemExit):
                module.host_arch()


if __name__ == "__main__":
    unittest.main()
