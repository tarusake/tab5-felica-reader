"""Increase v0.10.0's draw-task stack for TAB5 and Japanese footer rendering."""
from pathlib import Path
import re

Import("env")

source = (Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
          / "M5Stack-Avatar" / "src" / "Avatar.cpp")
text = source.read_text()
pattern = r'("drawLoop",\s*/\* Name of the task \*/\s*)2048(,)'
updated, count = re.subn(pattern, r'\g<1>16384\2', text)
if count:
    source.write_text(updated)
elif not re.search(r'"drawLoop",\s*/\* Name of the task \*/\s*16384,', text):
    raise RuntimeError("Avatar draw task changed; review TAB5 stack adjustment")
