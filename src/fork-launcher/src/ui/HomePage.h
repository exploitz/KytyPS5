#pragma once

#include "core/GameInfo.h"
#include "core/Precompile.h"
#include "ui/Widgets.h"

#include <QWidget>

class QPlainTextEdit;

// Home: the game's banner, PLAY with its status, the shader state or the precompile's progress card, an error card
// after a failed run, the live log (collapsed) and quick links.
class HomePage final: public QWidget {
	Q_OBJECT

public:
	explicit HomePage(QWidget* parent = nullptr);

	// `async`: load the key art in the background (not for --screenshots).
	void setGame(const GameInfo& game, bool hasGame, bool async);
	void setPlay(PlayButton::Look look, const QString& text, ThinBar::Mode bar, Tone barTone = Tone::Accent);
	[[nodiscard]] PlayButton::Look playLook() const;
	// A reason under PLAY (no AVX2, files missing); empty: none.
	void setPlayReason(const QString& reason);
	void setShaderState(Tone tone, const QString& text, const QString& action, const QString& hint);
	void setSummary(const QString& summary);
	void setUpdate(const QString& tag);

	void showPrecompile(const PrecompileProgress& progress);
	void showPrecompileResult(bool ok, const QString& title, const QString& detail);
	void hidePrecompile();
	[[nodiscard]] bool precompileShown() const;

	void showError(const QString& sentence, const QString& details);
	void hideError();
	void setErrorDetailsShown(bool shown);
	void showNotice(Tone tone, const QString& text, const QString& action);
	void hideNotice();

	void appendLog(const QString& text);
	[[nodiscard]] QString logText() const;
	void setLogShown(bool shown);
	[[nodiscard]] bool logShown() const;

signals:
	void playClicked();
	void shaderChipClicked();
	void stopPrecompileClicked();
	void precompileCardClosed();
	void reportClicked();
	void settingsClicked();
	void noticeActionClicked();
	void updateClicked();

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	void placeUpdateChip();
	void setStatusPage(int index);
	void updateHeroHeight();

	HeroBanner*     m_hero;
	QLabel*         m_icon;
	QLabel*         m_title;
	QLabel*         m_line;
	Chip*           m_update;
	PlayButton*     m_play;
	ThinBar*        m_play_bar;
	QLabel*         m_play_reason;
	QWidget*        m_status;
	QWidget*        m_chips;
	QWidget*        m_card;
	int             m_status_page = 0;
	Chip*           m_shader_chip;
	QLabel*         m_shader_hint;
	QLabel*         m_summary;
	QLabel*         m_pc_title;
	QLabel*         m_pc_percent;
	ProgressBar*    m_pc_bar;
	QLabel*         m_pc_step;
	QLabel*         m_pc_time;
	QLabel*         m_pc_hint;
	QPushButton*    m_pc_stop;
	QPushButton*    m_pc_report;
	StateIcon*      m_pc_icon;
	bool            m_pc_finished = false;
	QFrame*         m_error;
	QLabel*         m_error_text;
	QPushButton*    m_error_details;
	QPlainTextEdit* m_error_log;
	QFrame*         m_notice;
	StateIcon*      m_notice_icon;
	QLabel*         m_notice_text;
	QPushButton*    m_notice_action;
	QPushButton*    m_log_toggle;
	QPlainTextEdit* m_log;
	QWidget*        m_log_spacer;
	int             m_image_generation = 0;
};
