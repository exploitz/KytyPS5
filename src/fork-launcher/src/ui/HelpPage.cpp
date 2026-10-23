#include "ui/HelpPage.h"

#include "core/Version.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QDir>
#include <QPainter>
#include <QVBoxLayout>

ActionRow::ActionRow(Icons::Icon icon, const QString& title, const QString& line, bool external, QWidget* parent)
    : QAbstractButton(parent), m_icon(icon), m_line(line), m_external(external) {
	setText(title);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setMinimumHeight(60);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setAccessibleName(title);
	setAccessibleDescription(line);
}

void ActionRow::setLine(const QString& line) {
	m_line = line;
	update();
}

void ActionRow::enterEvent(QEnterEvent* event) {
	m_hover = true;
	update();
	QAbstractButton::enterEvent(event);
}

void ActionRow::leaveEvent(QEvent* event) {
	m_hover = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void ActionRow::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	if (m_hover || Theme::keyboardFocus(this) || isDown()) {
		p.setPen(Qt::NoPen);
		p.setBrush(isDown() ? QColor(0x2A, 0x2A, 0x2A) : QColor(0x24, 0x24, 0x24));
		p.drawRoundedRect(QRectF(rect()), Theme::kRadius, Theme::kRadius);
	}
	// The icon on a small tile.
	const QRectF tile(14, height() / 2.0 - 18, 36, 36);
	p.setPen(Qt::NoPen);
	p.setBrush(Theme::withAlpha(Theme::kAccent, 24));
	p.drawRoundedRect(tile, Theme::kRadius, Theme::kRadius);
	Icons::paint(p, m_icon, tile.adjusted(8, 8, -8, -8), Theme::kAccent, 1.6);
	p.setFont(Theme::font(10.5, QFont::DemiBold));
	p.setPen(Theme::kText);
	p.drawText(QRectF(64, 10, width() - 120, 22), Qt::AlignLeft | Qt::AlignVCenter, text());
	p.setFont(Theme::font(9.5));
	p.setPen(Theme::kSecondary);
	const QString line = QFontMetrics(Theme::font(9.5)).elidedText(m_line, Qt::ElideMiddle, width() - 120);
	p.drawText(QRectF(64, 31, width() - 120, 20), Qt::AlignLeft | Qt::AlignVCenter, line);
	Icons::paint(p, m_external ? Icons::Icon::External : Icons::Icon::ChevronRight, QRectF(width() - 38, height() / 2.0 - 9, 18, 18),
	             m_hover ? Theme::kText : Theme::kSecondary, 1.6);
}

HelpPage::HelpPage(const QString& logFolder, QWidget* parent): QWidget(parent) {
	setObjectName(QStringLiteral("Page"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(32, 24, 32, 20);
	layout->setSpacing(12);
	layout->addWidget(makeLabel(tr("Help"), "title", this));
	auto* intro = makeLabel(tr("Everything about setting up and playing is in the guide. If something goes wrong, a report helps us fix it."),
	                        "secondary", this);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	layout->addSpacing(4);

	auto* card        = makeCard(this);
	auto* card_layout = new QVBoxLayout(card);
	card_layout->setContentsMargins(6, 6, 6, 6);
	card_layout->setSpacing(0);
	auto add = [&](ActionRow* row, void (HelpPage::*signal)()) {
		if (card_layout->count() > 0) card_layout->addWidget(makeSeparator(card));
		card_layout->addWidget(row);
		connect(row, &ActionRow::clicked, this, signal);
	};
	add(new ActionRow(Icons::Icon::Book, tr("Open guide"), tr("README.md: setup, settings and fixes for common problems"), false, card),
	    &HelpPage::openGuide);
	add(new ActionRow(Icons::Icon::Folder, tr("Open logs folder"), QDir::toNativeSeparators(logFolder), false, card), &HelpPage::openLogs);
	add(new ActionRow(Icons::Icon::Flag, tr("Report a problem"), tr("Saves a report to your Desktop and opens a new GitHub issue"), true, card),
	    &HelpPage::reportProblem);
	add(new ActionRow(Icons::Icon::Sparkle, tr("Export shader hints"),
	                  tr("Which shaders your play used (no game code): attached to an issue, the next releases precompile them"), false, card),
	    &HelpPage::exportHints);
	add(new ActionRow(Icons::Icon::Download, tr("Releases page"), QStringLiteral("github.com/exploitz/KytyPS5/releases"), true, card),
	    &HelpPage::openReleases);
	layout->addWidget(card);
	layout->addStretch(1);

	auto* about = new QVBoxLayout;
	about->setSpacing(4);
	auto* version = makeLabel(tr("KytyPS5 Launcher %1").arg(Version::text()), nullptr, this);
	version->setFont(Theme::font(9.5, QFont::DemiBold));
	version->setTextInteractionFlags(Qt::TextSelectableByMouse);
	about->addWidget(version);
	auto* credits = makeLabel(tr("Built on chenxiao07's KytyPS5 branch, KytyPS5 and Kyty."), "secondary", this);
	credits->setFont(Theme::font(9.5));
	about->addWidget(credits);
	layout->addLayout(about);
}
