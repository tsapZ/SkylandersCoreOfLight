# leak_scan.py <mirror-dir> <repo> [--word W ...] [--exclude-glob GLOB]: every file version (blob)
# reachable from the branches and tags of a mirror clone (git clone --mirror into <mirror-dir>, minus
# the upstream refs of a fork with --exclude-glob 'refs/upstream/*') searched for personal words,
# in ASCII and UTF-16, and the commits' author/committer lines too. Prints path, blob and the first
# commit that has it. The words: each --word, by default the Windows user name. A check of HEAD is
# not a check of the repository: strangers can open every old file version.
import os
import re
import subprocess
import sys

mirror, repo = sys.argv[1], sys.argv[2]
exclude = sys.argv[sys.argv.index("--exclude-glob") + 1] if "--exclude-glob" in sys.argv else None
gd = f"{mirror}/{repo}.git"
WORDS = [sys.argv[i + 1].encode() for i, a in enumerate(sys.argv) if a == "--word"]
if not WORDS and os.environ.get("USERNAME"):
    WORDS = [os.environ["USERNAME"].encode()]
if not WORDS:
    sys.exit("no words: give --word")
pats = []
for w in WORDS:
    pats.append(re.compile(re.escape(w), re.I))
    pats.append(re.compile(re.escape(w.decode().encode("utf-16-le")), re.I))


def git(*a):
    return subprocess.run(["git", "--git-dir", gd, *a], capture_output=True).stdout


args = ["rev-list", "--objects", "--branches", "--tags"]
if exclude:
    args += ["--not", f"--glob={exclude}"]
objs = {}
for line in git(*args).decode("utf-8", "surrogateescape").splitlines():
    sha, _, path = line.partition(" ")
    if path:
        objs.setdefault(sha, path)

cat = subprocess.Popen(["git", "--git-dir", gd, "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
hits = []
for sha, path in objs.items():
    cat.stdin.write(sha.encode() + b"\n")
    cat.stdin.flush()
    header = cat.stdout.readline().split()
    size = int(header[2])
    body = cat.stdout.read(size)
    cat.stdout.read(1)
    if header[1] != b"blob":
        continue
    for p in pats:
        m = p.search(body)
        if m:
            ctx = body[max(0, m.start() - 40):m.end() + 40].replace(b"\x00", b"")
            hits.append((path, sha, ctx.decode("utf-8", "replace").replace("\n", " ")))
            break
cat.stdin.close()
cat.wait()

for path, sha, ctx in sorted(hits):
    first = git("log", "--all", "--format=%h %ad", "--date=short", "--reverse", "--find-object=" + sha).decode().split("\n")[0]
    print(f"{repo}: {path} (blob {sha[:7]}, first in {first}) ...{ctx[:120]}...")
idents = set(git("log", "--branches", "--tags", "--format=%an <%ae>%n%cn <%ce>", *(["--not", f"--glob={exclude}"] if exclude else [])).decode().split("\n"))
bad = [i for i in idents if i and any(re.search(re.escape(w.decode()), i, re.I) for w in WORDS)]
print(f"{repo}: {len(objs)} objects scanned, {len(hits)} file hits, identities with the name: {bad or 'none'}")
