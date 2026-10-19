#pragma once

#include "core/Settings.h"

#include <QHash>
#include <QWidget>

class DarkCombo;
class QLabel;
class QScrollArea;
class ToggleSwitch;

// One setting: its label on the left, its control on the right; under the mouse or with focus it fills the page's
// description panel.
class SettingsRow final: public QWidget {
	Q_OBJECT
public:
	SettingsRow(const QString& key, const QString& label, QWidget* control, QWidget* parent = nullptr);
	[[nodiscard]] const QString& key() const { return m_key; }
	[[nodiscard]] QWidget*       control() const { return m_control; }
	void                         setActive(bool active);
	void                         setRowEnabled(bool enabled);

signals:
	void activated(SettingsRow* row);

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	QString  m_key;
	QLabel*  m_label;
	QWidget* m_control;
	bool     m_hover  = false;
	bool     m_active = false;
};

// Settings: DISPLAY, GRAPHICS and SYSTEM rows and one description panel at the bottom. Every change is saved at once.
class SettingsPage final: public QWidget {
	Q_OBJECT

public:
	SettingsPage(SettingsStore* store, bool hybridCpu, QWidget* parent = nullptr);

	// The controls from the store (after a change elsewhere, or a change taken back).
	void reload();
	// --screenshots: a row as if the mouse were over it.
	void simulateHover(const QString& key);

signals:
	// `key` changed; `previous` is its old value (for taking it back).
	void changed(const QString& key, const QString& previous);
	void runSetupClicked();

private:
	SettingsRow* addRow(QWidget* card, const QString& key, const QString& label, QWidget* control);
	void         describe(SettingsRow* row, int highlighted = -1);
	void         updateEnabled();
	void         commit(const QString& key);

	SettingsStore*              m_store;
	bool                        m_loading = false;
	QHash<QString, SettingsRow*> m_rows;
	QHash<QString, DarkCombo*>   m_combos;
	QHash<QString, ToggleSwitch*> m_toggles;
	SettingsRow*                m_active = nullptr;
	QScrollArea*                m_scroll;
	QLabel*                     m_description_title;
	QLabel*                     m_description_text;
};
