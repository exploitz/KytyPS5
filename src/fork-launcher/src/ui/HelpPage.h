#pragma once

#include "ui/Icons.h"

#include <QAbstractButton>
#include <QWidget>

// A row of the Help page: an icon, a title and a line, an arrow; the whole row is the button.
class ActionRow final: public QAbstractButton {
	Q_OBJECT
public:
	ActionRow(Icons::Icon icon, const QString& title, const QString& line, bool external, QWidget* parent = nullptr);
	void                setLine(const QString& line);
	[[nodiscard]] QSize sizeHint() const override { return {560, 60}; }

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	Icons::Icon m_icon;
	QString     m_line;
	bool        m_external;
	bool        m_hover = false;
};

// Help: the guide, the logs, reporting a problem, the releases page; the version and the credits.
class HelpPage final: public QWidget {
	Q_OBJECT
public:
	explicit HelpPage(const QString& logFolder, QWidget* parent = nullptr);

signals:
	void openGuide();
	void openLogs();
	void reportProblem();
	void exportHints();
	void openReleases();
};
