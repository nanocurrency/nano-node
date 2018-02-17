#include "clipboardproxy.hpp"

#include <QApplication>
#include <QClipboard>

ClipboardProxy::ClipboardProxy (QObject * parent) :
QObject (parent)
{
}

void ClipboardProxy::sendText (QString text)
{
	QApplication::clipboard ()->setText (text);
}
