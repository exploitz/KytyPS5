#pragma once

#include <QColor>
#include <QIcon>
#include <QRectF>

class QPainter;

// Line icons drawn with QPainter on a 24-unit grid: sharp at any scale, no image plugins needed.
namespace Icons {

enum class Icon {
	Home,
	Settings,
	Help,
	Play,
	Check,
	Exclamation,
	Cross,
	Folder,
	Book,
	Flag,
	External,
	ChevronDown,
	ChevronUp,
	ChevronRight,
	Stop,
	Close,
	Download,
	Info,
	Sparkle,
};

void paint(QPainter& painter, Icon icon, const QRectF& rect, const QColor& color, qreal stroke = 1.6);
// The launcher's emblem (an ember gold diamond); `background`: on its dark rounded tile (the app icon).
void paintEmblem(QPainter& painter, const QRectF& rect, bool background);
QIcon appIcon();

} // namespace Icons
