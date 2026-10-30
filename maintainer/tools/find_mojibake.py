"""Finds (and with --fix repairs) double-encoded UTF-8 (UTF-8 bytes read as Latin-1 and encoded
again: '\u00c3\u00bc' for '\u00fc') in text files under the given roots."""
import os
import re
import sys

fix = "--fix" in sys.argv
roots = [a for a in sys.argv[1:] if a != "--fix"]
SKIP = {".git", "thirdparty", "generated", "out", "rom", "logs", "node_modules"}
EXT = (".cpp", ".h", ".md", ".toml", ".example", ".iss", ".txt", ".py", ".ps1", ".cmake")
# A run of characters U+0080-U+00FF that is itself valid UTF-8 once encoded as Latin-1.
pattern = re.compile("[Â-ô][\u0080-¿]+")
for root in roots:
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP]
        for f in files:
            if not f.endswith(EXT):
                continue
            p = os.path.join(dirpath, f)
            try:
                s = open(p, encoding="utf-8", newline="").read()
            except UnicodeDecodeError:
                continue
            hits = []

            def repl(m):
                try:
                    good = m.group(0).encode("latin-1").decode("utf-8")
                except (UnicodeDecodeError, UnicodeEncodeError):
                    return m.group(0)
                hits.append((m.group(0), good))
                return good
            t = pattern.sub(repl, s)
            if hits:
                print(p, hits[:6])
                if fix:
                    open(p, "w", encoding="utf-8", newline="").write(t)
