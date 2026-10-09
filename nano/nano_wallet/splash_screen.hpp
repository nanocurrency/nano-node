#pragma once

#include <QSplashScreen>

namespace nano
{
/**
 * Startup splash screen with a status line above the logo, next to the QSplashScreen message at the bottom.
 * Stays up when clicked.
 */
class splash_screen final : public QSplashScreen
{
public:
	explicit splash_screen (QPixmap const & pixmap);

	// Shows a status line at the top of the splash screen and repaints right away
	void show_status (QString const & text);

protected:
	void drawContents (QPainter * painter) override;
	void mousePressEvent (QMouseEvent * event) override;

private:
	QString status;
};
}
