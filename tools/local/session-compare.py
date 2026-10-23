"""Compare session-run.ps1 sessions (legs.csv) between two groups, leg by leg.

    python tools\\local\\session-compare.py A=sec-old-a,sec-old-b B=sec-new-a,sec-new-b

Per leg: mean fps / 1% low / p99 of each group and the B-A change; the count of legs where B is faster; the group
means; and the spread between same-group sessions as the noise floor. Local tool."""
import csv, sys, os, statistics

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '_Build', 'session')
groups = {}
for arg in sys.argv[1:]:
    name, labels = arg.split('=', 1)
    groups[name] = labels.split(',')
if len(groups) != 2:
    sys.exit('need two groups: A=l1,l2 B=l3,l4')

def load(label):
    with open(os.path.join(root, label, 'legs.csv'), newline='') as f:
        return {r['label']: r for r in csv.DictReader(f)}

data = {g: [load(l) for l in labels] for g, labels in groups.items()}
(ga, la), (gb, lb) = data.items()
legs = [l for l in la[0] if all(l in s for s in la + lb)]

def mean(sessions, leg, key):
    return statistics.fmean(float(s[leg][key]) for s in sessions)

print('%-16s %7s %7s %7s | %7s %7s %7s | %7s %7s %7s' % ('leg', ga + 'fps', 'low1', 'p99', gb + 'fps', 'low1', 'p99', 'dfps%', 'dlow1%', 'dp99%'))
wins = 0
tot = {k: [0.0, 0.0] for k in ('fps', 'low1', 'p99_ms')}
for leg in legs:
    a = {k: mean(la, leg, k) for k in tot}
    b = {k: mean(lb, leg, k) for k in tot}
    for k in tot:
        tot[k][0] += a[k]; tot[k][1] += b[k]
    wins += b['fps'] > a['fps']
    print('%-16s %7.2f %7.2f %7.2f | %7.2f %7.2f %7.2f | %+7.2f %+7.2f %+7.2f' % (
        leg, a['fps'], a['low1'], a['p99_ms'], b['fps'], b['low1'], b['p99_ms'],
        100 * (b['fps'] / a['fps'] - 1), 100 * (b['low1'] / a['low1'] - 1), 100 * (b['p99_ms'] / a['p99_ms'] - 1)))
n = len(legs)
print('mean of %d legs: fps %+.2f%%  low1 %+.2f%%  p99 %+.2f%%  (%s faster on %d/%d legs)' % (
    n, 100 * (tot['fps'][1] / tot['fps'][0] - 1), 100 * (tot['low1'][1] / tot['low1'][0] - 1),
    100 * (tot['p99_ms'][1] / tot['p99_ms'][0] - 1), gb, wins, n))
for g, sessions in data.items():
    if len(sessions) >= 2:
        f = [statistics.fmean(float(s[l]['fps']) for l in legs) for s in sessions]
        lo = [statistics.fmean(float(s[l]['low1']) for l in legs) for s in sessions]
        print('noise %s: fps %s (spread %.2f%%)  low1 %s (spread %.2f%%)' % (
            g, ' '.join('%.2f' % x for x in f), 100 * (max(f) / min(f) - 1),
            ' '.join('%.2f' % x for x in lo), 100 * (max(lo) / min(lo) - 1)))
