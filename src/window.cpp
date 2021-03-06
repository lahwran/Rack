#include "window.hpp"
#include "app.hpp"
#include "gamepad.hpp"
#include "keyboard.hpp"
#include "asset.hpp"
#include "util/color.hpp"
#include <map>
#include <queue>
#include <thread>

#include "osdialog.h"

#if (defined(__arm__) || defined(__aarch64__) || defined(ARCH_WEB))
#define NANOVG_GLES2_IMPLEMENTATION 1
#else
#define NANOVG_GL2_IMPLEMENTATION 1
#endif

#include "nanovg_gl.h"
// Hack to get framebuffer objects working on OpenGL 2 (we blindly assume the extension is supported)
#define NANOVG_FBO_VALID 1
#include "nanovg_gl_utils.h"
#define BLENDISH_IMPLEMENTATION
#include "blendish.h"
#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#ifdef ARCH_MAC
	// For CGAssociateMouseAndMouseCursorPosition
	#include <ApplicationServices/ApplicationServices.h>
#endif

#ifdef ARCH_WEB
GLFWAPI void glfwGetWindowContentScale(GLFWwindow* window, float* xscale, float* yscale) {
	double scale = EM_ASM_INT({
        return window.devicePixelRatio;
    });

    if (xscale)
    	*xscale = scale;
    if (yscale)
    	*yscale = scale;
}
#endif

namespace rack {


//TODO: where should this go?
const Vec Vec::zero = Vec(0, 0);

GLFWwindow *gWindow = NULL;
NVGcontext *gVg = NULL;
std::shared_ptr<Font> gGuiFont;
float gPixelRatio = 1.0;
float gWindowRatio = 1.0;
bool gAllowCursorLock = true;
int gGuiFrame;
Vec gMousePos;
bool gForceRMB;

std::string lastWindowTitle;


void windowSizeCallback(GLFWwindow* window, int windowWidth, int windowHeight) {
	// Get desired scaling
	float pixelRatio;
	glfwGetWindowContentScale(gWindow, &pixelRatio, NULL);
	pixelRatio = roundf(pixelRatio);
	if (pixelRatio != gPixelRatio) {
		EventZoom eZoom;
		gScene->onZoom(eZoom);
		gPixelRatio = pixelRatio;
	}

	// Get framebuffer/window ratio
	int width, height;
	glfwGetFramebufferSize(gWindow, &width, &height);
	gWindowRatio = (float)width / windowWidth;

	gScene->box.size = Vec(width, height).div(gPixelRatio);
}

void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
#ifdef ARCH_MAC
	// Ctrl-left click --> right click
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		if (glfwGetKey(gWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(gWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) {
			button = GLFW_MOUSE_BUTTON_RIGHT;
		}
	}
#endif

	if (action == GLFW_PRESS) {
		gTempWidget = NULL;
		// onMouseDown
		{
			EventMouseDown e;
			e.pos = gMousePos;
			e.button = button;
			gScene->onMouseDown(e);
			gTempWidget = e.target;
		}

		if (button == GLFW_MOUSE_BUTTON_LEFT) {
			if (gTempWidget) {
				// onDragStart
				EventDragStart e;
				gTempWidget->onDragStart(e);
			}
			gDraggedWidget = gTempWidget;

			if (gTempWidget != gFocusedWidget) {
				if (gFocusedWidget) {
					// onDefocus
					EventDefocus e;
					gFocusedWidget->onDefocus(e);
				}
				gFocusedWidget = NULL;
				if (gTempWidget) {
					// onFocus
					EventFocus e;
					gTempWidget->onFocus(e);
					if (e.consumed) {
						gFocusedWidget = gTempWidget;
					}
				}
			}
		}
		gTempWidget = NULL;

#ifdef TOUCH
		gForceRMB = false;
#endif		
	}
	else if (action == GLFW_RELEASE) {
		// onMouseUp
		gTempWidget = NULL;
		{
			EventMouseUp e;
			e.pos = gMousePos;
			e.button = button;
			gScene->onMouseUp(e);
			gTempWidget = e.target;
		}

		bool isMenuItem = dynamic_cast<MenuItem*>(gTempWidget);
		if (button == GLFW_MOUSE_BUTTON_LEFT || isMenuItem) {
			if (gDraggedWidget || isMenuItem) {
				// onDragDrop
				EventDragDrop e;
				e.origin = gDraggedWidget;
				gTempWidget->onDragDrop(e);
			}
			// gDraggedWidget might have been set to null in the last event, recheck here
			if (gDraggedWidget) {
				// onDragEnd
				EventDragEnd e;
				gDraggedWidget->onDragEnd(e);
			}
			gDraggedWidget = NULL;
			gDragHoveredWidget = NULL;
		}
		gTempWidget = NULL;
	}
}

struct MouseButtonArguments {
	GLFWwindow *window;
	int button;
	int action;
	int mods;
};

static std::queue<MouseButtonArguments> mouseButtonQueue;
void mouseButtonStickyPop() {
	if (!mouseButtonQueue.empty()) {
		MouseButtonArguments args = mouseButtonQueue.front();
		mouseButtonQueue.pop();
		mouseButtonCallback(args.window, args.button, args.action, args.mods);
	}
}

void mouseButtonStickyCallback(GLFWwindow *window, int button, int action, int mods) {
	// Defer multiple clicks per frame to future frames
	MouseButtonArguments args = {window, button, action, mods};
	mouseButtonQueue.push(args);
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
	//TODO: remove rounding but that causes some problems with scrollbars on Mac at least
	Vec mousePos = Vec(xpos, ypos).div(gPixelRatio / gWindowRatio).round();
	Vec mouseRel = mousePos.minus(gMousePos);

	if (mouseRel.isZero())
		return;

#ifdef ARCH_MAC
	int cursorMode = glfwGetInputMode(gWindow, GLFW_CURSOR);

	// Workaround for Mac. We can't use GLFW_CURSOR_DISABLED because it's buggy, so implement it on our own.
	// This is not an ideal implementation. For example, if the user drags off the screen, the new mouse position will be clamped.
	if (cursorMode == GLFW_CURSOR_HIDDEN) {
		// CGSetLocalEventsSuppressionInterval(0.0);
		glfwSetCursorPos(gWindow, gMousePos.x, gMousePos.y);
		CGAssociateMouseAndMouseCursorPosition(true);
		mousePos = gMousePos;
	}
	// Because sometimes the cursor turns into an arrow when its position is on the boundary of the window
	glfwSetCursor(gWindow, NULL);
#endif

	gMousePos = mousePos;

	gTempWidget = NULL;
	// onMouseMove
	{
		EventMouseMove e;
		e.pos = mousePos;
		e.mouseRel = mouseRel;
		gScene->onMouseMove(e);
		gTempWidget = e.target;
	}

	if (gDraggedWidget) {
		// onDragMove
		EventDragMove e;
		e.mouseRel = mouseRel;
		gDraggedWidget->onDragMove(e);

		if (gTempWidget != gDragHoveredWidget) {
			if (gDragHoveredWidget) {
				EventDragEnter e;
				e.origin = gDraggedWidget;
				gDragHoveredWidget->onDragLeave(e);
			}
			gDragHoveredWidget = gTempWidget;
			if (gDragHoveredWidget) {
				EventDragEnter e;
				e.origin = gDraggedWidget;
				gDragHoveredWidget->onDragEnter(e);
			}
		}
	}
	else {
		if (gTempWidget != gHoveredWidget) {
			if (gHoveredWidget) {
				// onMouseLeave
				EventMouseLeave e;
				gHoveredWidget->onMouseLeave(e);
			}
			gHoveredWidget = gTempWidget;
			if (gHoveredWidget) {
				// onMouseEnter
				EventMouseEnter e;
				gHoveredWidget->onMouseEnter(e);
			}
		}
	}
	gTempWidget = NULL;
	if (glfwGetMouseButton(gWindow, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
		// TODO
		// Define a new global called gScrollWidget, which remembers the widget where middle-click was first pressed
		EventScroll e;
		e.pos = mousePos;
		e.scrollRel = mouseRel;
		gScene->onScroll(e);
	}
}

void cursorEnterCallback(GLFWwindow* window, int entered) {
	if (!entered) {
		if (gHoveredWidget) {
			// onMouseLeave
			EventMouseLeave e;
			gHoveredWidget->onMouseLeave(e);
		}
		gHoveredWidget = NULL;
	}
}

void scrollCallback(GLFWwindow *window, double x, double y) {
	Vec scrollRel = Vec(x, y);
#if ARCH_LIN || ARCH_WIN
	if (windowIsShiftPressed())
		scrollRel = Vec(y, x);
#endif
	// onScroll
	EventScroll e;
	e.pos = gMousePos;
	e.scrollRel = scrollRel.mult(50.0);
	gScene->onScroll(e);
}

void charCallback(GLFWwindow *window, unsigned int codepoint) {
	if (gFocusedWidget) {
		// onText
		EventText e;
		e.codepoint = codepoint;
		gFocusedWidget->onText(e);
	}
}

void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods) {
	if (action == GLFW_PRESS || action == GLFW_REPEAT) {
		if (gFocusedWidget) {
			// onKey
			EventKey e;
			e.key = key;
			gFocusedWidget->onKey(e);
			if (e.consumed)
				return;
		}
		// onHoverKey
		EventHoverKey e;
		e.pos = gMousePos;
		e.key = key;
		gScene->onHoverKey(e);
	}

	// Keyboard MIDI driver
	// if (!(mods & (GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER))) {
	if (mods & GLFW_MOD_CAPS_LOCK)
	{
		if (action == GLFW_PRESS) {
			keyboardPress(key);
		}
		else if (action == GLFW_RELEASE) {
			keyboardRelease(key);
		}
	}	
}

void dropCallback(GLFWwindow *window, int count, const char **paths) {
	// onPathDrop
	EventPathDrop e;
	e.pos = gMousePos;
	for (int i = 0; i < count; i++) {
		e.paths.push_back(paths[i]);
	}
	gScene->onPathDrop(e);
}

void errorCallback(int error, const char *description) {
	warn("GLFW error %d: %s", error, description);
}

void renderGui() {
	int width, height;
	glfwGetFramebufferSize(gWindow, &width, &height);

	gScene->ensureCached(gVg);

	// Update and render
	nvgBeginFrame(gVg, width, height, gPixelRatio);
	nvgReset(gVg);
	nvgScale(gVg, gPixelRatio, gPixelRatio);

	gScene->draw(gVg);

	glViewport(0, 0, width, height);
	glClearColor(0.2, 0.2, 0.2, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	nvgEndFrame(gVg);
	glfwSwapBuffers(gWindow);
}

void windowInit() {
	int err;

	// Set up GLFW
	glfwSetErrorCallback(errorCallback);

	err = glfwInit();
	if (err != GLFW_TRUE) {
		osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, "Could not initialize GLFW.");
		exit(1);
	}

#if defined NANOVG_GL2
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#elif defined NANOVG_GL3
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#elif defined NANOVG_GLES2
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
	glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
	glfwWindowHint(GLFW_DEPTH_BITS, 0);
	glfwWindowHint(GLFW_ALPHA_BITS, 0);

#if (defined(__arm__) || defined(__aarch64__))
	glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

	GLFWmonitor *monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);
	int w = mode->width, h = mode->height;
#elif !defined(ARCH_WEB)
	glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

	GLFWmonitor *monitor = NULL;
	int w = 800, h = 600;
#else
	int w, h, fs;
	emscripten_get_canvas_size(&w, &h, &fs);
	GLFWmonitor *monitor = NULL;
#endif	

	lastWindowTitle = gApplicationName + " " + gApplicationVersion;
	gWindow = glfwCreateWindow(w, h, lastWindowTitle.c_str(), monitor, NULL);
	if (!gWindow) {
		osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, "Cannot open window with OpenGL 2.0 renderer. Does your graphics card support OpenGL 2.0 or greater? If so, make sure you have the latest graphics drivers installed.");
		exit(1);
	}

	glfwMakeContextCurrent(gWindow);
    info("GL_VERSION  : %s", glGetString(GL_VERSION) );
    info("GL_RENDERER : %s", glGetString(GL_RENDERER) );

#if (defined(__arm__) || defined(__aarch64__))
	glfwSwapInterval(0);
#else
	glfwSwapInterval(1);
#endif

	glfwSetInputMode(gWindow, GLFW_LOCK_KEY_MODS, 1);

	glfwSetWindowSizeCallback(gWindow, windowSizeCallback);
	glfwSetMouseButtonCallback(gWindow, mouseButtonStickyCallback);
	// Call this ourselves, but on every frame instead of only when the mouse moves
	// glfwSetCursorPosCallback(gWindow, cursorPosCallback);
	glfwSetCursorEnterCallback(gWindow, cursorEnterCallback);
	glfwSetScrollCallback(gWindow, scrollCallback);
	glfwSetCharCallback(gWindow, charCallback);
	glfwSetKeyCallback(gWindow, keyCallback);
	glfwSetDropCallback(gWindow, dropCallback);

#ifdef ARCH_WIN
	// Set up GLEW
	glewExperimental = GL_TRUE;
	err = glewInit();
	if (err != GLEW_OK) {
		osdialog_message(OSDIALOG_ERROR, OSDIALOG_OK, "Could not initialize GLEW. Does your graphics card support OpenGL 2.0 or greater? If so, make sure you have the latest graphics drivers installed.");
		exit(1);
	}

	// GLEW generates GL error because it calls glGetString(GL_EXTENSIONS), we'll consume it here.
	glGetError();
#endif

	glfwSetWindowSizeLimits(gWindow, 800, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);

	// Set up NanoVG
#if defined NANOVG_GL2
	gVg = nvgCreateGL2(NVG_ANTIALIAS);
#elif defined NANOVG_GL3
	gVg = nvgCreateGL3(NVG_ANTIALIAS);
#elif defined NANOVG_GLES2
	gVg = nvgCreateGLES2(NVG_ANTIALIAS);
#endif
	assert(gVg);

	// Set up Blendish
	gGuiFont = Font::load(assetGlobal("res/fonts/DejaVuSans.ttf"));
	bndSetFont(gGuiFont->handle);
	// bndSetIconImage(loadImage(assetGlobal("res/icons.png")));

	windowSetTheme(nvgRGB(0x33, 0x33, 0x33), nvgRGB(0xf0, 0xf0, 0xf0));

	float pixelRatio;
	glfwGetWindowContentScale(gWindow, &pixelRatio, NULL);
	gPixelRatio = roundf(pixelRatio);
}

void windowDestroy() {
	gGuiFont.reset();

#if defined NANOVG_GL2
	nvgDeleteGL2(gVg);
#elif defined NANOVG_GL3
	nvgDeleteGL3(gVg);
#elif defined NANOVG_GLES2
	nvgDeleteGLES2(gVg);
#endif	

	glfwDestroyWindow(gWindow);
	glfwTerminate();
}

#ifdef ARCH_WEB
static void webLoop() {
	gGuiFrame++;

	// Poll events
	glfwPollEvents();
	double startTime = glfwGetTime();
	{
		double xpos, ypos;
		glfwGetCursorPos(gWindow, &xpos, &ypos);
		cursorPosCallback(gWindow, xpos, ypos);
	}
	mouseButtonStickyPop();

	// Step scene
	gScene->step();

	renderGui();	
}
#endif

void windowRun() {
	assert(gWindow);
	gGuiFrame = 0;

	int windowWidth, windowHeight;
	glfwGetWindowSize(gWindow, &windowWidth, &windowHeight);
	windowSizeCallback(gWindow, windowWidth, windowHeight);

#ifndef ARCH_WEB
#if (defined(__arm__) || defined(__aarch64__))
	const double fps = 30.;
#else
	const double fps = 60.;
#endif
	double wait = 1./fps;
	while(!glfwWindowShouldClose(gWindow)) {
		gGuiFrame++;

		// Poll events
		glfwWaitEventsTimeout(wait);
		double startTime = glfwGetTime();
		{
			double xpos, ypos;
			glfwGetCursorPos(gWindow, &xpos, &ypos);
			cursorPosCallback(gWindow, xpos, ypos);
		}
		mouseButtonStickyPop();
		// gamepadStep();

		// Set window title
		/*std::string windowTitle;
		windowTitle = gApplicationName;
		windowTitle += " ";
		windowTitle += gApplicationVersion;
		if (!gRackWidget->currentPatchPath.empty()) {
			windowTitle += " - ";
			windowTitle += stringFilename(gRackWidget->currentPatchPath);
		}
		if (windowTitle != lastWindowTitle) {
			glfwSetWindowTitle(gWindow, windowTitle.c_str());
			lastWindowTitle = windowTitle;
		}*/



		// Step scene
		gScene->step();

		// Render
		bool visible = glfwGetWindowAttrib(gWindow, GLFW_VISIBLE) && !glfwGetWindowAttrib(gWindow, GLFW_ICONIFIED);
		if (visible)
			renderGui();

		/*static int frame = 0;
		static double t1, t2;
		frame++;
		t2 = glfwGetTime();
		if (t2 - t1 > 2.) {
			info("%d frames @ %f FPS", frame, frame/(t2-t1));
			frame = 0;
			t1 = t2;
		}*/

		wait = std::max(0., 1./fps - (glfwGetTime()-startTime));
	}
#else
	emscripten_set_main_loop(webLoop, 0, 0);
#endif
}

void windowClose() {
	glfwSetWindowShouldClose(gWindow, GLFW_TRUE);
}

void windowCursorLock() {
	if (gAllowCursorLock) {
#ifdef ARCH_MAC
		glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
#else
		glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
#endif
	}
}

void windowCursorUnlock() {
	if (gAllowCursorLock) {
		glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

bool windowIsModPressed() {
#ifdef ARCH_MAC
	return glfwGetKey(gWindow, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS || glfwGetKey(gWindow, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
#else
	return glfwGetKey(gWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(gWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
#endif
}

bool windowIsShiftPressed() {
	return glfwGetKey(gWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(gWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
}

Vec windowGetWindowSize() {
	int width, height;
	glfwGetWindowSize(gWindow, &width, &height);
	return Vec(width, height);
}

void windowSetWindowSize(Vec size) {
	int width = size.x;
	int height = size.y;
	glfwSetWindowSize(gWindow, width, height);
}

Vec windowGetWindowPos() {
	int x, y;
	glfwGetWindowPos(gWindow, &x, &y);
	return Vec(x, y);
}

void windowSetWindowPos(Vec pos) {
	int x = pos.x;
	int y = pos.y;
	glfwSetWindowPos(gWindow, x, y);
}

bool windowIsMaximized() {
	return glfwGetWindowAttrib(gWindow, GLFW_MAXIMIZED);
}

void windowSetTheme(NVGcolor bg, NVGcolor fg) {
	// Assume dark background and light foreground

	BNDwidgetTheme w;
	w.outlineColor = bg;
	w.itemColor = fg;
	w.innerColor = bg;
	w.innerSelectedColor = colorPlus(bg, nvgRGB(0x30, 0x30, 0x30));
	w.textColor = fg;
	w.textSelectedColor = fg;
	w.shadeTop = 0;
	w.shadeDown = 0;

	BNDtheme t;
	t.backgroundColor = colorPlus(bg, nvgRGB(0x30, 0x30, 0x30));
	t.regularTheme = w;
	t.toolTheme = w;
	t.radioTheme = w;
	t.textFieldTheme = w;
	t.optionTheme = w;
	t.choiceTheme = w;
	t.numberFieldTheme = w;
	t.sliderTheme = w;
	t.scrollBarTheme = w;
	t.tooltipTheme = w;
	t.menuTheme = w;
	t.menuItemTheme = w;

	t.sliderTheme.itemColor = bg;
	t.sliderTheme.innerColor = colorPlus(bg, nvgRGB(0x50, 0x50, 0x50));
	t.sliderTheme.innerSelectedColor = colorPlus(bg, nvgRGB(0x60, 0x60, 0x60));

	t.textFieldTheme = t.sliderTheme;
	t.textFieldTheme.textColor = colorMinus(bg, nvgRGB(0x20, 0x20, 0x20));
	t.textFieldTheme.textSelectedColor = t.textFieldTheme.textColor;

	t.scrollBarTheme.itemColor = colorPlus(bg, nvgRGB(0x50, 0x50, 0x50));
	t.scrollBarTheme.innerColor = bg;

	t.menuTheme.innerColor = colorMinus(bg, nvgRGB(0x10, 0x10, 0x10));
	t.menuTheme.textColor = colorMinus(fg, nvgRGB(0x50, 0x50, 0x50));
	t.menuTheme.textSelectedColor = t.menuTheme.textColor;

	bndSetTheme(t);
}

////////////////////
// resources
////////////////////

Font::Font(const std::string &filename) {
	handle = nvgCreateFont(gVg, filename.c_str(), filename.c_str());
	if (handle >= 0) {
		info("Loaded font %s", filename.c_str());
	}
	else {
		warn("Failed to load font %s", filename.c_str());
	}
}

Font::~Font() {
	// There is no NanoVG deleteFont() function yet, so do nothing
}

std::shared_ptr<Font> Font::load(const std::string &filename) {
	static std::map<std::string, std::weak_ptr<Font>> cache;
	auto sp = cache[filename].lock();
	if (!sp)
		cache[filename] = sp = std::make_shared<Font>(filename);
	return sp;
}

////////////////////
// Image
////////////////////

Image::Image(const std::string &filename) {
	handle = nvgCreateImage(gVg, filename.c_str(), NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY);
	if (handle > 0) {
		info("Loaded image %s", filename.c_str());
	}
	else {
		warn("Failed to load image %s", filename.c_str());
	}
}

Image::~Image() {
	// TODO What if handle is invalid?
	nvgDeleteImage(gVg, handle);
}

std::shared_ptr<Image> Image::load(const std::string &filename) {
	static std::map<std::string, std::weak_ptr<Image>> cache;
	auto sp = cache[filename].lock();
	if (!sp)
		cache[filename] = sp = std::make_shared<Image>(filename);
	return sp;
}

////////////////////
// SVG
////////////////////

SVG::SVG(const std::string &filename) {
	image = 0;

	NSVGimage *handle = nsvgParseFromFile(filename.c_str(), "px", SVG_DPI);
	if (handle) {
		info("Loaded SVG %s", filename.c_str());
		size = Vec(handle->width, handle->height);
		int w = ceil(handle->width)*2;
		int h = ceil(handle->height)*2;
		unsigned char *data = (unsigned char*)malloc(w*h*4);
		nsvgRasterize(nsvgCreateRasterizer(), handle, 0,0,2, data, w, h, w*4);
		image = nvgCreateImageRGBA(gVg, w, h, 0, data);
		nsvgDelete(handle);
	}
	else {
		warn("Failed to load SVG %s", filename.c_str());
	}
}

SVG::~SVG() {
	nvgDeleteImage(gVg, image);
}

std::shared_ptr<SVG> SVG::load(const std::string &filename) {
	static std::map<std::string, std::weak_ptr<SVG>> cache;
	auto sp = cache[filename].lock();
	if (!sp)
		cache[filename] = sp = std::make_shared<SVG>(filename);
	return sp;
}


} // namespace rack
