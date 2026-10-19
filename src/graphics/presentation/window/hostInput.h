#ifndef KYTY_GRAPHICS_PRESENTATION_WINDOW_HOST_INPUT_H_
#define KYTY_GRAPHICS_PRESENTATION_WINDOW_HOST_INPUT_H_

#include <cstdint>

union SDL_Event;

namespace Libs::Graphics {

void               HostInputInit();
// Every presented frame (the window title's counter): the scripted input watches the frame rate.
void               HostInputNotePresentedFrame();
void               HostInputKey(int key_code, bool down);
void               HostInputMouseButton(uint8_t mouse_button, bool down);
void               HostInputToggleMouseToJoystick();
// The wheel's notches (SDL's y: up positive) as D-pad presses without a custom key map.
void               HostInputMouseWheel(int y);
// Esc: the mouse released from the camera; true when it was held.
bool               HostInputReleaseMouse();
// The window's keyboard focus came (true) or went: the mouse capture follows it.
void               HostInputFocus(bool gained);
[[nodiscard]] bool HostInputMouseCaptured();
[[nodiscard]] bool HostInputWaitEvent(SDL_Event* event);
// The route walker (KYTY_ROUTE=<file.track> at start, the live `route`): the character walks a recorded track with stick
// input (hostInput.cpp). False when the track cannot be read or has fewer than two waypoints; a new route replaces a
// running one. Stop gives the pad back to the host.
bool               HostInputStartRoute(const char* track_path);
void               HostInputStopRoute();

} // namespace Libs::Graphics

#endif /* KYTY_GRAPHICS_PRESENTATION_WINDOW_HOST_INPUT_H_ */
