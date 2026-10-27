# wander.py <route> <seconds> <seed> <out>: the route (title -> a level), then that many
# seconds of play: the left stick in a random direction for 0.5-3 s, attacks (x, y, b) and jumps
# (a) in between, an fps line and a "stacks" sample every 60 s (a hang shows where it sits), quit.
import random
import sys

prefix, seconds, seed, out = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
rng = random.Random(seed)
lines = [l.rstrip("\n") for l in open(prefix, encoding="utf-8") if l.strip() and not l.startswith("#")]
# The prefix's own end (quit, renderdoc) is dropped.
lines = [l for l in "; ".join(lines).split(";") if l.strip() and l.split()[0] not in ("quit", "renderdoc")]
t = 0.0
minute = 0
lines.append("log wander start")
while t < seconds:
    if t >= minute * 60:
        lines.append("log wander minute %d" % minute)
        lines.append("fps 2")
        t += 2
        minute += 1
        if minute % 3 == 0:
            lines.append("stacks 1")
    ms = rng.randint(500, 3000)
    x, y = rng.uniform(-1, 1), rng.uniform(-1, 1)
    lines.append("stick %.2f %.2f %d" % (x, y, ms))
    t += ms / 1000
    # Now and then the right trigger held (SuperChargers' vehicles accelerate with it; the vortex
    # ride only ends that way).
    if rng.random() < 0.25:
        hold = rng.randint(1500, 4000)
        lines.append("hold rt %d" % hold)
        t += hold / 1000
    for _ in range(rng.randint(0, 3)):
        lines.append("tap %s" % rng.choice(["x", "x", "y", "b", "a"]))
        lines.append("wait 0.3")
        t += 0.42
lines.append("log wander end")
lines.append("wait 2")
lines.append("quit")
open(out, "w", encoding="utf-8").write("\n".join(l.strip() for l in lines) + "\n")
print("%s: %d lines, %.0f s of play" % (out, len(lines), t))
