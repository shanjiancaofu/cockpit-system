import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    'boundaries', ROOT / 'scripts/dev/module_boundaries.py')
BOUNDARIES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BOUNDARIES)


class BoundariesTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in BOUNDARIES.MODULES:
            (self.root / 'cockpit' / name).mkdir(parents=True)

    def write(self, path, text):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)

    def test_declared_interface_allowed(self):
        self.write('cockpit/agent/llm/local_llm_client.h', '#pragma once\n')
        self.write('cockpit/voice/dialogue.cc',
                   '#include "cockpit/agent/llm/local_llm_client.h"\n')
        self.assertEqual(BOUNDARIES.inspect(self.root), [])

    def test_foreign_private_rejected(self):
        self.write('cockpit/agent/runtime/private.h', '#pragma once\n')
        self.write('cockpit/voice/dialogue.cc',
                   '#include "cockpit/agent/runtime/private.h"\n')
        errors = BOUNDARIES.inspect(self.root)
        self.assertTrue(any('foreign private' in item for item in errors))

    def test_interface_transitive_leak_rejected(self):
        self.write('cockpit/agent/runtime/private.h', '#pragma once\n')
        self.write('cockpit/agent/llm/local_llm_client.h',
                   '#include "cockpit/agent/runtime/private.h"\n')
        errors = BOUNDARIES.inspect(self.root)
        self.assertTrue(any('interface header leaks' in item
                            for item in errors))

    def test_relative_private_import_rejected(self):
        self.write('cockpit/agent/runtime/private.h', '#pragma once\n')
        self.write('cockpit/voice/dialogue.cc',
                   '#include "../agent/runtime/private.h"\n')
        errors = BOUNDARIES.inspect(self.root)
        self.assertTrue(any('foreign private' in item for item in errors))

    def test_extra_module_rejected(self):
        (self.root / 'cockpit/misc').mkdir()
        self.assertTrue(BOUNDARIES.inspect(self.root))

    def test_ui_capture_rejected(self):
        self.write('cockpit/driver/camera/capture/native_argus_capture.h',
                   '#pragma once\n')
        self.write('cockpit/hmi/main.cc',
                   '#include "cockpit/driver/camera/capture/'
                   'native_argus_capture.h"\n')
        errors = BOUNDARIES.inspect(self.root)
        self.assertTrue(any('HMI must consume' in item for item in errors))


if __name__ == '__main__':
    unittest.main()
