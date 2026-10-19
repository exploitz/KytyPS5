#pragma once

#include "ui/Widgets.h"

#include <QDialog>
#include <QStringList>

class ProgressBar;

// The launcher's message dialog: dark, an icon, one sentence of what is wrong and one of what to do, and its buttons
// (the last one is the primary, filled in gold).
class MessageDialog final: public QDialog {
	Q_OBJECT
public:
	MessageDialog(QWidget* parent, Tone tone, const QString& title, const QString& body, const QStringList& buttons);

	// Shows it and returns the index of the button chosen (-1: closed).
	static int ask(QWidget* parent, Tone tone, const QString& title, const QString& body, const QStringList& buttons);
	static void tell(QWidget* parent, Tone tone, const QString& title, const QString& body);
	// --screenshots: no native title bar styling (no native window is made for a dialog drawn off screen).
	static void setScreenshotMode(bool on);

	// A progress bar under the text (the DLSS add-on's download).
	void        showProgress(bool show);
	void        setProgress(double fraction, const QString& text);
	void        setBody(const QString& body);
	QPushButton* button(int index) const;

private:
	QLabel*            m_body;
	QLabel*            m_progress_text;
	ProgressBar*       m_progress;
	QList<QPushButton*> m_buttons;
};
