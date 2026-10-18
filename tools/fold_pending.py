# Folds a parallel agent's batch records (docs/pending/<name>.md: "- [x] Batch <TAG>-<n> (...)" entries) into
# docs/decomp-progress.md before its "- [ ] Next:" line, numbered from the next free batch number.
#   python -I tools/fold_pending.py docs/pending/frontend-ui.md FE "front-end agent" [--date 2026-10-19]
# The entries' "(NOT run in the game yet; ...)" status becomes "(<TAG>-<n>; <agent>, merged <date>; ...)" pointing
# at the pending file for the game runs. The pending file is kept (its game runs and checks for the user).
# Then run tools/progress_report.py and commit.
import argparse
import datetime
import re

ap = argparse.ArgumentParser()
ap.add_argument("pending")
ap.add_argument("tag")
ap.add_argument("agent")
ap.add_argument("--date", default=datetime.date.today().isoformat())
ap.add_argument("--progress", default="docs/decomp-progress.md")
a = ap.parse_args()

lines = open(a.pending, encoding="utf-8").read().split("\n")
entry = re.compile(r"^- \[x\] Batch %s-(\d+) \((.*)$" % re.escape(a.tag))
start = next(i for i, l in enumerate(lines) if entry.match(l))
end = next((i for i in range(start, len(lines)) if lines[i].startswith("## ")), len(lines))
doc = open(a.progress, encoding="utf-8").read()
last = max(int(n) for n in re.findall(r"^- \[x\] Batch (\d+)\b", doc, re.M))

out, count = [], 0
for l in lines[start:end]:
    m = entry.match(l)
    if m:
        count += 1
        rest = re.sub(r"^NOT run in the game yet;\s*", "", m.group(2))
        l = "- [x] Batch %d (%s-%s; %s, merged %s; run in the game: %s; NOT judged by a player yet; %s" % (
            last + count, a.tag, m.group(1), a.agent, a.date, a.pending.replace("\\", "/"), rest)
    out.append(l)
body = "\n".join(out).rstrip() + "\n"
i = doc.index("- [ ] Next:")
open(a.progress, "w", encoding="utf-8", newline="\n").write(doc[:i] + body + doc[i:])
print("folded %d batches as %d-%d" % (count, last + 1, last + count))
