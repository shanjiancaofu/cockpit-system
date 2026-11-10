#!/usr/bin/env python3
"""Regenerate the review patch after editing the complete local driver. No build/install."""
import argparse
import difflib
import sys
from pathlib import Path
from bsp import BSP, LOCK, checked_sources, require_equal


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oot', type=Path, required=True, help='clean pinned NVIDIA source checkout')
    args = parser.parse_args()
    try:
        source = checked_sources(args.oot, LOCK['source_sha256'], LOCK['oot_commit'])
        original = source / 'drivers/media/i2c/nv_imx219.c'
        local = BSP / 'drivers/media/i2c/imx219'
        require_equal(source / 'drivers/media/i2c/imx219_mode_tbls.h', local / 'imx219_mode_tbls.h',
                      'mode table is intentionally unchanged; mode changes require their own reviewed patch')
        patch = ''.join(difflib.unified_diff(original.read_text().splitlines(True),
                                            (local / 'nv_imx219.c').read_text().splitlines(True),
                                            fromfile='a/drivers/media/i2c/nv_imx219.c',
                                            tofile='b/drivers/media/i2c/nv_imx219.c'))
        if not patch:
            raise RuntimeError('local driver equals vendor source; BSP enhancement patch would be empty')
        destination = BSP / 'patches/0001-imx219-test-pattern-lifecycle.patch'
        destination.write_text(patch)
        print('Updated ' + str(destination) + '; review diff, then rebuild and test. Nothing was deployed.')
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print('ERROR: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
