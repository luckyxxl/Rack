#include "app.hpp"
#include "window.hpp"
#include "util/request.hpp"
#include "../ext/osdialog/osdialog.h"
#include <string.h>
#include <thread>


namespace rack {


static std::string newVersion = "";


#if defined(RELEASE)
static void checkVersion() {
	json_t *resJ = requestJson(METHOD_GET, gApiHost + "/version", NULL);

	if (resJ) {
		json_t *versionJ = json_object_get(resJ, "version");
		if (versionJ) {
			const char *version = json_string_value(versionJ);
			if (version && strlen(version) > 0 && version != gApplicationVersion) {
				newVersion = version;
			}
		}
		json_decref(resJ);
	}
}
#endif


RackScene::RackScene() {
	scrollWidget = new RackScrollWidget();
	{
		zoomWidget = new ZoomWidget();
		{
			assert(!gRackWidget);
			gRackWidget = new RackWidget();
			zoomWidget->addChild(gRackWidget);
		}
		scrollWidget->container->addChild(zoomWidget);
	}
	addChild(scrollWidget);

	gToolbar = new Toolbar();
	addChild(gToolbar);
	scrollWidget->box.pos.y = gToolbar->box.size.y;

	// Check for new version
#if defined(RELEASE)
	std::thread versionThread(checkVersion);
	versionThread.detach();
#endif
}

void RackScene::step() {
	// Resize owned descendants
	gToolbar->box.size.x = box.size.x;
	scrollWidget->box.size = box.size.minus(scrollWidget->box.pos);

	// Resize to be a bit larger than the ScrollWidget viewport
	gRackWidget->box.size = scrollWidget->box.size
		.minus(scrollWidget->container->box.pos)
		.plus(Vec(500, 500))
		.div(zoomWidget->zoom);

	Scene::step();

	zoomWidget->box.size = gRackWidget->box.size.mult(zoomWidget->zoom);

	// Version popup message
	if (!newVersion.empty()) {
		std::string versionMessage = stringf("Rack v%s is available.\n\nYou have Rack v%s.\n\nWould you like to download the new version on the website?", newVersion.c_str(), gApplicationVersion.c_str());
		if (osdialog_message(OSDIALOG_INFO, OSDIALOG_YES_NO, versionMessage.c_str())) {
			std::thread t(openBrowser, "https://vcvrack.com/");
			t.detach();
			windowClose();
		}
		newVersion = "";
	}
}

void RackScene::draw(NVGcontext *vg) {
	Scene::draw(vg);
}

void RackScene::onHoverKey(EventHoverKey &e) {
	Widget::onHoverKey(e);

	if (!e.consumed) {
		switch (e.key) {
#ifdef USE_SDL2
			case SDLK_n: {
#else
			case GLFW_KEY_N: {
#endif
				if (windowIsModPressed() && !windowIsShiftPressed()) {
					gRackWidget->reset();
					e.consumed = true;
					return;
				}
			} break;
#ifdef USE_SDL2
			case SDLK_q: {
#else
			case GLFW_KEY_Q: {
#endif
				if (windowIsModPressed() && !windowIsShiftPressed()) {
					windowClose();
					e.consumed = true;
					return;
				}
			} break;
#ifdef USE_SDL2
			case SDLK_o: {
#else
			case GLFW_KEY_O: {
#endif
				if (windowIsModPressed() && !windowIsShiftPressed()) {
					gRackWidget->openDialog();
					e.consumed = true;
					return;
				}
			} break;
#ifdef USE_SDL2
			case SDLK_s: {
#else
			case GLFW_KEY_S: {
#endif
				if (windowIsModPressed() && !windowIsShiftPressed()) {
					gRackWidget->saveDialog();
					e.consumed = true;
					return;
				}
				if (windowIsModPressed() && windowIsShiftPressed()) {
					gRackWidget->saveAsDialog();
					e.consumed = true;
					return;
				}
			} break;
		}
	}
}

void RackScene::onPathDrop(EventPathDrop &e) {
	if (e.paths.size() >= 1) {
		const std::string& firstPath = e.paths.front();
		if (extractExtension(firstPath) == "vcv") {
			gRackWidget->loadPatch(firstPath);
			e.consumed = true;
		}
	}

	if (!e.consumed)
		Scene::onPathDrop(e);
}


} // namespace rack
