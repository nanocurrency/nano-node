#pragma once

#include <QObject>

class ClipboardProxy : public QObject
{
	Q_OBJECT
public:
	explicit ClipboardProxy (QObject * parent = nullptr);
	Q_INVOKABLE void sendText (QString);
};
