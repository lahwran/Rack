#include "util/common.hpp"
#include "engine.hpp"
#include "window.hpp"
#include "app.hpp"
#include "plugin.hpp"
#include "settings.hpp"
#include "asset.hpp"
#include "bridge.hpp"
#include "midi.hpp"
#include "rtmidi.hpp"
#include "keyboard.hpp"
#include "gamepad.hpp"
#include "osdialog.h"
#include "util/color.hpp"

#include <unistd.h>


using namespace rack;

extern "C" void main2() {
	pluginInit();
	engineInit();
	rtmidiInit();
#ifndef ARCH_WEB
	// bridgeInit();
	// gamepadInit();
#endif
	keyboardInit();
	windowInit();
	appInit();
	settingsLoad(assetHidden("settings.json"));

#ifndef ARCH_WEB
	// To prevent launch crashes, if Rack crashes between now and 15 seconds from now, the "skipAutosaveOnLaunch" property will remain in settings.json, so that in the next launch, the broken autosave will not be loaded.
	bool oldSkipAutosaveOnLaunch = skipAutosaveOnLaunch;
	skipAutosaveOnLaunch = true;
	settingsSave(assetHidden("settings.json"));
	skipAutosaveOnLaunch = false;
	if (oldSkipAutosaveOnLaunch && osdialog_message(OSDIALOG_INFO, OSDIALOG_YES_NO, "Rack has recovered from a crash, possibly caused by a faulty module in your patch. Would you like to clear your patch and start over?")) {
		gRackWidget->loadPatch(assetGlobal("template.vcv"));
		gRackWidget->currentPatchPath = "";
	}
	else
#endif
	if (!gRackWidget->loadPatch(assetHidden("autosave.vcv"))) {
		gRackWidget->loadPatch(assetGlobal("template.vcv"));
		gRackWidget->currentPatchPath = "";
	}

	engineStart();
	windowRun();
#ifndef ARCH_WEB
	engineStop();

	gRackWidget->savePatch(assetHidden("autosave.vcv"));
	settingsSave(assetHidden("settings.json"));
	appDestroy();
	windowDestroy();
#ifndef ARCH_WEB
	// bridgeDestroy();
#endif
	engineDestroy();
	pluginDestroy();
	loggerDestroy();
#endif
}

int main(int argc, char* argv[]) {
	randomInit();
	assetInit();
	loggerInit();

	info("Rack %s", gApplicationVersion.c_str());

	{
#if ARCH_LIN
	    char *path = realpath("/proc/self/exe", NULL);
	    if (path) {
	        *(strrchr(path, '/')+1) = 0;
			chdir(path);
			free(path);
	    }
#endif

		char *cwd = getcwd(NULL, 0);
		info("Current working directory: %s", cwd);
		free(cwd);

		info("Global directory: %s", assetGlobal("").c_str());
		info("Local directory: %s", assetLocal("").c_str());
		info("Settings directory: %s", assetHidden("").c_str());
		info("Plugins directory: %s", pluginPath().c_str());		
	}

#ifndef ARCH_WEB
	main2();
#else
	EM_ASM(
	    FS.mkdir('/work');
	    FS.mount(IDBFS, {}, '/work');
	    FS.syncfs(true, function() {
	    	if (navigator.requestMIDIAccess)
		    	navigator.requestMIDIAccess().then(function(midiAccess) {
		    		Module.midiAccess = midiAccess;
		    		ccall('main2', 'v');
		    	}, function() {});
	    	else
	    		ccall('main2', 'v');
	    });
	);

	emscripten_exit_with_live_runtime();
#endif

	return 0;
}
