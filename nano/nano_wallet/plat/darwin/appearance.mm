#include <nano/nano_wallet/appearance.hpp>

#include <AppKit/AppKit.h>

void nano::set_application_appearance (QApplication &)
{
	// Info.plist opts out of dark mode only inside the app bundle; Qt follows this change and refreshes its palette
	NSApp.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
}
