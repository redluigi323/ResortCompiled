#pragma once
// Virtual Wii Remote with keyboard/mouse and DualSense input (src/resort_wiimote_kbm.cpp).
#include <cstdint>

union SDL_Event;

namespace ResortWiimote {
// WPADProbe for the virtual remote; false when `chan` is not served by it.
bool Probe(uint32_t chan, uint32_t typePtr, int32_t& result);
bool IsVirtualRemote(uint32_t chan);
// True when an assigned DualSense with a gyroscope drives player 1.
bool DualSenseActive();
const char* ActiveInputName();
// Mouse wheel (roll) and focus changes; called from the settings overlay's event loop.
void HandleSdlEvent(const SDL_Event& ev);
// True while the game window has focus, no overlay owns input and the virtual remote is in use.
bool CursorShouldHide();
}  // namespace ResortWiimote
