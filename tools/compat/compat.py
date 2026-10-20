# (Has a TypeScript twin, compat.ts, which the TypeScript tools use: until this file is removed - once the Python
# tools importing it are ported, docs/typescript.md - change both, and run tools/parity's cases.)
# NOT ORIGINAL - the compatibility profiles: data that makes our generated files agree with another game's or mod's
# for online play, kept apart from the converters so every such choice is listed in one place (<profile>\README.md,
# its register) and can be dropped or replaced (e.g. by a run-time translation of the network messages) later.
#
#   import compat
#   profile = compat.load("thugpro")          # or compat.load("none"): no compatibility data at all
#   profile.section("thaw_levels", "level_names")  ->  {"Z_CH": "skatepark", ...}

import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PROFILES = ("thugpro", "none")


class Profile:
    def __init__(self, name):
        self.name = name
        self._files = {}

    def file(self, tool):
        # <profile>\<tool>.json as a dict ({} for "none" or when the profile has no such file).
        if tool not in self._files:
            path = os.path.join(HERE, self.name, tool + ".json")
            data = {}
            if self.name != "none" and os.path.exists(path):
                with open(path, encoding="utf-8") as f:
                    data = json.load(f)
            self._files[tool] = data
        return self._files[tool]

    def section(self, tool, key):
        # One section of a tool's file without its "_..." notes ({} when absent).
        return {k: v for k, v in self.file(tool).get(key, {}).items() if not k.startswith("_")}


def load(name="thugpro"):
    if name not in PROFILES:
        raise ValueError("no compatibility profile %r (%s)" % (name, ", ".join(PROFILES)))
    return Profile(name)
