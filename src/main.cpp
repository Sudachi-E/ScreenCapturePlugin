#include "common.h"
#include "capture_utils.h"
#include "audio_capture.h"
#include "config.h"
#include "retain_vars.hpp"
#include "ring_buffer.h"
#include "save_thread.h"
#include "utils/logger.h"
#include <coreinit/debug.h>
#include <coreinit/title.h>
#include <notifications/notifications.h>
#include <nn/acp/client.h>
#include <nn/acp/title.h>
#include <malloc.h>
#include <string>
#include <wups.h>

static void updateAppName() {
    ACPInitialize();
    ACPMetaXml *metaXml = (ACPMetaXml *) memalign(0x40, sizeof(ACPMetaXml));
    if (metaXml) {
        if (ACPGetTitleMetaXml(OSGetTitleID(), metaXml) == ACP_RESULT_SUCCESS) {
            gShortNameEn = metaXml->shortname_en;
            const char *illegal = "\\/:?\"<>|@=;`_^[]";
            for (char &c : gShortNameEn) {
                if (c < ' ' || c > '~') { c = ' '; }
                for (const char *ch = illegal; *ch; ch++) {
                    if (c == *ch) { c = ' '; break; }
                }
            }
            size_t w = 0;
            for (size_t r = 0; r < gShortNameEn.size(); r++) {
                if (gShortNameEn[r] != ' ' || (w > 0 && gShortNameEn[w - 1] != ' ')) {
                    gShortNameEn[w++] = gShortNameEn[r];
                }
            }
            if (w > 0 && gShortNameEn[w - 1] == ' ') w--;
            gShortNameEn.resize(w);
        }
        free(metaXml);
    }
    ACPFinalize();

    if (gShortNameEn.empty()) {
        gShortNameEn = "unknown";
    }
    DEBUG_FUNCTION_LINE("App name: \"%s\"\n", gShortNameEn.c_str());
}

WUPS_PLUGIN_NAME("Screen Capture");
WUPS_PLUGIN_DESCRIPTION("Capture footage directly on the Wii U.");
WUPS_PLUGIN_VERSION("v1.0.1");
WUPS_PLUGIN_AUTHOR("SudoTronics");
WUPS_PLUGIN_LICENSE("GPL");

WUPS_USE_WUT_DEVOPTAB();

	INITIALIZE_PLUGIN() {
	    initLogging();
	    NotificationModule_InitLibrary();
	    InitConfig();
	    InitNotificationModule();
	    OSReport("ScreenCapture: plugin initialized\n");
	}

DEINITIALIZE_PLUGIN() {
    gButtonComboInstances.clear();
    NotificationModule_DeInitLibrary();
    deinitLogging();
}

ON_APPLICATION_START() {
    initLogging();
    gSaveRequested       = false;
    gEnabled             = false;
    gInForeground        = true;
    gCapturing           = gEnabled;
    gTitleID             = OSGetTitleID();
    gIsInGame            = !(gTitleID);
    updateAppName();
    resetCaptureTiming();

	OSReport("ScreenCapture: app started, titleID=0x%016llX\n", OSGetTitleID());

    gRingBuffer.clear();
    gRingBufferTV.clear();

    if (gEnabled) {
        initAudioCapture();
        startEncodeThread();
        startSaveThread();
    }

	OSReport("ScreenCapture: application started\n");
}

ON_APPLICATION_REQUESTS_EXIT() {
    destroyAudioCapture();
    stopEncodeThread();
    stopSaveThread();
    deinitLogging();
	OSReport("ScreenCapture: application exited\n");
}

ON_RELEASE_FOREGROUND() {
    gInForeground = false;
    gCapturing    = false;
}

ON_ACQUIRED_FOREGROUND() {
    gInForeground = true;
    gCapturing    = gEnabled;
}

void saveTrack() {
    stopAudioCapture();

    for (int s = 0; s < 2; s++) {
        AudioSource src = (AudioSource)s;
        const int16_t *audioBuf = nullptr;
        size_t audioSamps = 0;
        uint32_t audioRate = 48000;

		getAudioData(src, &audioBuf, &audioSamps, &audioRate);

		if (audioSamps > 0) {
			for (size_t i = 0; i < 10 && i < audioSamps; i++) {
			}
		}
	}
}