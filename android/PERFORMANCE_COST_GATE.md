# Required performance accounting

For every new performance candidate, report the following before proposing an APK.
The user explicitly requires absolute per-frame accounting, not isolated percentages.

1. Absolute baseline/new cost (ns per operation), benchmark platform and variability.
2. Eligible operations per displayed frame, separately for each CPU thread.
   Distinguish measured counts, synthetic distributions and missing information.
3. CPU savings per thread per frame: `saved_ns * eligible_calls_per_frame / 1e6` ms.
   The count must include only the optimized path (cache misses, crossings, etc.).
4. Critical-path relevance. A saving on draw/record/helper threads is not automatically
   a saving on the command thread or in end-to-end frame duration.
5. Regressions, correctness/ownership cases and repeated benchmark results.
6. The actual caller sequence when one operation warms another's cache. For example,
   clamp-before-copy must be benchmarked together; independently cold timings cannot
   be added as though both operations stay cold in the real path.

Every reported percentage must identify its denominator. A function becoming 36% faster
does not make a frame 36% faster. Do not add parallel CPU threads or overlapping waits.
Do not extrapolate raw sampled phase means times all calls: clock overhead and sampling
coverage bias are material in these logs.

When actual coverage is absent, give conditional scenarios and sensitivity/required-volume
calculations. Label them as scenarios, not measured phone savings, predicted FPS, hard
upper bounds, or proof of a bottleneck reduction. Unknown savings are unquantified;
they are not assumed to be zero, positive or the favorable benchmark case.

Use `tools/tlou_cost_budget.py LOG.zip` and the versioned cost inputs for the current
candidates. The saved inputs identify their native ARM64 CI job. Later candidates need
their own absolute costs and observed eligible counts. Keep the release branch unchanged
until there is a concrete performance foundation; passing isolated correctness tests
does not itself justify generating another APK.
