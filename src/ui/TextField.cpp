#include "ui.hpp"
// for gVg
#include "window.hpp"


namespace rack {


void TextField::draw(NVGcontext *vg) {
	BNDwidgetState state;
	if (this == gFocusedWidget)
		state = BND_ACTIVE;
	else if (this == gHoveredWidget)
		state = BND_HOVER;
	else
		state = BND_DEFAULT;

	bndTextField(vg, 0.0, 0.0, box.size.x, box.size.y, BND_CORNER_NONE, state, -1, text.c_str(), begin, end);
	// Draw placeholder text
	if (text.empty() && state != BND_ACTIVE) {
		bndIconLabelCaret(vg, 0.0, 0.0, box.size.x, box.size.y, -1, bndGetTheme()->textFieldTheme.itemColor, 13, placeholder.c_str(), bndGetTheme()->textFieldTheme.itemColor, 0, -1);
	}
}

void TextField::onMouseDown(EventMouseDown &e) {
	end = begin = bndTextFieldTextPosition(gVg, 0.0, 0.0, box.size.x, box.size.y, -1, text.c_str(), e.pos.x, e.pos.y);
	OpaqueWidget::onMouseDown(e);
}

void TextField::onFocus(EventFocus &e) {
	begin = 0;
	end = text.size();
	e.consumed = true;
}

void TextField::onText(EventText &e) {
	std::string newText(1, (char) e.codepoint);
	insertText(newText);
	e.consumed = true;
}

void TextField::onKey(EventKey &e) {
	switch (e.key) {
#ifdef USE_SDL2
		case SDLK_BACKSPACE:
#else
		case GLFW_KEY_BACKSPACE:
#endif
			if (begin < end) {
				text.erase(begin, end - begin);
				onTextChange();
			}
			else {
				begin--;
				if (begin >= 0) {
					text.erase(begin, 1);
					onTextChange();
				}
			}
			end = begin;
			break;
#ifdef USE_SDL2
		case SDLK_DELETE:
#else
		case GLFW_KEY_DELETE:
#endif
			if (begin < end) {
				text.erase(begin, end - begin);
				onTextChange();
			}
			else {
				text.erase(begin, 1);
				onTextChange();
			}
			end = begin;
			break;
#ifdef USE_SDL2
		case SDLK_LEFT:
#else
		case GLFW_KEY_LEFT:
#endif
			if (begin < end) {
			}
			else {
				begin--;
			}
			end = begin;
			break;
#ifdef USE_SDL2
		case SDLK_RIGHT:
#else
		case GLFW_KEY_RIGHT:
#endif
			if (begin < end) {
				begin = end;
			}
			else {
				begin++;
			}
			end = begin;
			break;
#ifdef USE_SDL2
		case SDLK_HOME:
#else
		case GLFW_KEY_HOME:
#endif
			end = begin = 0;
			break;
#ifdef USE_SDL2
		case SDLK_END:
#else
		case GLFW_KEY_END:
#endif
			end = begin = text.size();
			break;
#ifdef USE_SDL2
		case SDLK_v:
#else
		case GLFW_KEY_V:
#endif
			if (windowIsModPressed()) {
#ifdef USE_SDL2
				const char *newText = SDL_GetClipboardText();
#else
				const char *newText = glfwGetClipboardString(gWindow);
#endif
				if (newText)
					insertText(newText);
			}
			break;
#ifdef USE_SDL2
		case SDLK_c:
#else
		case GLFW_KEY_C:
#endif
			if (windowIsModPressed()) {
				if (begin < end) {
					std::string selectedText = text.substr(begin, end - begin);
#ifdef USE_SDL2
					SDL_SetClipboardText(selectedText.c_str());
#else
					glfwSetClipboardString(gWindow, selectedText.c_str());
#endif
				}
			}
			break;
#ifdef USE_SDL2
		case SDLK_RETURN:
#else
		case GLFW_KEY_ENTER:
#endif
			if (multiline) {
				insertText("\n");
			}
			else {
				EventAction e;
				onAction(e);
			}
			break;
	}

	begin = clamp(begin, 0, text.size());
	end = clamp(end, 0, text.size());
	e.consumed = true;
}

void TextField::insertText(std::string newText) {
	if (begin < end)
		text.erase(begin, end - begin);
	text.insert(begin, newText);
	begin += newText.size();
	end = begin;
	onTextChange();
}


} // namespace rack
