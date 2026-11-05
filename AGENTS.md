# TLOU performance work

Follow `android/PERFORMANCE_COST_GATE.md` for every performance candidate.
Report absolute cost saved per operation AND CPU ms saved per frame, split by thread.
Use measured eligible counts; label scenario assumptions and missing coverage explicitly.
Include regressions and benchmark platform. Test the actual call sequence when cache
warming or shared state means isolated gains cannot be added.
Do not generate an APK merely because isolated benchmarks or correctness tests pass.
