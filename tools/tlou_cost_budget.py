#!/usr/bin/env python3
"""Convert observed workload volumes and isolated costs into EXPLICIT CPU scenarios.

Never labels a scenario as measured device improvement. No GPU timing is inferred.
Read only runtime.log in the supplied archive, never runtime-previous.log.
"""
import argparse
import json
from pathlib import Path
import re
from statistics import mean
import zipfile


def saved_ns(case):
    return mean(a - b for a, b in zip(case['old'], case['new'], strict=True))


def cpu_ms_per_frame(saved, count):
    return saved * count / 1_000_000


def volumes(path):
    with zipfile.ZipFile(path) as archive:
        names = [n for n in archive.namelist() if n == 'runtime.log' or n.endswith('/runtime.log')]
        if len(names) != 1:
            raise ValueError('Expected exactly one runtime.log')
        lines = archive.read(names[0]).decode(errors='replace').splitlines()
    clean = []
    for i, line in enumerate(lines):
        match = re.search(r'TLOU frame summary: ([\d.]+) flips/s', line)
        if not match or 'worker-compile=0.0ms' not in line or 'pending-draws=0' not in line:
            continue
        previous = '\n'.join(lines[max(0, i-3):i])
        cpu = re.search(r'TLOU CPU critical: .*?command=([\d.]+)%', previous)
        if cpu and float(cpu[1]) >= 60:
            clean.append(float(match[1]))
    if len(clean) < 2:
        raise ValueError('Need two compilation-free heavy gameplay windows')
    fps = mean(clean[-2:])
    result = {'flips_per_second': fps, 'frame_ms': 1000/fps}
    for role in ('draw-record', 'dispatch-record'):
        pattern = rf'TLOU CPU phases {role} \(([\d.]+) s\): calls=(\d+)'
        matches = re.findall(pattern, '\n'.join(lines))
        if len(matches) < 2:
            raise ValueError(f'Need two {role} windows')
        last = [(float(seconds), int(calls)) for seconds, calls in matches[-2:]]
        rate = sum(calls for _, calls in last)/sum(seconds for seconds, _ in last)
        result[role] = {'windows': last, 'calls_per_second': rate, 'calls_per_flip': rate/fps}
    return result


def report(volume, costs):
    fps, frame_ms = volume['flips_per_second'], volume['frame_ms']
    draws = volume['draw-record']['calls_per_flip']
    dispatches = volume['dispatch-record']['calls_per_flip']
    text = [
        '# CPU cost budget from log 24', '',
        '**Status: scenario calculations, not measured device or end-to-end savings.**', '',
        f'Observed: {fps:.2f} flips/s, {frame_ms:.3f} ms/flip; approximately '
        f'{draws:.1f} draw-record calls and {dispatches:.1f} dispatch-record calls per flip.', '',
        'Phase windows and flip windows are asynchronous. Dispatches handled by HLE or native '
        'copy/clear paths can return before BindBuffers, so phase call counts overstate coverage '
        'when assigned to every binding stage. No subtraction for that is possible from this log.', '',
        '## Ring cursor: per-stage cost, not per-draw cost', '',
        'Assume TWO populated graphics stages per draw and ONE compute stage per dispatch, '
        'with every counted call reaching binding. The log does not establish those assumptions. '
        'The test measures one stage list, despite the original ns/packet label.', '',
        f'Scenario binding stages/flip = 2 × {draws:.1f} + {dispatches:.1f} = '
        f'{2*draws+dispatches:.1f}.', '',
        '| Slots / ring bindings per stage | Saved ns/stage | CPU ms/flip, draw thread | '
        f'Relative to {frame_ms:.1f} ms frame duration, NOT FPS gain |',
        '| --- | ---: | ---: | ---: |',
    ]
    for case in costs['ring_stage']:
        delta = saved_ns(case)
        ms = cpu_ms_per_frame(delta, 2*draws+dispatches)
        text.append(f"| {case['slots']} / {case['ring']} | {delta:.3f} | {ms:.4f} | {100*ms/frame_ms:.3f}% |")
    text += ['', 'Multiply each row by the fraction of eligible calls actually covered. '
             'For one populated graphics stage use a factor '
             f'{(draws+dispatches)/(2*draws+dispatches):.3f}; for five use '
             f'{(5*draws+dispatches)/(2*draws+dispatches):.3f}. '
             'These are alternative scenarios, not measured stage counts or physical bounds.', '',
             'Ring lookup runs on the DRAW recording thread (stage B). Saving its CPU time '
             'does not directly remove CPU time from the COMMAND thread (stage A). It can '
             'affect throughput only through dependencies, contention or a changed bottleneck.', '',
             '## Memory queries: coverage is not recorded', '',
             'The log has no per-thread clamp miss counts, copy sizes or crossing distribution. '
             'An actual total cannot be calculated. Hot clamp hits save approximately zero. '
             'Use the sensitivity table below; do not substitute total buffer binds for misses.', '',
             '| Case | Saved ns/call | 1,000 eligible calls/flip | 10,000 eligible calls/flip | '
             'Eligible calls/flip needed for 1 ms CPU saving |',
             '| --- | ---: | ---: | ---: | ---: |']
    for case in costs['memory']:
        delta = saved_ns(case)
        required = f'{1_000_000/delta:,.0f}' if delta > 0 else 'not achievable (no saving)'
        text.append(f"| {case['case']} | {delta:.2f} | {cpu_ms_per_frame(delta,1000):.4f} ms | "
                    f"{cpu_ms_per_frame(delta,10000):.4f} ms | {required} |")
    text += ['', 'The two copy rows are different workloads, not additive savings for one copy. '
             'Each eligible call belongs in one category. Split memory savings by CPU thread; '
             'do not sum parallel threads into critical-path time. In CollectRingBindings, '
             'clamp comes before copy and warms its backing cache. Independently cold '
             'clamp and copy timings cannot be added for that sequence.', '']
    if costs.get('sequence'):
        text += ['## Actual clamp-before-copy sequence', '',
                 'This calls the real reference/optimized clamp followed by copy, using the '
                 'clamped length, and skips copy when clamp returns zero. Timing includes '
                 'both functions and their shared cache. Counts below are sequence attempts, '
                 'not independent cold copies. 48 KiB is the requested maximum; holes and '
                 'reservations truncate it. This is a synthetic mapping distribution. The '
                 '0.15 ns hot-path difference is too small to support a material saving claim.', '',
                 '| Sequence workload | Old ns/attempt | New ns/attempt | Saved ns/attempt | '
                 'CPU ms/flip at 1,000 attempts | CPU ms/flip at 10,000 attempts |',
                 '| --- | ---: | ---: | ---: | ---: | ---: |']
        for case in costs['sequence']:
            delta = saved_ns(case)
            text.append(f"| {case['case']} | {mean(case['old']):.2f} | {mean(case['new']):.2f} | "
                        f"{delta:.2f} | {cpu_ms_per_frame(delta,1000):.4f} | {cpu_ms_per_frame(delta,10000):.4f} |")
        text += ['', 'Do not add the memory rows above to these sequence rows. They measure '
                 'overlapping work. Phone sequence frequency, sizes and thread split remain '
                 'unmeasured. These are CPU savings scenarios, not measured FPS.', '',
                 f'Sequence run: https://github.com/erickmendes22/tlou-remastered-android/actions/runs/{costs["sequence_run"]}', '']
    text += [
             '## Gate for current and future changes', '',
             'Report before approving an APK: (1) old/new absolute cost and benchmark platform; '
             '(2) actual eligible count per frame, by thread, or explicitly missing coverage; '
             '(3) saved CPU ms/frame by thread; (4) dependency/critical-path coverage; '
             '(5) regressions and invalidation cases. Unknown coverage means unquantified '
             'game impact, not zero cost and not a guessed benefit.', '',
             'Do not multiply a function percentage by whole-frame time. Do not infer '
             'an FPS gain from a reduction on a noncritical thread. Do not add overlapping '
             'sample phases or GPU waits.', '',
             f'Cost source: https://github.com/erickmendes22/tlou-remastered-android/actions/runs/{costs["source_run"]}',
             f'Platform: {costs["measurement_platform"]}.', '']
    return '\n'.join(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log_zip', type=Path)
    parser.add_argument('--costs', type=Path,
                        default=Path(__file__).resolve().parents[1]/'android/perf/log24_arm64_costs.json')
    parser.add_argument('--json', action='store_true', help='emit observed workload volumes only')
    args = parser.parse_args()
    volume = volumes(args.log_zip)
    if args.json:
        print(json.dumps(volume, indent=2))
    else:
        print(report(volume, json.loads(args.costs.read_text())))


if __name__ == '__main__':
    main()
