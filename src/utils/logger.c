#include <stdint.h>
#include <whb/log_cafe.h>
#include <whb/log_module.h>
#include <whb/log_udp.h>

static uint32_t moduleLogInit = 0;
static uint32_t cafeLogInit   = 0;
static uint32_t udpLogInit    = 0;

void initLogging() {
    if (!(moduleLogInit = WHBLogModuleInit())) {
        cafeLogInit = WHBLogCafeInit();
        udpLogInit  = WHBLogUdpInit();
    }
}

void deinitLogging() {
    if (moduleLogInit) {
        WHBLogModuleDeinit();
        moduleLogInit = 0;
    }
    if (cafeLogInit) {
        WHBLogCafeDeinit();
        cafeLogInit = 0;
    }
    if (udpLogInit) {
        WHBLogUdpDeinit();
        udpLogInit = 0;
    }
}
