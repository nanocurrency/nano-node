#include <nano/nano_wallet/splash_screen.hpp>

#include <QPainter>

nano::splash_screen::splash_screen (QPixmap const & pixmap) :
	QSplashScreen{ pixmap }
{
}

void nano::splash_screen::show_status (QString const & text)
{
	status = text;
	repaint ();
}

void nano::splash_screen::drawContents (QPainter * painter)
{
	QSplashScreen::drawContents (painter);
	// Same inset and pen as the message QSplashScreen just painted at the bottom
	painter->drawText (rect ().adjusted (5, 5, -5, -5), Qt::AlignTop | Qt::AlignHCenter, status);
}

void nano::splash_screen::mousePressEvent (QMouseEvent *)
{
	// QSplashScreen hides itself on click, which would leave a long ledger load with no indicator
}
