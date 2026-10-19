#include "graphics/presentation/window/hostInput.h"

#include "SDL_error.h"
#include "SDL_events.h"
#include "SDL_keyboard.h"
#include "SDL_keycode.h"
#include "SDL_mouse.h"
#include "SDL_timer.h"
#include "SDL_video.h"
#include "common/assert.h"
#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "audio-probe.h"
#include "frame-gen.h"
#include "libs/controller.h"
#include "loader/demonsSoulsWarp.h"

#include <atomic>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <thread>
#include <chrono>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>

namespace Libs::Graphics {

namespace {

struct ControlInfo {
	std::string_view name;
	uint32_t         button   = 0;
	Controller::Axis axis     = Controller::Axis::AxisMax;
	bool             positive = false;
	float            touch_x  = 0.0f;
};

static constexpr std::array CONTROL_INFO = {
    ControlInfo {"L3", Controller::PAD_BUTTON_L3},
    ControlInfo {"R3", Controller::PAD_BUTTON_R3},
    ControlInfo {"Options", Controller::PAD_BUTTON_OPTIONS},
    ControlInfo {"Up", Controller::PAD_BUTTON_UP},
    ControlInfo {"Right", Controller::PAD_BUTTON_RIGHT},
    ControlInfo {"Down", Controller::PAD_BUTTON_DOWN},
    ControlInfo {"Left", Controller::PAD_BUTTON_LEFT},
    ControlInfo {"L2", Controller::PAD_BUTTON_L2},
    ControlInfo {"R2", Controller::PAD_BUTTON_R2},
    ControlInfo {"L1", Controller::PAD_BUTTON_L1},
    ControlInfo {"R1", Controller::PAD_BUTTON_R1},
    ControlInfo {"Triangle", Controller::PAD_BUTTON_TRIANGLE},
    ControlInfo {"Circle", Controller::PAD_BUTTON_CIRCLE},
    ControlInfo {"Cross", Controller::PAD_BUTTON_CROSS},
    ControlInfo {"Square", Controller::PAD_BUTTON_SQUARE},
    ControlInfo {"TouchPad", Controller::PAD_BUTTON_TOUCH_PAD, Controller::Axis::AxisMax, false,
                 0.25f},
    ControlInfo {"TouchPadRight", Controller::PAD_BUTTON_TOUCH_PAD, Controller::Axis::AxisMax,
                 false, 0.75f},
    ControlInfo {"LeftStickLeft", 0, Controller::Axis::LeftX, false},
    ControlInfo {"LeftStickRight", 0, Controller::Axis::LeftX, true},
    ControlInfo {"LeftStickUp", 0, Controller::Axis::LeftY, false},
    ControlInfo {"LeftStickDown", 0, Controller::Axis::LeftY, true},
    ControlInfo {"RightStickLeft", 0, Controller::Axis::RightX, false},
    ControlInfo {"RightStickRight", 0, Controller::Axis::RightX, true},
    ControlInfo {"RightStickUp", 0, Controller::Axis::RightY, false},
    ControlInfo {"RightStickDown", 0, Controller::Axis::RightY, true},
};

constexpr std::size_t INVALID_CONTROL = CONTROL_INFO.size();

struct Binding {
	SDL_Keycode key          = SDLK_UNKNOWN;
	uint8_t     mouse_button = 0;
	std::size_t control      = INVALID_CONTROL;
};

// The mouse is read every 8 ms while it drives the right stick (33 ms stepped the camera visibly).
constexpr int              MOUSE_POLL_INTERVAL_MS = 8;
constexpr std::string_view MOUSE_SENSITIVITY      = "MouseSensitivity=";
// Mouse speed that deflects the stick fully at sensitivity 1 (KYTY_MOUSE_SENSITIVITY scales it down).
constexpr double MOUSE_FULL_DEFLECTION_PX_PER_S = 1200.0;

struct MouseJoystickState {
	bool     enabled   = false; // relative mode on: motion drives the right stick
	bool     output    = false; // the stick holds a mouse value (centred when the mouse rests)
	bool     wanted    = false; // enabled before the window lost focus: enabled again when it returns
	uint64_t next_poll = 0;
	uint64_t last_poll = 0;
	double   out_x     = 0; // the smoothed deflection, -127..127
	double   out_y     = 0;
};

MouseJoystickState g_mouse;

// KYTY_MOUSE_CAPTURE=0: the mouse drives the camera only after F7 (default: also from the first click in the window;
// Esc, F7 or losing the window's focus release it). KYTY_MOUSE_INVERT_Y=1: the camera's up and down swapped.
bool MouseCaptureOnClick() {
	static const bool on = [] {
		const char* value = std::getenv("KYTY_MOUSE_CAPTURE");
		return value == nullptr || value[0] != '0';
	}();
	return on;
}

bool MouseInvertY() {
	static const bool on = [] {
		const char* value = std::getenv("KYTY_MOUSE_INVERT_Y");
		return value != nullptr && value[0] == '1';
	}();
	return on;
}

// KYTY_KEYBOARD_LAYOUT=classic: the earlier layout (J/L/K/I face buttons, T/F/G/H camera, Q/E L1/R1, Shift/Ctrl
// L3/R3); the default is the PC layout below (DefaultKeyboardInput).
bool ClassicKeyboardLayout() {
	static const bool on = [] {
		const char* value = std::getenv("KYTY_KEYBOARD_LAYOUT");
		return value != nullptr && std::string_view(value) == "classic";
	}();
	return on;
}

std::size_t ControlFromName(std::string_view name) {
	const auto info = std::find_if(CONTROL_INFO.begin(), CONTROL_INFO.end(),
	                               [name](const auto& item) { return item.name == name; });
	return static_cast<std::size_t>(std::distance(CONTROL_INFO.begin(), info));
}

SDL_Keycode NormalizeKey(SDL_Keycode key) {
	switch (key) {
		case SDLK_RSHIFT: return SDLK_LSHIFT;
		case SDLK_RCTRL: return SDLK_LCTRL;
		case SDLK_RALT: return SDLK_LALT;
		case SDLK_RGUI: return SDLK_LGUI;
		default: return key;
	}
}

uint8_t MouseButtonFromName(std::string_view name) {
	static constexpr std::array buttons = {
	    std::pair {std::string_view("Mouse:Left"), uint8_t {SDL_BUTTON_LEFT}},
	    std::pair {std::string_view("Mouse:Middle"), uint8_t {SDL_BUTTON_MIDDLE}},
	    std::pair {std::string_view("Mouse:Right"), uint8_t {SDL_BUTTON_RIGHT}},
	    std::pair {std::string_view("Mouse:X1"), uint8_t {SDL_BUTTON_X1}},
	    std::pair {std::string_view("Mouse:X2"), uint8_t {SDL_BUTTON_X2}},
	};

	const auto button = std::find_if(buttons.begin(), buttons.end(),
	                                 [name](const auto& item) { return item.first == name; });
	return button != buttons.end() ? button->second : 0;
}

bool Conflicts(const Binding& first, const Binding& second) {
	return first.control == second.control ||
	       (first.key != SDLK_UNKNOWN && first.key == second.key) ||
	       (first.mouse_button != 0 && first.mouse_button == second.mouse_button);
}

class InputMap {
public:
	InputMap() {
		// KYTY_MOUSE_SENSITIVITY=<0.1..5> (the launchers' setting) stands unless the key map names one.
		if (const char* value = std::getenv("KYTY_MOUSE_SENSITIVITY"); value != nullptr && value[0] != '\0') {
			const float sensitivity = std::strtof(value, nullptr);
			m_mouse_sensitivity     = std::clamp(std::isfinite(sensitivity) ? sensitivity : 1.0f, 0.1f, 5.0f);
		}
		for (const auto& value: Config::GetKeymap()) {
			const std::string_view entry = value;
			if (entry.starts_with(MOUSE_SENSITIVITY)) {
				const float sensitivity =
				    std::strtof(value.c_str() + MOUSE_SENSITIVITY.size(), nullptr);
				m_mouse_sensitivity =
				    std::clamp(std::isfinite(sensitivity) ? sensitivity : 1.0f, 0.1f, 5.0f);
				continue;
			}
			const auto split = entry.find('=');

			Binding binding;
			if (split != std::string_view::npos) {
				binding.control       = ControlFromName(entry.substr(0, split));
				const auto host_input = entry.substr(split + 1);
				binding.mouse_button  = MouseButtonFromName(host_input);
				if (binding.mouse_button == 0) {
					binding.key = NormalizeKey(SDL_GetKeyFromName(std::string(host_input).c_str()));
				}
			}

			const bool reserved = binding.key == SDLK_ESCAPE || binding.key == SDLK_PAUSE ||
			                      binding.key == SDLK_F7 || binding.key == SDLK_F8 ||
			                      binding.key == SDLK_F9 || binding.key == SDLK_F11;
			if (binding.control == INVALID_CONTROL || reserved ||
			    (binding.key == SDLK_UNKNOWN && binding.mouse_button == 0)) {
				EXIT("Invalid input mapping: %s\n", value.c_str());
			}
			Add(binding);
		}
	}
	[[nodiscard]] bool  Custom() const { return m_size != 0; }
	[[nodiscard]] float MouseSensitivity() const { return m_mouse_sensitivity; }

	[[nodiscard]] std::size_t FindKey(int key_code) const {
		key_code = NormalizeKey(static_cast<SDL_Keycode>(key_code));
		const auto binding =
		    std::find_if(m_bindings.begin(), m_bindings.begin() + m_size,
		                 [key_code](const auto& item) { return item.key == key_code; });
		return binding != m_bindings.begin() + m_size ? binding->control : INVALID_CONTROL;
	}

	[[nodiscard]] std::size_t FindMouseButton(uint8_t mouse_button) const {
		const auto binding = std::find_if(
		    m_bindings.begin(), m_bindings.begin() + m_size,
		    [mouse_button](const auto& item) { return item.mouse_button == mouse_button; });
		return binding != m_bindings.begin() + m_size ? binding->control : INVALID_CONTROL;
	}

private:
	void Add(const Binding& binding) {
		for (std::size_t index = 0; index < m_size;) {
			if (Conflicts(m_bindings[index], binding)) {
				m_bindings[index] = m_bindings[--m_size];
			} else {
				index++;
			}
		}
		EXIT_IF(m_size >= m_bindings.size());
		m_bindings[m_size++] = binding;
	}

	std::array<Binding, CONTROL_INFO.size()> m_bindings {};
	std::size_t                              m_size              = 0;
	float                                    m_mouse_sensitivity = 1.0f;
};

const InputMap& GetInputMap() {
	static const InputMap map;
	return map;
}

void SetButton(uint32_t button, bool down) {
	if (button == Controller::PAD_BUTTON_L2) {
		Controller::SetAxis(Controller::HOST_INPUT_CONTROLLER_ID, Controller::Axis::TriggerLeft,
		                    down ? 255 : 0);
	} else if (button == Controller::PAD_BUTTON_R2) {
		Controller::SetAxis(Controller::HOST_INPUT_CONTROLLER_ID, Controller::Axis::TriggerRight,
		                    down ? 255 : 0);
	} else if (button != 0) {
		Controller::SetButton(Controller::HOST_INPUT_CONTROLLER_ID, button, down);
	}
}

void SetTouchPad(float x, bool down) {
	Controller::SetTouchPad(Controller::HOST_INPUT_CONTROLLER_ID, 0, down, x, 0.5f);
}

uint32_t DefaultKeyboardButton(int key_code) {
	const auto key = NormalizeKey(static_cast<SDL_Keycode>(key_code));
	switch (key) {
		case SDLK_UP: return Controller::PAD_BUTTON_UP;
		case SDLK_LEFT: return Controller::PAD_BUTTON_LEFT;
		case SDLK_DOWN: return Controller::PAD_BUTTON_DOWN;
		case SDLK_RIGHT: return Controller::PAD_BUTTON_RIGHT;
		case SDLK_RETURN:
		case SDLK_RETURN2: return Controller::PAD_BUTTON_OPTIONS;
		default: break;
	}
	if (ClassicKeyboardLayout()) {
		switch (key) {
			case SDLK_j: return Controller::PAD_BUTTON_CROSS;
			case SDLK_i: return Controller::PAD_BUTTON_TRIANGLE;
			case SDLK_k: return Controller::PAD_BUTTON_SQUARE;
			case SDLK_l: return Controller::PAD_BUTTON_CIRCLE;
			case SDLK_q: return Controller::PAD_BUTTON_L1;
			case SDLK_e: return Controller::PAD_BUTTON_R1;
			case SDLK_LSHIFT: return Controller::PAD_BUTTON_L3;
			case SDLK_LCTRL: return Controller::PAD_BUTTON_R3;
			default: return 0;
		}
	}
	// The PC layout: the mouse attacks and blocks (DefaultMouseButton), the left hand rolls, interacts and uses items
	// (Demon's Souls: Circle rolls and sprints, Cross interacts and confirms, Square uses the item, Triangle two-hands).
	switch (key) {
		case SDLK_SPACE: return Controller::PAD_BUTTON_CIRCLE;
		case SDLK_e: return Controller::PAD_BUTTON_CROSS;
		case SDLK_r: return Controller::PAD_BUTTON_SQUARE;
		case SDLK_f: return Controller::PAD_BUTTON_TRIANGLE;
		case SDLK_q: return Controller::PAD_BUTTON_L1;
		case SDLK_LSHIFT: return Controller::PAD_BUTTON_R2;
		case SDLK_LCTRL: return Controller::PAD_BUTTON_L2;
		case SDLK_c: return Controller::PAD_BUTTON_R3;
		case SDLK_z: return Controller::PAD_BUTTON_L3;
		case SDLK_1: return Controller::PAD_BUTTON_UP;
		case SDLK_2: return Controller::PAD_BUTTON_LEFT;
		case SDLK_3: return Controller::PAD_BUTTON_RIGHT;
		case SDLK_4: return Controller::PAD_BUTTON_DOWN;
		default: return 0;
	}
}

// Without a custom key map the mouse buttons are the DualSense's shoulders: left R1 (attack), right L1 (block),
// middle R3 (lock on), the side buttons L2 and R2.
uint32_t DefaultMouseButton(uint8_t mouse_button) {
	switch (mouse_button) {
		case SDL_BUTTON_LEFT: return Controller::PAD_BUTTON_R1;
		case SDL_BUTTON_RIGHT: return Controller::PAD_BUTTON_L1;
		case SDL_BUTTON_MIDDLE: return Controller::PAD_BUTTON_R3;
		case SDL_BUTTON_X1: return Controller::PAD_BUTTON_L2;
		case SDL_BUTTON_X2: return Controller::PAD_BUTTON_R2;
		default: return 0;
	}
}

struct StickKeys {
	bool left  = false;
	bool right = false;
	bool up    = false;
	bool down  = false;
};

void SetStickAxis(Controller::Axis axis, bool negative, bool positive) {
	int value = 128;
	if (negative && !positive) {
		value = 0;
	} else if (positive && !negative) {
		value = 255;
	}
	Controller::SetAxis(Controller::HOST_INPUT_CONTROLLER_ID, axis, value);
}

void SetControl(std::size_t control, bool down) {
	if (control == INVALID_CONTROL) {
		return;
	}

	const auto& info = CONTROL_INFO[control];
	if (info.button == Controller::PAD_BUTTON_TOUCH_PAD) {
		SetTouchPad(info.touch_x, down);
		return;
	}
	if (info.button != 0) {
		SetButton(info.button, down);
		return;
	}

	struct AxisKeys {
		bool negative = false;
		bool positive = false;
	};
	static std::array<AxisKeys, 4> axes;

	const auto axis = static_cast<std::size_t>(info.axis);
	EXIT_IF(axis >= axes.size());
	if (info.positive) {
		axes[axis].positive = down;
	} else {
		axes[axis].negative = down;
	}
	SetStickAxis(info.axis, axes[axis].negative, axes[axis].positive);
}

void DefaultKeyboardInput(int key_code, bool down) {
	static StickKeys left;
	static StickKeys right;

	// The camera keys: I/J/K/L (classic: T/F/G/H).
	const bool classic = ClassicKeyboardLayout();
	const auto key     = NormalizeKey(static_cast<SDL_Keycode>(key_code));
	const auto cam_up = classic ? SDLK_t : SDLK_i, cam_down = classic ? SDLK_g : SDLK_k,
	           cam_left = classic ? SDLK_f : SDLK_j, cam_right = classic ? SDLK_h : SDLK_l;
	if (key == cam_up || key == cam_down || key == cam_left || key == cam_right) {
		if (key == cam_up) right.up = down;
		if (key == cam_down) right.down = down;
		if (key == cam_left) right.left = down;
		if (key == cam_right) right.right = down;
		if (key == cam_up || key == cam_down) SetStickAxis(Controller::Axis::RightY, right.up, right.down);
		if (key == cam_left || key == cam_right) SetStickAxis(Controller::Axis::RightX, right.left, right.right);
		return;
	}
	switch (key) {
		case SDLK_BACKSPACE: SetTouchPad(0.25f, down); return;
		case SDLK_TAB: SetTouchPad(0.75f, down); return;
		case SDLK_a:
			left.left = down;
			SetStickAxis(Controller::Axis::LeftX, left.left, left.right);
			return;
		case SDLK_d:
			left.right = down;
			SetStickAxis(Controller::Axis::LeftX, left.left, left.right);
			return;
		case SDLK_w:
			left.up = down;
			SetStickAxis(Controller::Axis::LeftY, left.up, left.down);
			return;
		case SDLK_s:
			left.down = down;
			SetStickAxis(Controller::Axis::LeftY, left.up, left.down);
			return;
		default: SetButton(DefaultKeyboardButton(key_code), down); return;
	}
}

// The right stick follows the mouse's speed: full deflection at MOUSE_FULL_DEFLECTION_PX_PER_S / sensitivity, in
// proportion below it (the earlier mapping put any motion at half deflection or more, so the camera lurched), smoothed
// over the last polls so an 8 ms read does not flicker, and back to centre once the mouse rests.
void MouseToJoystick(int delta_x, int delta_y, double dt_ms) {
	const double full = MOUSE_FULL_DEFLECTION_PX_PER_S / GetInputMap().MouseSensitivity();
	const double gain = 127.0 / full * 1000.0 / std::max(dt_ms, 1.0); // px per poll -> deflection
	const double tx   = std::clamp(delta_x * gain, -127.0, 127.0);
	const double ty   = std::clamp((MouseInvertY() ? -delta_y : delta_y) * gain, -127.0, 127.0);
	g_mouse.out_x     = g_mouse.out_x * 0.4 + tx * 0.6;
	g_mouse.out_y     = g_mouse.out_y * 0.4 + ty * 0.6;
	Controller::SetRightStick(Controller::HOST_INPUT_CONTROLLER_ID,
	                          std::clamp(128 + static_cast<int>(std::lround(g_mouse.out_x)), 0, 255),
	                          std::clamp(128 + static_cast<int>(std::lround(g_mouse.out_y)), 0, 255));
	g_mouse.output = true;
}

void CenterMouseStick() {
	if (!g_mouse.output) {
		return;
	}
	Controller::SetRightStick(Controller::HOST_INPUT_CONTROLLER_ID, 128, 128);
	g_mouse.output = false;
	g_mouse.out_x = g_mouse.out_y = 0;
}

bool SetRelativeMouseMode(bool enabled) {
	if (SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) == 0) {
		return true;
	}
	LOGF("Mouse-to-joystick relative mode failed: %s\n", SDL_GetError());
	return false;
}
} // namespace

// KYTY_INPUT_SCRIPT="<start s>:<every s>:<for s>:<control>[:<hold ms>][;...]": synthetic pad
// input for unattended runs (keyboard injection needs window focus, which Windows denies after
// the user has been idle). From <start> for <for> seconds, press <control> (a DualSense control
// name: Cross, Options, LeftStickUp, ...) for <hold> ms (default 150) every <every> seconds.
// "20:3:180:Cross" walks the title screens; "240:12:120:LeftStickUp:5000" walks forward.
// KYTY_INPUT_SCRIPT_START=<file>: a step whose <start> begins with '@' counts from the moment that
// file appears (the harness creates it once the level has loaded and settled) instead of from
// launch, and every step without '@' ends at that moment (no more menu presses in the level).
// KYTY_INPUT_SCRIPT_STOP=<file>: the steps without '@' also end when this file appears (the
// harness creates it as soon as the level loads, before it measures and starts the walk).
// A step whose hold is followed by ":fog" (e.g. "20:3:900:Cross:150:fog") ends by itself: once
// the game has run its title (25+ fps for 2 s after 20 s) the step presses eight more times at
// most (intro, title, Continue, and slack) and stops, or earlier when a loading screen follows
// (15 fps or less for 3 s). The presses stop before the level is up, whatever a build's start-up
// takes and however short its loading screen is.
// "20:3:600:Cross;@0:12:120:LeftStickUp:5000" presses Cross until the level is up, then walks.
std::atomic<uint64_t> g_presented_frames {0};
void HostInputNotePresentedFrame() {
	g_presented_frames.fetch_add(1, std::memory_order_relaxed);
}

static std::size_t ControlIndex(std::string_view name) {
	for (std::size_t i = 0; i < CONTROL_INFO.size(); i++) {
		if (CONTROL_INFO[i].name == name) return i;
	}
	return CONTROL_INFO.size();
}

static void StartScriptedInput() {
	const char* script = std::getenv("KYTY_INPUT_SCRIPT");
	if (script == nullptr || script[0] == 0) {
		return;
	}
	struct Step {
		double      start = 0, every = 3, duration = 0;
		std::size_t control = 0;
		uint32_t    hold_ms = 150;
		bool        from_start_file = false; // '@': the clock starts when the start file appears
		bool        until_fog       = false; // ":fog": ends once the title ran and a loading screen followed
	};
	const char*       start_env = std::getenv("KYTY_INPUT_SCRIPT_START");
	const std::string start_file(start_env != nullptr ? start_env : "");
	const char*       stop_env = std::getenv("KYTY_INPUT_SCRIPT_STOP");
	const std::string stop_file(stop_env != nullptr ? stop_env : "");
	std::vector<Step> steps;
	std::string       text(script);
	size_t            pos = 0;
	while (pos < text.size()) {
		const auto end  = text.find(';', pos);
		auto       item = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
		pos             = end == std::string::npos ? text.size() : end + 1;
		Step   step;
		if (!item.empty() && item[0] == '@') {
			step.from_start_file = true;
			item.erase(0, 1);
		}
		size_t a = item.find(':'), b = item.find(':', a + 1), c = item.find(':', b + 1);
		if (a == std::string::npos || b == std::string::npos || c == std::string::npos) continue;
		const size_t e    = item.find(':', c + 1);
		step.start        = std::atof(item.substr(0, a).c_str());
		step.every        = std::max(0.2, std::atof(item.substr(a + 1, b - a - 1).c_str()));
		step.duration     = std::atof(item.substr(b + 1, c - b - 1).c_str());
		const auto name   = item.substr(c + 1, e == std::string::npos ? std::string::npos : e - c - 1);
		step.control      = ControlIndex(name);
		if (e != std::string::npos) {
			auto rest = item.substr(e + 1);
			if (const auto f = rest.find(":fog"); f != std::string::npos) {
				step.until_fog = true;
				rest.erase(f);
			}
			if (!rest.empty()) step.hold_ms = static_cast<uint32_t>(std::max(1, std::atoi(rest.c_str())));
		}
		if (step.control < CONTROL_INFO.size()) steps.push_back(step);
	}
	if (steps.empty()) {
		return;
	}
	std::thread([steps, start_file, stop_file] {
		const auto t0 = std::chrono::steady_clock::now();
		std::vector<double> next(steps.size());
		for (size_t i = 0; i < steps.size(); i++) next[i] = steps[i].start;
		bool   started = false; // the start file has appeared
		bool   stopped = false; // the stop file has appeared
		bool   fogged  = false; // the title ran, then a loading screen (":fog")
		double t_start = 0;     // when the start file appeared
		int    polls   = 0;
		// Frame-rate watch for ":fog": presented frames over the last second.
		uint64_t frames_before = g_presented_frames.load(std::memory_order_relaxed);
		double   t_rate = 0, title_s = 0, fog_s = 0, level_s = 0;
		bool     title_seen = false;
		// The level: the G-buffer vertex shaders drawn every second for 5 s (3D gameplay; the loading
		// screen's frame rate no longer tells it from the level since the warm-up made loads fast).
		uint64_t gameplay_before = FrameGen::g_gameplay_draws.load(std::memory_order_relaxed);
		static const bool trace = [] {
			const char* value = std::getenv("KYTY_INPUT_SCRIPT_TRACE");
			return value != nullptr && value[0] == '1';
		}();
		int      presses_after_title = 0;
		const auto seen = [](const std::string& path) {
			if (path.empty()) return false;
			FILE* f = std::fopen(path.c_str(), "rb");
			if (f == nullptr) return false;
			std::fclose(f);
			std::remove(path.c_str());
			return true;
		};
		for (;;) {
			const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			if (t - t_rate >= 1.0) {
				const auto frames_now = g_presented_frames.load(std::memory_order_relaxed);
				const auto fps        = static_cast<double>(frames_now - frames_before) / (t - t_rate);
				frames_before         = frames_now;
				t_rate                = t;
				title_s               = (fps >= 25 && t >= 20) ? title_s + 1 : 0; // a title with a warm-up behind it
				if (title_s >= 2) title_seen = true;
				fog_s = (title_seen && fps <= 15 && t >= 40) ? fog_s + 1 : 0;
				if (!fogged && title_seen && fog_s >= 3) {
					fogged = true;
					std::printf("HostInput: scripted input saw the loading screen at %.0f s\n", t);
					std::fflush(stdout);
				}
				const auto gameplay_now = FrameGen::g_gameplay_draws.load(std::memory_order_relaxed);
				const auto gameplay     = gameplay_now - gameplay_before;
				gameplay_before         = gameplay_now;
				level_s                 = (gameplay >= 20 && fps >= 10) ? level_s + 1 : 0;
				if (!fogged && title_seen && level_s >= 5) {
					fogged = true;
					std::printf("HostInput: scripted input saw the level at %.0f s\n", t);
					std::fflush(stdout);
				}
				if (trace) {
					std::printf("HostInput: t=%.0f fps=%.0f gameplay_draws=%llu title=%d fog=%d\n", t, fps,
					            static_cast<unsigned long long>(gameplay), title_seen ? 1 : 0, fogged ? 1 : 0);
					std::fflush(stdout);
				}
			}
			if ((++polls % 5) == 0) {
				if (!started && seen(start_file)) {
					started = true;
					t_start = t;
					std::printf("HostInput: scripted input start file seen at %.0f s\n", t);
					std::fflush(stdout);
				}
				if (!stopped && seen(stop_file)) {
					stopped = true;
					std::printf("HostInput: scripted input stop file seen at %.0f s\n", t);
					std::fflush(stdout);
				}
			}
			bool pending = false;
			for (size_t i = 0; i < steps.size(); i++) {
				const auto& s = steps[i];
				if (s.from_start_file) {
					if (!start_file.empty() && !started) {
						pending = true; // waiting for the level
						continue;
					}
					const double ts = t - t_start;
					if (ts > s.start + s.duration) continue;
					pending = true;
					if (ts >= next[i]) {
						AudioProbe::NotePress(CONTROL_INFO[s.control].name);
						SetControl(s.control, true);
						SDL_Delay(s.hold_ms);
						SetControl(s.control, false);
						next[i] = ts + s.every + s.hold_ms / 1000.0;
					}
					continue;
				}
				if (started || stopped || (s.until_fog && (fogged || presses_after_title >= 200)) ||
				    t > s.start + s.duration)
					continue;
				pending = true;
				if (t >= next[i]) {
					SetControl(s.control, true);
					SDL_Delay(s.hold_ms);
					SetControl(s.control, false);
					next[i] = t + s.every + s.hold_ms / 1000.0;
					if (s.until_fog && title_seen && ++presses_after_title >= 200) {
						std::printf("HostInput: scripted menu presses done at %.0f s\n", t);
						std::fflush(stdout);
					}
				}
			}
			if (!pending) return;
			SDL_Delay(100);
		}
	}).detach();
	LOGF("HostInput: scripted input active (%zu step(s))\n", steps.size());
}

// With an input tape recording or replaying: "level" marks (Controller::NoteMarker) whenever gameplay is drawn again
// after two seconds or more without it (a load, a menu, a cutscene), so a replay realigns its tape there (a mark the
// live run never reaches is let go after 20 s: controller.cpp).
// The track beside a tape (<tape>.track: "tape seconds x y z yaw" twice a second while a player exists): a replay that
// has drifted from the recorded position by KYTY_TAPE_TRACK_M metres (default 2; 0: off) is glided back onto it
// (DemonsSoulsWarp::Glide), so a step a frame off does not compound into a wall.
struct TrackPoint {
	double t = 0, x = 0, y = 0, z = 0;
};

static void FollowTrack(const std::vector<TrackPoint>& track, size_t& cursor, double max_m) {
	if (track.empty() || max_m <= 0 || !Libs::Controller::ReplayPlaying()) return;
	const double t = Libs::Controller::TapeTimeUs() / 1e6;
	if (t <= 0) return;
	while (cursor + 1 < track.size() && track[cursor + 1].t <= t) cursor++;
	if (cursor + 1 >= track.size() || track[cursor].t > t) return;
	const auto&  a = track[cursor];
	const auto&  b = track[cursor + 1];
	const double f = (t - a.t) / std::max(b.t - a.t, 1e-3);
	const double tx = a.x + (b.x - a.x) * f, ty = a.y + (b.y - a.y) * f, tz = a.z + (b.z - a.z) * f;
	float xyz[3] {};
	if (!Loader::DemonsSoulsWarp::PlayerPosition(xyz, nullptr)) return;
	const double d = std::sqrt((tx - xyz[0]) * (tx - xyz[0]) + (ty - xyz[1]) * (ty - xyz[1]) + (tz - xyz[2]) * (tz - xyz[2]));
	if (d < max_m || d > 60.0) return; // (60 m or more: another place entirely, a load the track is not at; left alone)
	const auto seconds = Loader::DemonsSoulsWarp::Glide(static_cast<float>(tx), static_cast<float>(ty), static_cast<float>(tz), 8.0f);
	std::printf("Input replay: track: %.1f m off at %.1f s, glided back (%.2f s)\n", d, t, seconds);
	std::fflush(stdout);
}

static void StartTapeMarkerWatch() {
	const char* record = std::getenv("KYTY_INPUT_RECORD");
	const char* replay = std::getenv("KYTY_INPUT_REPLAY");
	if (record == nullptr && replay == nullptr) return;
	const double max_m = [] {
		const char* value = std::getenv("KYTY_TAPE_TRACK_M");
		return value != nullptr && value[0] != '\0' ? std::atof(value) : 2.0;
	}();
	std::vector<TrackPoint> track;
	if (replay != nullptr && replay[0] != '\0') {
		if (FILE* f = std::fopen((std::string(replay) + ".track").c_str(), "rb"); f != nullptr) {
			char line[160];
			while (std::fgets(line, sizeof(line), f) != nullptr) {
				TrackPoint p;
				if (std::sscanf(line, "%lf %lf %lf %lf", &p.t, &p.x, &p.y, &p.z) == 4) track.push_back(p);
			}
			std::fclose(f);
			std::printf("Input replay: %zu track point(s)\n", track.size());
		}
	}
	std::thread([record = std::string(record != nullptr ? record : ""), track = std::move(track), max_m] {
		uint64_t before  = FrameGen::g_gameplay_draws.load(std::memory_order_relaxed);
		int      none    = 0;
		bool     playing = false;
		size_t   cursor  = 0;
		for (;;) {
			SDL_Delay(500);
			const auto now = FrameGen::g_gameplay_draws.load(std::memory_order_relaxed);
			const bool drawn = now - before >= 10;
			before           = now;
			if (drawn) {
				if (!playing && none >= 4) Libs::Controller::NoteMarker("level");
				playing = true;
				none    = 0;
			} else if (++none >= 4) {
				playing = false;
			}
			if (!record.empty() && !Libs::Controller::RecordingTapePath().empty()) {
				float xyz[3] {}, yaw = 0;
				if (Loader::DemonsSoulsWarp::PlayerPosition(xyz, &yaw)) {
					if (FILE* f = std::fopen((record + ".track").c_str(), "ab"); f != nullptr) {
						std::fprintf(f, "%.3f %.2f %.2f %.2f %.3f\n", Libs::Controller::TapeTimeUs() / 1e6, xyz[0], xyz[1], xyz[2], yaw);
						std::fclose(f);
					}
				}
			}
			FollowTrack(track, cursor, max_m);
		}
	}).detach();
}

// ---- route walker ----------------------------------------------------------------------------------------------------
// KYTY_ROUTE=<file.track> (or the live `route <file>`): the character walks a player's recorded path (a tape's track,
// above) with stick input, as the player did: the left stick forward, the camera turned toward the path ahead with the
// right stick. A replayed tape gives the game the recorded presses at the recorded times, but the game is not
// frame-exact (its step follows the frame time), so a replay drifts within a minute and walks into walls; a glide
// (DemonsSoulsWarp::Glide) stays on the path but moves the player by warp requests, without the walk, its animation or
// the camera a player has. The walker follows the path by position instead and corrects whatever the frame times did.
// The camera's yaw is not read: as a player does, it turns the camera until the character, walking camera-forward,
// faces the waypoint about 3 m ahead (the turn's direction is learned from the first turns; KYTY_ROUTE_TURN_SIGN=1/-1
// fixes it; KYTY_ROUTE_TURN_GAIN, default 1: the full stick at 45 degrees off). A character standing still does not
// turn with the camera, so far off (70 degrees and more) it walks slowly while the camera comes round. Stuck (under
// 0.5 m in 2 s): a roll and a camera wiggle, again at 4 s; at 6 s a glide to the waypoint (counted). The tape beside
// the track (the track's name without .track) gives the Cross presses: each is pressed again where the player was when
// pressing it (doors, levers, fog walls, items). After a load (the player gone, or 30 m away at once) the walk resumes
// at the nearest waypoint ahead. KYTY_ROUTE_LOOP=1: at the end, glided back to the start and walked again (a route
// without loads). Once a second a "Route:" line (the waypoint, metres off the path, the heading error) for calibrating.
namespace {
struct RouteWaypoint {
	double t = 0, x = 0, y = 0, z = 0;
	double speed   = 0; // m/s the player went from the previous waypoint (0: a part's first)
	int    segment = 0; // the part of the route between loads (points 30 m or more apart)
};
struct RouteInteraction {
	double x = 0, y = 0, z = 0;
	size_t after = 0; // the waypoint the player had passed last when pressing
};
struct RouteData {
	std::string                   path;
	std::vector<RouteWaypoint>    waypoints;
	std::vector<RouteInteraction> interactions;
	double                        full_speed = 1; // the recording's running speed (its 90th percentile)
};

// A new route or `route off` moves it on: the running walker sees another generation and ends.
std::atomic<uint64_t> g_route_generation {0};
constexpr double      RoutePi = 3.14159265358979323846;

double RouteWrap(double radians) {
	while (radians > RoutePi) radians -= 2 * RoutePi;
	while (radians <= -RoutePi) radians += 2 * RoutePi;
	return radians;
}
// The body's yaw for a direction on the ground (y is up): DemonsSoulsWarp::Glide's convention.
double RouteHeading(double dx, double dz) {
	return std::atan2(-dx, -dz);
}
double RouteFlat(double ax, double az, double bx, double bz) {
	return std::hypot(ax - bx, az - bz);
}
double RouteDistance(double ax, double ay, double az, double bx, double by, double bz) {
	return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by) + (az - bz) * (az - bz));
}

bool LoadRoute(const std::string& path, RouteData& route) {
	route      = {};
	route.path = path;
	FILE* f    = std::fopen(path.c_str(), "rb");
	if (f == nullptr) {
		std::printf("Route: cannot open %s\n", path.c_str());
		std::fflush(stdout);
		return false;
	}
	std::vector<TrackPoint> points;
	char                    line[160];
	while (std::fgets(line, sizeof(line), f) != nullptr) {
		TrackPoint p;
		if (std::sscanf(line, "%lf %lf %lf %lf", &p.t, &p.x, &p.y, &p.z) == 4) points.push_back(p);
	}
	std::fclose(f);
	// Waypoints 1 m apart (the points of a player standing still fall away), a new part after each load.
	int segment = 0;
	for (size_t i = 0; i < points.size(); i++) {
		const auto& p = points[i];
		if (i > 0 && RouteDistance(p.x, p.y, p.z, points[i - 1].x, points[i - 1].y, points[i - 1].z) >= 30.0) segment++;
		const bool first = route.waypoints.empty() || route.waypoints.back().segment != segment;
		RouteWaypoint w {p.t, p.x, p.y, p.z, 0, segment};
		if (!first) {
			const auto&  b = route.waypoints.back();
			const double d = RouteDistance(p.x, p.y, p.z, b.x, b.y, b.z);
			if (d < 1.0) continue;
			w.speed = d / std::max(p.t - b.t, 1e-3);
		}
		route.waypoints.push_back(w);
	}
	std::vector<double> speeds;
	for (const auto& w: route.waypoints)
		if (w.speed > 0.3) speeds.push_back(w.speed);
	if (!speeds.empty()) {
		std::sort(speeds.begin(), speeds.end());
		route.full_speed = std::max(speeds[speeds.size() * 9 / 10], 0.5);
	}
	// The Cross presses of the tape beside it, placed where the player was (the track points around a press, 2 s apart
	// at most: a press in a menu or a load has no place and is left out).
	const std::string tape = path.size() > 6 && path.ends_with(".track") ? path.substr(0, path.size() - 6) : std::string();
	if (FILE* t = tape.empty() ? nullptr : std::fopen(tape.c_str(), "rb"); t != nullptr) {
#pragma pack(push, 1)
		struct Record { // controller.cpp's InputRecord
			uint32_t dt_us;
			uint32_t buttons;
			uint8_t  axes[6];
		};
#pragma pack(pop)
		static_assert(sizeof(Record) == 14);
		Record   r {};
		uint32_t before = 0;
		while (std::fread(&r, sizeof(r), 1, t) == 1) {
			const bool press = (r.buttons & Controller::PAD_BUTTON_CROSS) != 0 && (before & Controller::PAD_BUTTON_CROSS) == 0;
			before           = r.buttons;
			if (!press) continue;
			const double when = r.dt_us / 1e6;
			const auto   it   = std::upper_bound(points.begin(), points.end(), when,
			                                     [](double value, const TrackPoint& p) { return value < p.t; });
			if (it == points.begin() || it == points.end() || it->t - (it - 1)->t > 2.0) continue;
			const auto&      a = *(it - 1);
			const auto&      b = *it;
			const double     f = (when - a.t) / std::max(b.t - a.t, 1e-3);
			RouteInteraction in {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f, 0};
			for (size_t w = 0; w < route.waypoints.size() && route.waypoints[w].t <= when; w++) in.after = w;
			route.interactions.push_back(in);
		}
		std::fclose(t);
	}
	std::printf("Route: %s: %zu waypoints in %d part(s), %zu Cross presses, running speed %.1f m/s\n", path.c_str(),
	            route.waypoints.size(), segment + 1, route.interactions.size(), route.full_speed);
	std::fflush(stdout);
	return route.waypoints.size() >= 2;
}

// The pad the game reads goes back to the host's (the keyboard, the controller, a live `pad`).
void ReleaseRouteInput() {
	Controller::SetScriptedInput(-1, -1, -1, -1, 0, 0);
}

// The nearest waypoint from `from` on (3D: parts of a level lie above one another); the whole route when none ahead is
// within 30 m.
size_t NearestWaypoint(const std::vector<RouteWaypoint>& wps, size_t from, double x, double y, double z, double* distance) {
	size_t best = 0;
	double near = 1e30;
	for (int pass = 0; pass < 2 && near >= 30.0; pass++) {
		for (size_t i = pass == 0 ? from : 0; i < wps.size(); i++) {
			const double d = RouteDistance(x, y, z, wps[i].x, wps[i].y, wps[i].z);
			if (d < near) {
				near = d;
				best = i;
			}
		}
	}
	*distance = near;
	return best;
}

void RouteThread(RouteData route, uint64_t generation) {
	using Clock      = std::chrono::steady_clock;
	const auto alive = [generation] { return g_route_generation.load(std::memory_order_acquire) == generation; };
	const auto secs  = [](Clock::duration d) { return std::chrono::duration<double>(d).count(); };
	const auto env   = [](const char* name) {
		const char* value = std::getenv(name);
		return value != nullptr && value[0] != '\0' ? value : nullptr;
	};
	const double turn_gain = env("KYTY_ROUTE_TURN_GAIN") != nullptr ? std::atof(env("KYTY_ROUTE_TURN_GAIN")) : 1.0;
	int          sign      = env("KYTY_ROUTE_TURN_SIGN") != nullptr && std::atoi(env("KYTY_ROUTE_TURN_SIGN")) < 0 ? -1 : 1;
	bool         learn     = env("KYTY_ROUTE_TURN_SIGN") == nullptr; // the sign from the first turns
	const bool   loop      = env("KYTY_ROUTE_LOOP") != nullptr && env("KYTY_ROUTE_LOOP")[0] == '1';
	const auto&  wps       = route.waypoints;
	constexpr size_t Unplaced = static_cast<size_t>(-1);

	size_t   cursor = Unplaced; // the waypoint walked to
	size_t   next_press = 0;    // the next of route.interactions
	uint32_t glides = 0, presses = 0;
	bool     released = false, absent = true, far_logged = false;
	auto     now           = Clock::now();
	uint64_t draws_before  = FrameGen::g_gameplay_draws.load(std::memory_order_relaxed);
	auto     draws_changed = now;
	auto     busy_until    = now; // a glide on its way: no input
	auto     pause_until   = now; // standing for a press
	auto     press_until   = now; // Cross held
	auto     roll_until    = now; // Circle held (a roll)
	auto     wiggle_from = now, wiggle_until = now;
	auto     part_end_since = Clock::time_point {}; // standing at the end of a part, for the load that follows
	double   progress_x = 0, progress_y = 0, progress_z = 0; // where the walk last made 0.5 m of progress, and when
	double   last_x = 0, last_y = 0, last_z = 0;               // the position a step ago (a jump of 30 m: a load)
	bool     have_last = false;
	auto     progress_t   = now;
	int      unstick_step = 0; // the rolls done this time stuck
	auto     log_t        = now;
	// Learning the turn's sign: the yaw at the start of a 0.5 s window of turning one way while walking.
	double learn_yaw = 0, learn_x = 0, learn_z = 0;
	int    learn_dir = 0, learn_wrong = 0, learn_right = 0;
	auto   learn_t0  = now;

	for (;; SDL_Delay(33)) {
		if (!alive()) return; // (HostInputStopRoute or the next route released the input)
		now               = Clock::now();
		const auto draws  = FrameGen::g_gameplay_draws.load(std::memory_order_relaxed);
		if (draws != draws_before) {
			draws_before  = draws;
			draws_changed = now;
		}
		float      xyz[3] {}, yaw_f = 0;
		const bool player = Loader::DemonsSoulsWarp::PlayerPosition(xyz, &yaw_f);
		// Loads, menus, cutscenes: no player, or no gameplay drawn for half a second.
		if (!player || secs(now - draws_changed) > 0.5) {
			if (!released) ReleaseRouteInput();
			released = true;
			absent   = true;
			continue;
		}
		if (now < busy_until) continue;
		const double px = xyz[0], py = xyz[1], pz = xyz[2], yaw = yaw_f;
		if (have_last && RouteDistance(px, py, pz, last_x, last_y, last_z) >= 30.0) absent = true; // (a load without a gap)
		last_x    = px;
		last_y    = py;
		last_z    = pz;
		have_last = true;
		// Placed on the route: at the start, and after a load if the waypoint walked to is far.
		if (cursor == Unplaced ||
		    (absent && RouteDistance(px, py, pz, wps[cursor].x, wps[cursor].y, wps[cursor].z) >= 30.0)) {
			double     d    = 0;
			const auto from = cursor == Unplaced ? 0 : cursor;
			const auto at   = NearestWaypoint(wps, from, px, py, pz, &d);
			if (d >= 30.0) {
				if (!far_logged) std::printf("Route: the player is %.0f m from the route: waiting\n", d);
				far_logged = true;
				if (!released) ReleaseRouteInput();
				released = true;
				cursor   = Unplaced;
				continue;
			}
			cursor = at;
			while (next_press < route.interactions.size() && route.interactions[next_press].after + 1 < cursor) next_press++;
			std::printf("Route: at waypoint %zu/%zu (%.1f m away)\n", cursor, wps.size(), d);
			far_logged = false;
			progress_x = px;
			progress_y = py;
			progress_z = pz;
			progress_t = now;
		}
		absent   = false;
		released = false;
		const int part = wps[cursor].segment;
		const auto same_part = [&](size_t i) { return i < wps.size() && wps[i].segment == part; };
		// Waypoints reached (1.5 m on the ground), and one further on the player is already at (a corner cut).
		while (same_part(cursor + 1) && RouteFlat(px, pz, wps[cursor].x, wps[cursor].z) < 1.5) cursor++;
		for (size_t i = cursor + 1; i < cursor + 8 && same_part(i); i++) {
			if (RouteFlat(px, pz, wps[i].x, wps[i].z) < 1.5) {
				cursor = same_part(i + 1) ? i + 1 : i;
				break;
			}
		}
		const bool at_part_end = !same_part(cursor + 1) && RouteFlat(px, pz, wps[cursor].x, wps[cursor].z) < 1.5;
		if (at_part_end && cursor + 1 >= wps.size()) {
			ReleaseRouteInput();
			std::printf("Route: done (%zu waypoints, %u glides, %u interactions)\n", wps.size(), glides, presses);
			std::fflush(stdout);
			if (!loop) return;
			if (wps.back().segment != 0) {
				std::printf("Route: KYTY_ROUTE_LOOP needs a route without loads; stopped\n");
				std::fflush(stdout);
				return;
			}
			const double seconds = Loader::DemonsSoulsWarp::Glide(static_cast<float>(wps[0].x), static_cast<float>(wps[0].y),
			                                                      static_cast<float>(wps[0].z), 12.0f);
			busy_until = now + std::chrono::milliseconds(static_cast<int64_t>(std::max(seconds, 0.0) * 1000 + 300));
			cursor     = 0;
			next_press = 0;
			progress_t = busy_until;
			continue;
		}
		if (at_part_end) {
			// The recording loaded here (an archstone, a death, a lift): stand and wait for the load the presses bring.
			if (part_end_since == Clock::time_point {}) part_end_since = now;
			if (secs(now - part_end_since) > 30.0) {
				ReleaseRouteInput();
				std::printf("Route: ended at the end of part %d: no load came in 30 s (%u glides, %u interactions)\n", part,
				            glides, presses);
				std::fflush(stdout);
				return;
			}
		} else {
			part_end_since = {};
		}

		// A Cross press of the recording due here: stand, press, give the game a moment (a door, a lever, an item).
		while (next_press < route.interactions.size() && route.interactions[next_press].after + 6 < cursor) {
			const auto& in = route.interactions[next_press++];
			std::printf("Route: passed the Cross press at %.1f %.1f %.1f without reaching it\n", in.x, in.y, in.z);
		}
		if (next_press < route.interactions.size() && now >= pause_until) {
			const auto& in = route.interactions[next_press];
			if (in.after <= cursor + 1 && RouteFlat(px, pz, in.x, in.z) <= 1.5 && std::abs(py - in.y) <= 2.0) {
				press_until = now + std::chrono::milliseconds(150);
				pause_until = now + std::chrono::milliseconds(800);
				presses++;
				next_press++;
			}
		}

		// The waypoint about 3 m ahead along the route (the one walked to while 2.5 m or more off the path).
		size_t target = cursor;
		double off    = RouteFlat(px, pz, wps[cursor].x, wps[cursor].z);
		if (cursor > 0 && same_part(cursor - 1)) {
			// (Off the path: the distance to the line from the previous waypoint to this one.)
			const auto&  a  = wps[cursor - 1];
			const auto&  b  = wps[cursor];
			const double vx = b.x - a.x, vz = b.z - a.z;
			const double len2 = vx * vx + vz * vz;
			const double f    = len2 > 1e-6 ? std::clamp(((px - a.x) * vx + (pz - a.z) * vz) / len2, 0.0, 1.0) : 0.0;
			off               = RouteFlat(px, pz, a.x + vx * f, a.z + vz * f);
		}
		if (off < 2.5) {
			double along = RouteFlat(px, pz, wps[cursor].x, wps[cursor].z);
			while (along < 3.0 && same_part(target + 1)) {
				along += RouteFlat(wps[target].x, wps[target].z, wps[target + 1].x, wps[target + 1].z);
				target++;
			}
		}
		const double error   = RouteWrap(RouteHeading(wps[target].x - px, wps[target].z - pz) - yaw);
		const double error_d = error * 180.0 / RoutePi;
		double       turn    = std::abs(error_d) < 4.0 ? 0.0 : std::clamp(turn_gain * error_d / 45.0, -1.0, 1.0);
		double forward = wps[target].speed > 0 ? std::clamp(wps[target].speed / route.full_speed, 0.35, 1.0) : 1.0;
		if (std::abs(error_d) > 70.0) {
			forward = 0.3; // nearly turned around: walking slowly, the character turns with the camera
			turn    = error_d > 0 ? 1.0 : -1.0;
		}
		const bool standing = now < pause_until || at_part_end;
		if (standing) forward = turn = 0;

		// Stuck: under 0.5 m of progress in 2 s while walking. A roll and a camera wiggle at 2 s and 4 s, a glide at 6 s.
		if (standing || RouteDistance(px, py, pz, progress_x, progress_y, progress_z) >= 0.5) {
			progress_x   = px;
			progress_y   = py;
			progress_z   = pz;
			progress_t   = now;
			unstick_step = 0;
		} else if (const double stuck = secs(now - progress_t); stuck >= 6.0) {
			const auto&  w       = wps[cursor];
			const double seconds = Loader::DemonsSoulsWarp::Glide(static_cast<float>(w.x), static_cast<float>(w.y),
			                                                      static_cast<float>(w.z), 4.0f);
			glides++;
			std::printf("Route: stuck 6 s at %.1f %.1f %.1f: glided to waypoint %zu (%.1f s; %u glides)\n", px, py, pz, cursor,
			            seconds, glides);
			std::fflush(stdout);
			busy_until   = now + std::chrono::milliseconds(static_cast<int64_t>(std::max(seconds, 0.0) * 1000 + 300));
			progress_t   = busy_until;
			unstick_step = 0;
			continue;
		} else if (stuck >= 2.0 * (unstick_step + 1) && unstick_step < 2) {
			unstick_step++;
			roll_until   = now + std::chrono::milliseconds(120);
			wiggle_from  = now;
			wiggle_until = now + std::chrono::milliseconds(600);
			std::printf("Route: stuck %.0f s at %.1f %.1f %.1f: a roll and a camera wiggle\n", stuck, px, py, pz);
		}

		int rx = 128 + sign * static_cast<int>(std::lround(turn * 127.0));
		if (now < wiggle_until) rx = 128 + ((static_cast<int>(secs(now - wiggle_from) * 6.0) % 2) != 0 ? 90 : -90);
		const int      ly      = 128 - static_cast<int>(std::lround(forward * 127.0));
		const uint32_t buttons = (now < press_until ? Controller::PAD_BUTTON_CROSS : 0u) |
		                         (now < roll_until ? Controller::PAD_BUTTON_CIRCLE : 0u);
		// (A short hold, renewed each step: if this thread stopped, the pad would be the host's again at once.)
		Controller::SetScriptedInput(128, std::clamp(ly, 0, 255), std::clamp(rx, 0, 255), 128, buttons, 0.25);

		// The turn's sign: half a second of turning one way while walking must turn the character that way; the other
		// way twice in a row, and the sign flips (three times right: it stays).
		if (learn) {
			const int dir = std::abs(rx - 128) >= 64 && forward > 0 ? (rx > 128 ? 1 : -1) : 0;
			if (dir == 0 || dir != learn_dir) {
				learn_dir = dir;
				learn_yaw = yaw;
				learn_x   = px;
				learn_z   = pz;
				learn_t0  = now;
			} else if (secs(now - learn_t0) >= 0.5) {
				const double turned = RouteWrap(yaw - learn_yaw) * 180.0 / RoutePi;
				if (RouteFlat(px, pz, learn_x, learn_z) >= 0.3 && std::abs(turned) >= 6.0) {
					// rx right means turn > 0 (error > 0, the heading to raise) under `sign`: the yaw should have risen.
					const bool right_way = (turned > 0) == (dir * sign > 0);
					learn_wrong          = right_way ? 0 : learn_wrong + 1;
					learn_right          = right_way ? learn_right + 1 : 0;
					if (learn_wrong >= 2) {
						sign        = -sign;
						learn_wrong = 0;
						std::printf("Route: the camera turns the other way: turn sign %+d\n", sign);
					} else if (learn_right >= 3) {
						learn = false;
						std::printf("Route: turn sign %+d confirmed (KYTY_ROUTE_TURN_SIGN=%d skips the learning)\n", sign, sign);
					}
				}
				learn_dir = 0;
			}
		}

		if (secs(now - log_t) >= 1.0) {
			log_t = now;
			std::printf("Route: waypoint %zu/%zu part %d, %.1f m off the path, heading error %+.0f deg, forward %.2f, "
			            "camera %+d%s%s\n",
			            cursor, wps.size(), part, off, error_d, forward, rx - 128, standing ? ", standing" : "",
			            learn ? ", learning the turn" : "");
			std::fflush(stdout);
		}
	}
}
} // namespace

bool HostInputStartRoute(const char* track_path) {
	RouteData route;
	if (track_path == nullptr || track_path[0] == '\0' || !LoadRoute(track_path, route)) return false;
	const auto generation = g_route_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
	std::thread(RouteThread, std::move(route), generation).detach();
	return true;
}

void HostInputStopRoute() {
	g_route_generation.fetch_add(1, std::memory_order_acq_rel);
	ReleaseRouteInput();
	std::printf("Route: stopped\n");
	std::fflush(stdout);
}

void HostInputInit() {
	StartScriptedInput();
	StartTapeMarkerWatch();
	GetInputMap();
	if (const char* route = std::getenv("KYTY_ROUTE"); route != nullptr && route[0] != '\0') HostInputStartRoute(route);
}

void HostInputKey(int key_code, bool down) {
	const auto& map = GetInputMap();
	if (map.Custom()) {
		SetControl(map.FindKey(key_code), down);
	} else {
		DefaultKeyboardInput(key_code, down);
	}
}

static void EnableMouseToJoystick(bool enable, const char* why) {
	if (!enable) {
		if (!g_mouse.enabled) return;
		SetRelativeMouseMode(false);
		CenterMouseStick();
		g_mouse.enabled = false;
		LOGF("Mouse to right stick: released (%s)\n", why);
		return;
	}
	if (g_mouse.enabled || !SetRelativeMouseMode(true)) {
		return;
	}
	int ignored_x = 0;
	int ignored_y = 0;
	SDL_GetRelativeMouseState(&ignored_x, &ignored_y);
	g_mouse.enabled   = true;
	g_mouse.next_poll = SDL_GetTicks64() + MOUSE_POLL_INTERVAL_MS;
	g_mouse.last_poll = SDL_GetTicks64();
	g_mouse.out_x = g_mouse.out_y = 0;
	LOGF("Mouse to right stick: captured (%s; Esc or F7 releases)\n", why);
}

void HostInputMouseButton(uint8_t mouse_button, bool down) {
	if (mouse_button == 0) return;
	// The first click in the window takes the mouse for the camera (the click still counts).
	if (down && !g_mouse.enabled && MouseCaptureOnClick() && SDL_GetKeyboardFocus() != nullptr) {
		EnableMouseToJoystick(true, "clicked in the window");
		g_mouse.wanted = true;
	}
	const auto& map = GetInputMap();
	if (map.Custom()) {
		SetControl(map.FindMouseButton(mouse_button), down);
	} else {
		SetButton(DefaultMouseButton(mouse_button), down);
	}
}

// The wheel steps the D-pad (up: spells, down: items), a short press each notch.
void HostInputMouseWheel(int y) {
	if (y == 0 || GetInputMap().Custom()) return;
	const uint32_t button = y > 0 ? Controller::PAD_BUTTON_UP : Controller::PAD_BUTTON_DOWN;
	SetButton(button, true);
	std::thread([button] {
		SDL_Delay(60);
		SetButton(button, false);
	}).detach();
}

void HostInputToggleMouseToJoystick() {
	EnableMouseToJoystick(!g_mouse.enabled, "F7");
	g_mouse.wanted = g_mouse.enabled;
}

bool HostInputReleaseMouse() {
	if (!g_mouse.enabled) return false;
	EnableMouseToJoystick(false, "Esc");
	g_mouse.wanted = false;
	return true;
}

// The window's focus: the mouse is released while another window has it, and taken again when it returns.
void HostInputFocus(bool gained) {
	if (gained) {
		if (g_mouse.wanted && !g_mouse.enabled) EnableMouseToJoystick(true, "the window's focus returned");
	} else if (g_mouse.enabled) {
		EnableMouseToJoystick(false, "the window lost focus");
		g_mouse.wanted = true;
	}
}

bool HostInputMouseCaptured() {
	return g_mouse.enabled;
}

int PollMouse(uint64_t now_ms) {
	if (now_ms < g_mouse.next_poll) {
		return static_cast<int>(g_mouse.next_poll - now_ms);
	}
	const double dt_ms = static_cast<double>(now_ms - g_mouse.last_poll);
	g_mouse.last_poll  = now_ms;
	g_mouse.next_poll  = now_ms + MOUSE_POLL_INTERVAL_MS;

	int delta_x = 0;
	int delta_y = 0;
	SDL_GetRelativeMouseState(&delta_x, &delta_y);
	if (delta_x == 0 && delta_y == 0) {
		// The smoothed value runs down over a few polls, then the stick rests at centre.
		if (g_mouse.output && (std::fabs(g_mouse.out_x) >= 2.0 || std::fabs(g_mouse.out_y) >= 2.0)) {
			MouseToJoystick(0, 0, dt_ms);
		} else {
			CenterMouseStick();
		}
		return MOUSE_POLL_INTERVAL_MS;
	}

	MouseToJoystick(delta_x, delta_y, dt_ms);
	return MOUSE_POLL_INTERVAL_MS;
}

bool HostInputWaitEvent(SDL_Event* event) {
	if (!g_mouse.enabled || SDL_GetKeyboardFocus() == nullptr) {
		CenterMouseStick();
		if (SDL_WaitEvent(event) == 0) {
			EXIT("%s\n", SDL_GetError());
		}
		return true;
	}

	const int timeout_ms = PollMouse(SDL_GetTicks64());
	SDL_ClearError();
	if (SDL_WaitEventTimeout(event, timeout_ms) != 0) {
		return true;
	}
	if (SDL_GetError()[0] != '\0') {
		EXIT("%s\n", SDL_GetError());
	}
	return false;
}

} // namespace Libs::Graphics
