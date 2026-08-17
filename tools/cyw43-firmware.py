"""Turn the cyw43-driver firmware header into the raw blob NuttX expects.

The Pico SDK ships the CYW43439 firmware as a C array in a header. The NuttX
board build wants the same bytes as a file, and it quietly writes a file
containing the word "dummy" when the path is missing, which produces an image
that flashes and runs but never talks to the wireless chip. Generating the blob
here keeps that from happening silently.

The output goes under build/ rather than next to the header, so that the
pico-sdk submodule stays clean. The NuttX configuration finds it through
PLAYGROUND_ROOT, which the Nix dev shell exports.
"""

import argparse
import pathlib
import re
import sys

DEFAULT_HEADER = (
    "external/pico-sdk/lib/cyw43-driver/firmware/w43439A0_7_95_49_00_combined.h"
)
DEFAULT_OUTPUT = "build/cyw43439-firmware.bin"

# The blob is a few hundred kilobytes. Anything far below that means the header
# was not what we expected, and shipping it would repeat the dummy-file failure
# in a new form.
MINIMUM_SIZE = 100 * 1024


def convert(header: pathlib.Path, output: pathlib.Path) -> int:
    text = header.read_text()
    body = text[text.index("{") + 1 : text.rindex("}")]
    data = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", body))

    if len(data) < MINIMUM_SIZE:
        raise SystemExit(
            f"error: {header} yielded only {len(data)} bytes, "
            f"expected at least {MINIMUM_SIZE}"
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    return len(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", default=DEFAULT_HEADER, type=pathlib.Path)
    parser.add_argument("--output", default=DEFAULT_OUTPUT, type=pathlib.Path)
    args = parser.parse_args()

    if not args.header.is_file():
        raise SystemExit(
            f"error: {args.header} does not exist. Fetch the nested submodule with\n"
            f"  git -C external/pico-sdk submodule update --init --depth 1 "
            f"lib/cyw43-driver"
        )

    size = convert(args.header, args.output)
    print(f"wrote {args.output} ({size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
