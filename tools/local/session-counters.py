"""Per-leg counters (LIVE_COUNTERS / LIVE_MEASURE in the run log) of session-run.ps1 sessions, two groups compared.

    python tools\\local\\session-counters.py A=<log>,<log> B=<log>,<log> [--keys k1,k2,...]

Prints, per key, the mean over all legs of group A and B and the change; counters are per-frame averages as the
emulator prints them. Local tool."""
import argparse, re, statistics

p = argparse.ArgumentParser()
p.add_argument('groups', nargs=2)
p.add_argument('--keys', default='render_ms,render_busy,protect_calls,protect_calls_render,reprotects,unprotects,'
               'section_split_maps,placeholder_calls,placeholder_kt,address_space_wait_kt_render,backing_lock_wait_us,'
               'window_faults,upload_bytes,table_draws,table_dispatches,gpu_map_kt,gpu_unmap_kt,remapped_views')
a = p.parse_args()
keys = a.keys.split(',')

def load(path):
    legs = {}
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.search(r'LIVE_MEASURE .*label=(\S+) .*render_ms=([\d.]+) .*render_busy=([\d.]+)%', line)
        if m:
            legs.setdefault(m[1], {}).update(render_ms=float(m[2]), render_busy=float(m[3]))
            continue
        m = re.search(r'LIVE_COUNTERS .*label=(\S+) (.*)', line)
        if m:
            legs.setdefault(m[1], {}).update((k, float(v)) for k, v in re.findall(r'(\w+)=([-\d.]+)', m[2]))
    return legs

groups = {}
for g in a.groups:
    name, paths = g.split('=', 1)
    groups[name] = [load(x) for x in paths.split(',')]
(ga, la), (gb, lb) = groups.items()
legs = [l for l in la[0] if all(l in s for s in la + lb)]
print('%-30s %12s %12s %9s' % ('counter (mean per leg)', ga, gb, 'change'))
for k in keys:
    va = [s[l][k] for s in la for l in legs if k in s[l]]
    vb = [s[l][k] for s in lb for l in legs if k in s[l]]
    if not va or not vb:
        print('%-30s missing' % k); continue
    ma, mb = statistics.fmean(va), statistics.fmean(vb)
    print('%-30s %12.3f %12.3f %+8.1f%%' % (k, ma, mb, 100 * (mb / ma - 1) if ma else 0))
