#include "ui/Dialogs.h"

#include "platform/Platform.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {
bool g_screenshots = false;
} // namespace

void MessageDialog::setScreenshotMode(bool on) {
	g_screenshots = on;
}

MessageDialog::MessageDialog(QWidget* parent, Tone tone, const QString& title, const QString& body, const QStringList& buttons)
    : QDialog(parent) {
	setWindowTitle(QStringLiteral("KytyPS5 Launcher"));
	setWindowFlag(Qt::WindowContextHelpButtonHint, false);
	setModal(true);
	setMinimumWidth(460);
	setMaximumWidth(560);
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(24, 22, 24, 20);
	layout->setSpacing(10);
	// The icon on the title's line; the text and the progress under the title.
	auto* title_row = new QHBoxLayout;
	title_row->setSpacing(12);
	auto* icon = new StateIcon(tone == Tone::Neutral || tone == Tone::Accent ? Tone::Accent : tone, 22, this);
	title_row->addWidget(icon, 0, Qt::AlignVCenter);
	auto* heading = makeLabel(title, "heading", this);
	heading->setWordWrap(true);
	title_row->addWidget(heading, 1, Qt::AlignVCenter);
	layout->addLayout(title_row);
	auto* text = new QVBoxLayout;
	text->setContentsMargins(34, 0, 0, 0);
	text->setSpacing(6);
	m_body = makeLabel(body, "secondary", this);
	m_body->setWordWrap(true);
	m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);
	text->addWidget(m_body);
	m_progress = new ProgressBar(this);
	m_progress->hide();
	text->addSpacing(4);
	text->addWidget(m_progress);
	m_progress_text = makeLabel({}, "secondary", this);
	m_progress_text->hide();
	text->addWidget(m_progress_text);
	layout->addLayout(text);
	auto* row = new QHBoxLayout;
	row->setSpacing(8);
	row->addStretch(1);
	for (int i = 0; i < buttons.size(); i++) {
		const bool primary = i == buttons.size() - 1;
		auto*      button  = makeButton(buttons.at(i), primary ? "primary" : nullptr, this);
		button->setFocusPolicy(Qt::StrongFocus);
		button->setMinimumWidth(96);
		connect(button, &QPushButton::clicked, this, [this, i] { done(i + 1); });
		row->addWidget(button);
		m_buttons.append(button);
		if (primary) {
			button->setDefault(true);
			button->setFocus();
		}
	}
	layout->addSpacing(8);
	layout->addLayout(row);
	if (!g_screenshots) Platform::styleTitleBar(this);
}

int MessageDialog::ask(QWidget* parent, Tone tone, const QString& title, const QString& body, const QStringList& buttons) {
	MessageDialog dialog(parent, tone, title, body, buttons);
	// (The buttons end it with their index + 1; Escape and the close button with 0, rejected.)
	return dialog.exec() - 1;
}

void MessageDialog::tell(QWidget* parent, Tone tone, const QString& title, const QString& body) {
	ask(parent, tone, title, body, {QStringLiteral("OK")});
}

void MessageDialog::showProgress(bool show) {
	m_progress->setVisible(show);
	m_progress_text->setVisible(show);
}

void MessageDialog::setProgress(double fraction, const QString& text) {
	m_progress->setValue(fraction);
	m_progress_text->setText(text);
}

void MessageDialog::setBody(const QString& body) {
	m_body->setText(body);
}

QPushButton* MessageDialog::button(int index) const {
	return m_buttons.value(index);
}
