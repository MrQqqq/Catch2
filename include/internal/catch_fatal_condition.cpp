/*
 *  Created by Phil on 21/08/2014
 *  Copyright 2014 Two Blue Cubes Ltd. All rights reserved.
 *
 *  Distributed under the Boost Software License, Version 1.0. (See accompanying
 *  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 *
 */

#include "catch_fatal_condition.h"

#include "catch_context.h"
#include "catch_interfaces_capture.h"

namespace {
    // Report the error condition
    void reportFatal( char const * const message ) {
        Catch::getCurrentContext().getResultCapture()->handleFatalErrorCondition( Catch::StringRef::fromRaw( message ) );
    }
}

#if defined ( CATCH_PLATFORM_WINDOWS ) /////////////////////////////////////////

#  if !defined ( CATCH_CONFIG_WINDOWS_SEH )

namespace Catch {
    void FatalConditionHandler::reset() {}
}

#  else // CATCH_CONFIG_WINDOWS_SEH is defined

namespace Catch {
    struct SignalDefs { DWORD id; const char* name; };

    // There is no 1-1 mapping between signals and windows exceptions.
    // Windows can easily distinguish between SO and SigSegV,
    // but SigInt, SigTerm, etc are handled differently.
    static SignalDefs signalDefs[] = {
        { EXCEPTION_ILLEGAL_INSTRUCTION,  "SIGILL - Illegal instruction signal" },
        { EXCEPTION_STACK_OVERFLOW, "SIGSEGV - Stack overflow" },
        { EXCEPTION_ACCESS_VIOLATION, "SIGSEGV - Segmentation violation signal" },
        { EXCEPTION_INT_DIVIDE_BY_ZERO, "Divide by zero error" },
    };

    LONG CALLBACK FatalConditionHandler::handleVectoredException(PEXCEPTION_POINTERS ExceptionInfo) {
        for (auto const& def : signalDefs) {
            if (ExceptionInfo->ExceptionRecord->ExceptionCode == def.id) {
                reportFatal(def.name);
            }
        }
        // If its not an exception we care about, pass it along.
        // This stops us from eating debugger breaks etc.
        return EXCEPTION_CONTINUE_SEARCH;
    }

    FatalConditionHandler::FatalConditionHandler() {
        isSet = true;
        // 32k seems enough for Catch to handle stack overflow,
        // but the value was found experimentally, so there is no strong guarantee
        guaranteeSize = 32 * 1024;
        exceptionHandlerHandle = nullptr;
        // Register as first handler in current chain
        exceptionHandlerHandle = AddVectoredExceptionHandler(1, handleVectoredException);
        // Pass in guarantee size to be filled
        SetThreadStackGuarantee(&guaranteeSize);
    }

    void FatalConditionHandler::reset() {
        if (isSet) {
            // Unregister handler and restore the old guarantee
            RemoveVectoredExceptionHandler(exceptionHandlerHandle);
            SetThreadStackGuarantee(&guaranteeSize);
            exceptionHandlerHandle = nullptr;
            isSet = false;
        }
    }

    FatalConditionHandler::~FatalConditionHandler() {
        reset();
    }

bool FatalConditionHandler::isSet = false;
ULONG FatalConditionHandler::guaranteeSize = 0;
PVOID FatalConditionHandler::exceptionHandlerHandle = nullptr;


} // namespace Catch

#  endif // CATCH_CONFIG_WINDOWS_SEH

#else // Not Windows - assumed to be POSIX compatible //////////////////////////

#  if !defined(CATCH_CONFIG_POSIX_SIGNALS)

namespace Catch {
    void FatalConditionHandler::reset() {}
}


#  else // CATCH_CONFIG_POSIX_SIGNALS is defined

#include <signal.h>

namespace Catch {

    struct SignalDefs {
        int id;
        const char* name;
    };
    static SignalDefs signalDefs[] = {
        { SIGINT,  "SIGINT - Terminal interrupt signal" },
        { SIGILL,  "SIGILL - Illegal instruction signal" },
        { SIGFPE,  "SIGFPE - Floating point error signal" },
        { SIGSEGV, "SIGSEGV - Segmentation violation signal" },
        { SIGTERM, "SIGTERM - Termination request signal" },
        { SIGABRT, "SIGABRT - Abort (abnormal termination) signal" }
    };


    void FatalConditionHandler::handleSignal( int sig ) {
        char const * name = "<unknown signal>";
        for (auto const& def : signalDefs) {
            if (sig == def.id) {
                name = def.name;
                break;
            }
        }
        reset();
        reportFatal(name);
        raise( sig );
    }

    FatalConditionHandler::FatalConditionHandler() {
        isSet = true;
        stack_t sigStack;
        sigStack.ss_sp = altStackMem;
        sigStack.ss_size = SIGSTKSZ;
        sigStack.ss_flags = 0;
        sigaltstack(&sigStack, &oldSigStack);
        struct sigaction sa = { };

        sa.sa_handler = handleSignal;
        sa.sa_flags = SA_ONSTACK;
        for (std::size_t i = 0; i < sizeof(signalDefs)/sizeof(SignalDefs); ++i) {
            sigaction(signalDefs[i].id, &sa, &oldSigActions[i]);
        }
    }


    FatalConditionHandler::~FatalConditionHandler() {
        reset();
    }

    void FatalConditionHandler::reset() {
        if( isSet ) {
            // Set signals back to previous values -- hopefully nobody overwrote them in the meantime
            for( std::size_t i = 0; i < sizeof(signalDefs)/sizeof(SignalDefs); ++i ) {
                sigaction(signalDefs[i].id, &oldSigActions[i], nullptr);
            }
            // Return the old stack
            sigaltstack(&oldSigStack, nullptr);
            isSet = false;
        }
    }

    bool FatalConditionHandler::isSet = false;
    struct sigaction FatalConditionHandler::oldSigActions[sizeof(signalDefs)/sizeof(SignalDefs)] = {};
    stack_t FatalConditionHandler::oldSigStack = {};
    char FatalConditionHandler::altStackMem[SIGSTKSZ] = {};


} // namespace Catch

#  endif // CATCH_CONFIG_POSIX_SIGNALS

#endif // not Windows
