#pragma once

#include "shared.h"
#include "logger.h"

#include <atomic>

extern bool gDisableAssert;
extern bool gBreakOnAssertFailure; // DAT_00f47931

[[noreturn]] static void OnFatalError() { abort(); }

#if OTDU_MSVC
#define OTDU_TRIGGER_BREAKPOINT __debugbreak();
#elif OTDU_GCC
#include <signal.h>
#define OTDU_TRIGGER_BREAKPOINT raise( SIGTRAP );
#elif OTDU_CLANG
#define OTDU_TRIGGER_BREAKPOINT __builtin_debugtrap();
#else
#define OTDU_TRIGGER_BREAKPOINT
#endif

#ifdef OTDU_DEVBUILD
#define OTDU_ASSERT_ALWAYS( condition )                                        \
    if ( !( condition ) ) {                                                    \
        OTDU_LOG_ERROR( "Assertion failure: '%s'\n", #condition );             \
        OTDU_TRIGGER_BREAKPOINT;                                               \
    }
#define OTDU_ASSERT_IMPL( condition, logMacro )                                \
    if ( !gDisableAssert && !( condition ) ) {                                 \
        logMacro( "Assertion failure: '%s'\n", #condition );                   \
        if ( gBreakOnAssertFailure ) {                                         \
            OTDU_TRIGGER_BREAKPOINT;                                           \
        }                                                                      \
    }
#define OTDU_ASSERT_ONCE_IMPL( condition, logMacro )                           \
    do {                                                                       \
        static std::atomic_flag sAssertOnceReported = ATOMIC_FLAG_INIT;        \
        if ( !gDisableAssert && !( condition ) &&                              \
             !sAssertOnceReported.test_and_set(                                \
                 std::memory_order_relaxed ) ) {                               \
            logMacro( "Assertion failure (only reported once): '%s'\n",        \
                      #condition );                                            \
            if ( gBreakOnAssertFailure ) {                                     \
                OTDU_TRIGGER_BREAKPOINT;                                       \
            }                                                                  \
        }                                                                      \
    } while ( false )

#define OTDU_ASSERT( condition ) OTDU_ASSERT_IMPL( condition, OTDU_LOG_ERROR )
#define OTDU_ASSERT_ONCE( condition )                                          \
    OTDU_ASSERT_ONCE_IMPL( condition, OTDU_LOG_ERROR )
#define OTDU_ASSERT_TRANSIENT( condition )                                     \
    OTDU_ASSERT_IMPL( condition, OTDU_LOG_ERROR_TRANSIENT )
#define OTDU_ASSERT_ONCE_TRANSIENT( condition )                                \
    OTDU_ASSERT_ONCE_IMPL( condition, OTDU_LOG_ERROR_TRANSIENT )
#else
#define OTDU_ASSERT( condition )
#define OTDU_ASSERT_TRANSIENT( condition )
#define OTDU_ASSERT_ONCE( condition )                                          \
    do {                                                                       \
    } while ( false )
#define OTDU_ASSERT_ONCE_TRANSIENT( condition )                                \
    do {                                                                       \
    } while ( false )
#endif

#define OTDU_ASSERT_FATAL( condition )                                         \
    if ( !( condition ) ) {                                                    \
        OTDU_FATAL_ERROR( "Assertion failure: '%s'\n", #condition );           \
    }

#ifdef OTDU_DEVBUILD
#define OTDU_FATAL_ERROR( fmt, ... )                                           \
    OTDU_LOG_ERROR( "Fatal: " fmt, ##__VA_ARGS__ );                            \
    if ( gBreakOnAssertFailure ) {                                             \
        OTDU_TRIGGER_BREAKPOINT;                                               \
    }                                                                          \
    OnFatalError();
#else
#define OTDU_FATAL_ERROR( fmt, ... ) OnFatalError();
#endif
