#include "ui/Icons.h"

#include "ui/Theme.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>

namespace Icons {

void paint(QPainter& painter, Icon icon, const QRectF& rect, const QColor& color, qreal stroke) {
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	painter.translate(rect.topLeft());
	const qreal scale = rect.width() / 24.0;
	painter.scale(scale, rect.height() / 24.0);
	QPen pen(color, stroke / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	painter.setPen(pen);
	painter.setBrush(Qt::NoBrush);
	auto dot = [&](qreal x, qreal y, qreal r) {
		painter.save();
		painter.setPen(Qt::NoPen);
		painter.setBrush(color);
		painter.drawEllipse(QPointF(x, y), r, r);
		painter.restore();
	};
	QPainterPath path;
	switch (icon) {
		case Icon::Home:
			path.moveTo(3.5, 11.5);
			path.lineTo(12, 4.2);
			path.lineTo(20.5, 11.5);
			path.moveTo(6, 9.6);
			path.lineTo(6, 20);
			path.lineTo(18, 20);
			path.lineTo(18, 9.6);
			path.moveTo(10, 20);
			path.lineTo(10, 14.5);
			path.lineTo(14, 14.5);
			path.lineTo(14, 20);
			painter.drawPath(path);
			break;
		case Icon::Settings:
			// Sliders: three lines, each with a knob.
			for (const auto& [y, x]: {std::pair {6.5, 9.0}, std::pair {12.0, 15.5}, std::pair {17.5, 7.5}}) {
				painter.drawLine(QPointF(3.5, y), QPointF(x - 2.6, y));
				painter.drawLine(QPointF(x + 2.6, y), QPointF(20.5, y));
				painter.drawEllipse(QPointF(x, y), 2.2, 2.2);
			}
			break;
		case Icon::Help:
			painter.drawEllipse(QPointF(12, 12), 8.6, 8.6);
			path.moveTo(9.4, 9.7);
			path.cubicTo(9.4, 8.1, 10.6, 7.1, 12.1, 7.1);
			path.cubicTo(13.7, 7.1, 14.8, 8.1, 14.8, 9.6);
			path.cubicTo(14.8, 11.5, 12.1, 11.7, 12.1, 13.7);
			painter.drawPath(path);
			dot(12.1, 16.6, 1.05);
			break;
		case Icon::Play:
			path.moveTo(8, 5.2);
			path.lineTo(19, 12);
			path.lineTo(8, 18.8);
			path.closeSubpath();
			painter.setBrush(color);
			painter.drawPath(path);
			break;
		case Icon::Check:
			path.moveTo(6, 12.6);
			path.lineTo(10.2, 16.6);
			path.lineTo(18.2, 8);
			painter.drawPath(path);
			break;
		case Icon::Exclamation:
			painter.drawLine(QPointF(12, 6.4), QPointF(12, 13.4));
			dot(12, 17.4, 1.25);
			break;
		case Icon::Cross:
			painter.drawLine(QPointF(7.5, 7.5), QPointF(16.5, 16.5));
			painter.drawLine(QPointF(16.5, 7.5), QPointF(7.5, 16.5));
			break;
		case Icon::Close:
			painter.drawLine(QPointF(7, 7), QPointF(17, 17));
			painter.drawLine(QPointF(17, 7), QPointF(7, 17));
			break;
		case Icon::Folder:
			path.moveTo(3.5, 7.5);
			path.quadTo(3.5, 5.8, 5.2, 5.8);
			path.lineTo(9.3, 5.8);
			path.lineTo(11.3, 8);
			path.lineTo(18.8, 8);
			path.quadTo(20.5, 8, 20.5, 9.7);
			path.lineTo(20.5, 17.3);
			path.quadTo(20.5, 19, 18.8, 19);
			path.lineTo(5.2, 19);
			path.quadTo(3.5, 19, 3.5, 17.3);
			path.closeSubpath();
			painter.drawPath(path);
			break;
		case Icon::Book:
			path.moveTo(12, 7);
			path.cubicTo(10, 5.6, 6.6, 5.4, 3.8, 6.2);
			path.lineTo(3.8, 18.2);
			path.cubicTo(6.6, 17.4, 10, 17.6, 12, 19);
			path.cubicTo(14, 17.6, 17.4, 17.4, 20.2, 18.2);
			path.lineTo(20.2, 6.2);
			path.cubicTo(17.4, 5.4, 14, 5.6, 12, 7);
			path.lineTo(12, 19);
			painter.drawPath(path);
			break;
		case Icon::Flag:
			painter.drawLine(QPointF(6, 20.5), QPointF(6, 4));
			path.moveTo(6, 4.6);
			path.lineTo(18, 4.6);
			path.lineTo(15.3, 8.7);
			path.lineTo(18, 12.8);
			path.lineTo(6, 12.8);
			painter.drawPath(path);
			break;
		case Icon::External:
			path.moveTo(18.5, 13.5);
			path.lineTo(18.5, 18.2);
			path.quadTo(18.5, 19.5, 17.2, 19.5);
			path.lineTo(5.8, 19.5);
			path.quadTo(4.5, 19.5, 4.5, 18.2);
			path.lineTo(4.5, 6.8);
			path.quadTo(4.5, 5.5, 5.8, 5.5);
			path.lineTo(10.5, 5.5);
			path.moveTo(11.5, 12.5);
			path.lineTo(19.5, 4.5);
			path.moveTo(14.5, 4.5);
			path.lineTo(19.5, 4.5);
			path.lineTo(19.5, 9.5);
			painter.drawPath(path);
			break;
		case Icon::ChevronDown:
			path.moveTo(7, 9.5);
			path.lineTo(12, 14.5);
			path.lineTo(17, 9.5);
			painter.drawPath(path);
			break;
		case Icon::ChevronUp:
			path.moveTo(7, 14.5);
			path.lineTo(12, 9.5);
			path.lineTo(17, 14.5);
			painter.drawPath(path);
			break;
		case Icon::ChevronRight:
			path.moveTo(9.5, 7);
			path.lineTo(14.5, 12);
			path.lineTo(9.5, 17);
			painter.drawPath(path);
			break;
		case Icon::Stop:
			painter.setBrush(color);
			painter.drawRoundedRect(QRectF(7, 7, 10, 10), 1.5, 1.5);
			break;
		case Icon::Download:
			painter.drawLine(QPointF(12, 4), QPointF(12, 15));
			path.moveTo(7.5, 10.5);
			path.lineTo(12, 15);
			path.lineTo(16.5, 10.5);
			painter.drawPath(path);
			painter.drawLine(QPointF(5, 19.5), QPointF(19, 19.5));
			break;
		case Icon::Info:
			painter.drawEllipse(QPointF(12, 12), 8.6, 8.6);
			painter.drawLine(QPointF(12, 11), QPointF(12, 16.5));
			dot(12, 7.8, 1.1);
			break;
		case Icon::Sparkle:
			path.moveTo(12, 3.5);
			path.cubicTo(12.6, 8.8, 15.2, 11.4, 20.5, 12);
			path.cubicTo(15.2, 12.6, 12.6, 15.2, 12, 20.5);
			path.cubicTo(11.4, 15.2, 8.8, 12.6, 3.5, 12);
			path.cubicTo(8.8, 11.4, 11.4, 8.8, 12, 3.5);
			painter.setBrush(color);
			painter.drawPath(path);
			break;
	}
	painter.restore();
}

void paintEmblem(QPainter& painter, const QRectF& rect, bool background) {
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	const qreal size = std::min(rect.width(), rect.height());
	const QRectF square(rect.center().x() - size / 2, rect.center().y() - size / 2, size, size);
	if (background) {
		QLinearGradient tile(square.topLeft(), square.bottomRight());
		tile.setColorAt(0, QColor(0x2A, 0x24, 0x1B));
		tile.setColorAt(1, QColor(0x12, 0x12, 0x12));
		painter.setPen(QPen(QColor(0x4A, 0x3E, 0x2B), std::max(1.0, size / 64)));
		painter.setBrush(tile);
		const qreal inset = size * 0.04;
		painter.drawRoundedRect(square.adjusted(inset, inset, -inset, -inset), size * 0.2, size * 0.2);
		QRadialGradient glow(square.center(), size * 0.45);
		glow.setColorAt(0, QColor(0xC9, 0xA2, 0x5E, 70));
		glow.setColorAt(1, QColor(0xC9, 0xA2, 0x5E, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(glow);
		painter.drawEllipse(square.center(), size * 0.45, size * 0.45);
	}
	const QPointF c = square.center();
	const qreal   r = size * (background ? 0.30 : 0.42);
	const qreal   w = r * 0.74;
	QPolygonF     outer {QPointF(c.x(), c.y() - r), QPointF(c.x() + w, c.y()), QPointF(c.x(), c.y() + r), QPointF(c.x() - w, c.y())};
	QLinearGradient gold(QPointF(c.x(), c.y() - r), QPointF(c.x(), c.y() + r));
	gold.setColorAt(0, Theme::kAccentHover);
	gold.setColorAt(1, Theme::kAccentPressed);
	painter.setPen(QPen(QBrush(gold), r * 0.2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
	painter.setBrush(Qt::NoBrush);
	painter.drawPolygon(outer);
	const qreal inner_r = r * 0.34, inner_w = inner_r * 0.74;
	painter.setPen(Qt::NoPen);
	painter.setBrush(QBrush(gold));
	painter.drawPolygon(QPolygonF {QPointF(c.x(), c.y() - inner_r), QPointF(c.x() + inner_w, c.y()), QPointF(c.x(), c.y() + inner_r),
	                               QPointF(c.x() - inner_w, c.y())});
	painter.restore();
}

QIcon appIcon() {
	QIcon icon;
	for (int size: {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
		QPixmap pixmap(size, size);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		paintEmblem(painter, QRectF(0, 0, size, size), true);
		painter.end();
		icon.addPixmap(pixmap);
	}
	return icon;
}

} // namespace Icons
