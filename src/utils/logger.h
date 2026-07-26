#pragma once

#include <coreinit/debug.h>
#include <string.h>
#include <whb/log.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_APP_NAME "ScreenCapture"

#define __FILENAME_X__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#define __FILENAME__   (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILENAME_X__)

#define CONSOLE_COLOR_RED    "\033[31m"
#define CONSOLE_COLOR_YELLOW "\033[33m"
#define CONSOLE_COLOR_CYAN   "\033[36m"
#define CONSOLE_COLOR_RESET  "\033[0m"

#define LOG_EX(FILENAME, FUNCTION, LINE, LOG_FUNC, LOG_COLOR, LOG_LEVEL, LINE_END, FMT, ARGS...) \
    do {                                                                                           \
        LOG_FUNC(LOG_COLOR "[%s][%s]%s@L%04d: " LOG_LEVEL "" FMT "" LINE_END,                    \
                 LOG_APP_NAME, FILENAME, FUNCTION, LINE, ##ARGS);                                  \
    } while (0)

#define LOG_EX_DEFAULT(LOG_FUNC, LOG_COLOR, LOG_LEVEL, LINE_END, FMT, ARGS...) \
    LOG_EX(__FILENAME__, __FUNCTION__, __LINE__, LOG_FUNC, LOG_COLOR, LOG_LEVEL, LINE_END, FMT, ##ARGS)

#define DEBUG_FUNCTION_LINE(FMT, ARGS...) \
    LOG_EX_DEFAULT(OSReport, "", "", "\n", FMT, ##ARGS)

#define DEBUG_FUNCTION_LINE_ERR(FMT, ARGS...) \
    LOG_EX_DEFAULT(OSReport, "", "", "\n", FMT, ##ARGS)

#define DEBUG_FUNCTION_LINE_WARN(FMT, ARGS...) \
    LOG_EX_DEFAULT(OSReport, "", "", "\n", FMT, ##ARGS)

#define DEBUG_FUNCTION_LINE_INFO(FMT, ARGS...) \
    LOG_EX_DEFAULT(OSReport, "", "", "\n", FMT, ##ARGS)

#define DEBUG_FUNCTION_LINE_VERBOSE(FMT, ARGS...) while (0)

void initLogging();
void deinitLogging();

#ifdef __cplusplus
}
#endif
