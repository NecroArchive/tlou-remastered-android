// Standalone native benchmark shim for upstream spdlog-only assertion plumbing.
// This path is NOT part of the GPU target's includes and cannot be used by APK builds.
#pragma once
#include <cstdio>
#include <cstdlib>
#define LOG_GENERIC(...) ((void)0)
#define LOG_TRACE(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_CRITICAL(...) ((void)0)
