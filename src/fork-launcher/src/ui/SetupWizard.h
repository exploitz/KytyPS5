#pragma once

#include "core/GameInfo.h"
#include "core/Package.h"
#include "core/Settings.h"
#include "core/SystemCheck.h"

#include <QWidget>

class ChoiceCard;
class DarkCombo;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class StateIcon;
class StepBar;
class Tag;

// The setup: 1 PC (a checklist), 2 Game (the folder or .zar), 3 Display (recommended settings), 4 Shaders (precompile
// now or later). Shown on the first run and from Settings > Run setup again.
class SetupWizard final: public QWidget {
	Q_OBJECT

public:
	SetupWizard(const Package& package, SettingsStore* store, bool screenshotMode, QWidget* parent = nullptr);

	// Starts over at `step`; `cancellable`: a Cancel button back to the launcher (the setup was done before).
	void begin(bool cancellable, int step = 0);
	void showStep(int step);
	// --screenshots: the display page's screen and the shader state.
	void setShadersComplete(bool complete);

signals:
	void finished(bool precompileNow);
	void cancelled();

private:
	QWidget* makePcPage();
	QWidget* makeGamePage();
	QWidget* makeDisplayPage();
	QWidget* makeShadersPage();
	void     refreshPc();
	void     setGame(const QString& path);
	void     refreshDisplay();
	void     refreshShaders();
	void     finish();

	Package        m_package;
	SettingsStore* m_store;
	bool           m_screenshots;
	bool           m_cancellable = false;
	int            m_step        = 0;
	bool           m_shaders_complete = false;

	StepBar*        m_steps;
	QLabel*         m_title;
	QLabel*         m_intro;
	QStackedWidget* m_pages;
	QPushButton*    m_back;
	QPushButton*    m_next;
	QPushButton*    m_cancel;

	QVBoxLayout* m_pc_list;
	QLabel*      m_pc_note;

	QLineEdit*  m_game_path;
	QWidget*    m_game_card;
	QLabel*     m_game_icon;
	QLabel*     m_game_title;
	QLabel*     m_game_line;
	QWidget*    m_game_message;
	StateIcon*  m_game_message_icon;
	QLabel*     m_game_message_text;
	GameInfo    m_game;
	bool        m_game_ok = false;

	QLabel*    m_display_context;
	DarkCombo* m_output;
	DarkCombo* m_quality;
	DarkCombo* m_screen;
	Tag*       m_output_tag;
	Tag*       m_quality_tag;
	Tag*       m_screen_tag;
	Tag*       m_output_use;
	Tag*       m_quality_use;
	Tag*       m_screen_use;
	QLabel*    m_output_note;
	QLabel*    m_quality_note;
	QLabel*    m_screen_note;
	QString    m_recommended_output, m_recommended_quality;

	QLabel*     m_shader_text;
	ChoiceCard* m_now;
	ChoiceCard* m_later;
	QWidget*    m_shader_done;
};
