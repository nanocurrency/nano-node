#include <nano/nano_wallet/icon.hpp>

#include <QApplication>
#include <QIcon>

void nano::set_application_icon (QApplication & application)
{
	application.setWindowIcon (QIcon (":/icon.png"));
}
