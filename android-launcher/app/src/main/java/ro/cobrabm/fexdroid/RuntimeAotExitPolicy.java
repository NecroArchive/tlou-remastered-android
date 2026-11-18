package ro.cobrabm.fexdroid;

/** Distinguish requested app shutdown (usually SIGTERM/143) from a native crash. */
final class RuntimeAotExitPolicy {
    private RuntimeAotExitPolicy() {}

    static boolean shouldDisableDirectLaunch(boolean nativeAot, int exitCode,
                                             boolean closingRequested) {
        return nativeAot && exitCode != 0 && !closingRequested;
    }
}
