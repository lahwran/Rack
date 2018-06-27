#include "app.hpp"
#include "engine.hpp"
#include "plugin.hpp"
#include "window.hpp"
#include "compat/include/app.hpp"
#include "compat/include/engine.hpp"
#include "compat/include/plugin.hpp"

namespace mirack {


VCVModuleWidget::VCVModuleWidget(rack::ModuleWidget *mw) : ModuleWidget(NULL) {
	this->mw = mw;
	this->model = reinterpret_cast<Model*>(mw->model);
	box.size = Vec(mw->box.size.x, mw->box.size.y);

	auto *mp = new VCVModuleProxy(mw->module);

	for (rack::Widget *w : mw->children) {
		if (rack::Port *p = dynamic_cast<rack::Port*>(w)) {
			// p->parent->removeChild(p);
			auto *pp = new VCVPortProxy(p);
			pp->box = Rect(Vec(p->box.pos.x, p->box.pos.y), Vec(p->box.size.x, p->box.size.y));
			pp->type = (mirack::Port::PortType)((int)p->type);
			pp->portId = p->portId;
			pp->module = mp;
			addChild(pp);
		}
	}

	engineAddModule(mp);
}

VCVModuleWidget::~VCVModuleWidget() {
	// // Make sure WireWidget destructors are called *before* removing `module` from the rack.
	// disconnect();
	// // Remove and delete the Module instance
	// if (module) {
	// 	engineRemoveModule(module);
	// 	delete module;
	// 	module = NULL;
	// }
}


json_t *VCVModuleWidget::toJson() {
	mw->box.pos = rack::Vec(box.pos.x, box.pos.y);
	return mw->toJson();
}

void VCVModuleWidget::fromJson(json_t *rootJ) {
	mw->fromJson(rootJ);
	box.pos = Vec(mw->box.pos.x, mw->box.pos.y);
}

void VCVModuleWidget::disconnect() {
	for (Port *input : inputs) {
		gRackWidget->wireContainer->removeAllWires(input);
	}
	for (Port *output : outputs) {
		gRackWidget->wireContainer->removeAllWires(output);
	}
}

void VCVModuleWidget::create() {
}

void VCVModuleWidget::_delete() {
}

void VCVModuleWidget::reset() {
	mw->reset();
}

void VCVModuleWidget::randomize() {
	//TODO: broken
	mw->randomize();
}

void VCVModuleWidget::step() {
	mw->step();
}

void VCVModuleWidget::draw(NVGcontext *vg) {
	mw->draw(vg);
	// Widget::draw(vg);
	// nvgScissor(vg, 0, 0, box.size.x, box.size.y);
	// Widget::draw(vg);

	// // CPU meter
	// if (module && gPowerMeter) {
	// 	nvgBeginPath(vg);
	// 	nvgRect(vg,
	// 		0, box.size.y - 20,
	// 		55, 20);
	// 	nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.5));
	// 	nvgFill(vg);

	// 	std::string cpuText = stringf("%.0f mS", module->cpuTime * 1000.f);
	// 	nvgFontFaceId(vg, gGuiFont->handle);
	// 	nvgFontSize(vg, 12);
	// 	nvgFillColor(vg, nvgRGBf(1, 1, 1));
	// 	nvgText(vg, 10.0, box.size.y - 6.0, cpuText.c_str(), NULL);

	// 	float p = clamp(module->cpuTime, 0.f, 1.f);
	// 	nvgBeginPath(vg);
	// 	nvgRect(vg,
	// 		0, (1.f - p) * box.size.y,
	// 		5, p * box.size.y);
	// 	nvgFillColor(vg, nvgRGBAf(1, 0, 0, 1.0));
	// 	nvgFill(vg);
	// }

	// nvgResetScissor(vg);
}

void VCVModuleWidget::drawShadow(NVGcontext *vg) {
	// nvgBeginPath(vg);
	// float r = 20; // Blur radius
	// float c = 20; // Corner radius
	// Vec b = Vec(-10, 30); // Offset from each corner
	// nvgRect(vg, b.x - r, b.y - r, box.size.x - 2*b.x + 2*r, box.size.y - 2*b.y + 2*r);
	// NVGcolor shadowColor = nvgRGBAf(0, 0, 0, 0.2);
	// NVGcolor transparentColor = nvgRGBAf(0, 0, 0, 0);
	// nvgFillPaint(vg, nvgBoxGradient(vg, b.x, b.y, box.size.x - 2*b.x, box.size.y - 2*b.y, c, r, shadowColor, transparentColor));
	// nvgFill(vg);
}

void VCVModuleWidget::onMouseDown(EventMouseDown &e) {
	Widget::onMouseDown(e);
	if (e.consumed && e.target != this)
		return;

	rack::EventMouseDown e2;
	e2.pos = rack::Vec(e.pos.x, e.pos.y);
	e2.button = e.button;
	mw->onMouseDown(e2);
	e.consumed = e2.consumed;
	
	if (e2.target) {
		if (e2.target == mw)
			e.target = this;
		else
			e.target = new VCVWidgetProxy(e2.target);
	}

	if (e.consumed)
		return;

	if (e.button == 1)
		createContextMenu();
	e.consumed = true;
	e.target = this;
}

void VCVModuleWidget::onMouseMove(EventMouseMove &e) {
	OpaqueWidget::onMouseMove(e);

	// // Don't delete the VCVModuleWidget if a TextField is focused
	// if (!gFocusedWidget) {
	// 	// Instead of checking key-down events, delete the module even if key-repeat hasn't fired yet and the cursor is hovering over the widget.
	// 	if (glfwGetKey(gWindow, GLFW_KEY_DELETE) == GLFW_PRESS || glfwGetKey(gWindow, GLFW_KEY_BACKSPACE) == GLFW_PRESS) {
	// 		if (!windowIsModPressed() && !windowIsShiftPressed()) {
	// 			gRackWidget->deleteModule(this);
	// 			this->finalizeEvents();
	// 			delete this;
	// 			e.consumed = true;
	// 			return;
	// 		}
	// 	}
	// }
}

// void VCVModuleWidget::onDragStart(EventDragStart &e) {
// 	// rack::EventDragStart e2;
// 	// mw->onDragStart(e2);

// 	dragPos = gRackWidget->lastMousePos.minus(box.pos);
// }

// void VCVModuleWidget::onDragEnd(EventDragEnd &e) {
// }

// void VCVModuleWidget::onDragMove(EventDragMove &e) {
// 	// if (!gRackWidget->lockModules)
// 	{
// 		Rect newBox = box;
// 		newBox.pos = gRackWidget->lastMousePos.minus(dragPos);
// 		gRackWidget->requestModuleBoxNearest(this, newBox);
// 	}
// }

// void VCVModuleWidget::onDragEnter(EventDragEnter &e) {
// 	rack::EventDragEnter e2;
// 	mw->onDragEnter(e2);
// }

// void VCVModuleWidget::onDragLeave(EventDragEnter &e) {
// 	rack::EventDragEnter e2;
// 	mw->onDragLeave(e2);
// }

void VCVModuleWidget::appendContextMenu(Menu *menu) {
	rack::Menu *_menu = new rack::Menu();
	mw->appendContextMenu(_menu);	
}

} // namespace rack
