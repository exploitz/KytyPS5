#include "ui/SettingsPage.h"

#include "ui/Choices.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QAbstractItemView>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QLabel* SectionLabel(const QString& text, QWidget* parent) {
	auto* label = makeLabel(text, "secondary", parent);
	QFont font  = Theme::font(8, QFont::DemiBold);
	font.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
	label->setFont(font);
	label->setContentsMargins(4, 10, 0, 2);
	return label;
}

QString Value(const LauncherSettings& s, const QString& key) {
	if (key == QStringLiteral("output")) return s.output;
	if (key == QStringLiteral("quality")) return s.quality;
	if (key == QStringLiteral("aa")) return s.aa;
	if (key == QStringLiteral("framegen")) return s.framegen;
	if (key == QStringLiteral("render")) return QString::number(s.render);
	if (key == QStringLiteral("ratio")) return s.ratio;
	if (key == QStringLiteral("screen")) return s.screen;
	if (key == QStringLiteral("fps")) return QString::number(s.fps);
	if (key == QStringLiteral("language")) return QString::number(s.language);
	if (key == QStringLiteral("hud")) return QString::number(s.hud);
	if (key == QStringLiteral("aspect")) return s.aspect ? QStringLiteral("true") : QStringLiteral("false");
	if (key == QStringLiteral("vsync")) return s.vsync ? QStringLiteral("true") : QStringLiteral("false");
	if (key == QStringLiteral("hdr")) return s.hdr ? QStringLiteral("true") : QStringLiteral("false");
	if (key == QStringLiteral("redzone")) return s.redzone ? QStringLiteral("true") : QStringLiteral("false");
	if (key == QStringLiteral("ecores")) return s.ecores ? QStringLiteral("true") : QStringLiteral("false");
	return {};
}

const QList<Choice>* ChoicesOf(const QString& key) {
	if (key == QStringLiteral("output")) return &Choices::output();
	if (key == QStringLiteral("quality")) return &Choices::quality();
	if (key == QStringLiteral("aa")) return &Choices::antiAliasing();
	if (key == QStringLiteral("framegen")) return &Choices::frameGeneration();
	if (key == QStringLiteral("render")) return &Choices::render();
	if (key == QStringLiteral("ratio")) return &Choices::ratio();
	if (key == QStringLiteral("screen")) return &Choices::screen();
	if (key == QStringLiteral("fps")) return &Choices::fps();
	if (key == QStringLiteral("hud")) return &Choices::overlay();
	return nullptr;
}

} // namespace

// --- SettingsRow ------------------------------------------------------------------------------------------------------

SettingsRow::SettingsRow(const QString& key, const QString& label, QWidget* control, QWidget* parent)
    : QWidget(parent), m_key(key), m_control(control) {
	setMinimumHeight(48);
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(14, 6, 10, 6);
	layout->setSpacing(16);
	m_label = makeLabel(label, nullptr, this);
	layout->addWidget(m_label);
	layout->addStretch(1);
	control->setParent(this);
	layout->addWidget(control);
	control->installEventFilter(this);
	if (auto* combo = qobject_cast<DarkCombo*>(control)) combo->view()->installEventFilter(this);
	control->setAccessibleName(label);
}

void SettingsRow::setActive(bool active) {
	m_active = active;
	update();
}

void SettingsRow::setRowEnabled(bool enabled) {
	m_control->setEnabled(enabled);
	Theme::setRole(m_label, "role", enabled ? QString() : QStringLiteral("muted"));
}

void SettingsRow::paintEvent(QPaintEvent*) {
	if (!m_hover && !m_active) return;
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0x24, 0x24, 0x24));
	p.drawRoundedRect(QRectF(rect()), Theme::kRadius, Theme::kRadius);
}

void SettingsRow::enterEvent(QEnterEvent* event) {
	m_hover = true;
	update();
	emit activated(this);
	QWidget::enterEvent(event);
}

void SettingsRow::leaveEvent(QEvent* event) {
	m_hover = false;
	update();
	QWidget::leaveEvent(event);
}

void SettingsRow::mouseReleaseEvent(QMouseEvent* event) {
	// A click anywhere on a switch's row flips it.
	if (auto* toggle = qobject_cast<ToggleSwitch*>(m_control); toggle != nullptr && toggle->isEnabled() && event->button() == Qt::LeftButton) {
		toggle->click();
	}
	QWidget::mouseReleaseEvent(event);
}

bool SettingsRow::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() == QEvent::FocusIn && watched == m_control) emit activated(this);
	return QWidget::eventFilter(watched, event);
}

// --- SettingsPage -----------------------------------------------------------------------------------------------------

SettingsPage::SettingsPage(SettingsStore* store, bool hybridCpu, QWidget* parent): QWidget(parent), m_store(store) {
	setObjectName(QStringLiteral("Page"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(32, 24, 32, 20);
	layout->setSpacing(12);

	auto* header = new QHBoxLayout;
	header->addWidget(makeLabel(tr("Settings"), "title", this));
	header->addStretch(1);
	auto* setup = makeButton(tr("Run setup again"), nullptr, this);
	connect(setup, &QPushButton::clicked, this, &SettingsPage::runSetupClicked);
	header->addWidget(setup, 0, Qt::AlignVCenter);
	layout->addLayout(header);

	m_scroll = new QScrollArea(this);
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	// (Tab goes from row to row, not to the scrolling area itself.)
	m_scroll->setFocusPolicy(Qt::NoFocus);
	auto* content        = new QWidget(m_scroll);
	auto* content_layout = new QVBoxLayout(content);
	content_layout->setContentsMargins(0, 0, 8, 4);
	content_layout->setSpacing(6);
	m_scroll->setWidget(content);
	layout->addWidget(m_scroll, 1);

	auto combo = [&](const QList<Choice>& choices) {
		auto* box = new DarkCombo;
		for (const Choice& choice: choices) box->addItem(choice.label, choice.value);
		box->setFixedWidth(250);
		return box;
	};
	auto section = [&](const QString& name) {
		content_layout->addWidget(SectionLabel(name, content));
		auto* card = makeCard(content);
		auto* card_layout = new QVBoxLayout(card);
		card_layout->setContentsMargins(6, 6, 6, 6);
		card_layout->setSpacing(0);
		content_layout->addWidget(card);
		return card;
	};

	QWidget* display = section(tr("DISPLAY"));
	addRow(display, QStringLiteral("output"), tr("Output"), combo(Choices::output()));
	addRow(display, QStringLiteral("screen"), tr("Display mode"), combo(Choices::screen()));
	addRow(display, QStringLiteral("aspect"), tr("Keep 16:9"), new ToggleSwitch);
	addRow(display, QStringLiteral("fps"), tr("Frame rate limit"), combo(Choices::fps()));
	addRow(display, QStringLiteral("vsync"), tr("VSync"), new ToggleSwitch);
	addRow(display, QStringLiteral("hdr"), tr("HDR"), new ToggleSwitch);
	addRow(display, QStringLiteral("hud"), tr("Performance overlay"), combo(Choices::overlay()));
	QWidget* graphics = section(tr("GRAPHICS"));
	addRow(graphics, QStringLiteral("quality"), tr("Graphics detail"), combo(Choices::quality()));
	addRow(graphics, QStringLiteral("aa"), tr("Anti-aliasing"), combo(Choices::antiAliasing()));
	addRow(graphics, QStringLiteral("framegen"), tr("Frame generation"), combo(Choices::frameGeneration()));
	addRow(graphics, QStringLiteral("render"), tr("3D resolution"), combo(Choices::render()));
	addRow(graphics, QStringLiteral("ratio"), tr("Aspect ratio"), combo(Choices::ratio()));
	QWidget* system   = section(tr("SYSTEM"));
	auto*    language = new DarkCombo;
	for (int i = 0; i < Choices::languages().size(); i++) language->addItem(Choices::languages().at(i), QString::number(i));
	language->setFixedWidth(250);
	language->setMaxVisibleItems(12);
	addRow(system, QStringLiteral("language"), tr("Language"), language);
	// Advanced: what most players never need to touch, folded away (open when one of them is not at its default).
	auto* advanced_toggle = makeButton(tr("Show advanced settings"), "text", content);
	content_layout->addWidget(advanced_toggle, 0, Qt::AlignLeft);
	auto* advanced_label = SectionLabel(tr("ADVANCED"), content);
	content_layout->addWidget(advanced_label);
	auto* advanced        = makeCard(content);
	auto* advanced_layout = new QVBoxLayout(advanced);
	advanced_layout->setContentsMargins(6, 6, 6, 6);
	advanced_layout->setSpacing(0);
	content_layout->addWidget(advanced);
	addRow(advanced, QStringLiteral("redzone"), tr("Memory protection"), new ToggleSwitch);
	if (hybridCpu) addRow(advanced, QStringLiteral("ecores"), tr("Precompile on E-cores"), new ToggleSwitch);
	auto show_advanced = [advanced_toggle, advanced_label, advanced, this](bool show) {
		advanced_label->setVisible(show);
		advanced->setVisible(show);
		advanced_toggle->setText(show ? tr("Hide advanced settings") : tr("Show advanced settings"));
	};
	connect(advanced_toggle, &QPushButton::clicked, this, [advanced, show_advanced] { show_advanced(advanced->isHidden()); });
	show_advanced(!m_store->settings().redzone || m_store->settings().ecores);
	content_layout->addStretch(1);

	// The description panel.
	auto* panel        = makeCard(this);
	auto* panel_layout = new QVBoxLayout(panel);
	panel_layout->setContentsMargins(16, 12, 16, 12);
	panel_layout->setSpacing(4);
	m_description_title = makeLabel({}, nullptr, panel);
	m_description_title->setFont(Theme::font(10.5, QFont::DemiBold));
	panel_layout->addWidget(m_description_title);
	m_description_text = makeLabel({}, "secondary", panel);
	m_description_text->setWordWrap(true);
	m_description_text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	panel_layout->addWidget(m_description_text, 1);
	panel->setFixedHeight(80);
	layout->addWidget(panel);

	reload();
	describe(m_rows.value(QStringLiteral("output")));
}

SettingsRow* SettingsPage::addRow(QWidget* card, const QString& key, const QString& label, QWidget* control) {
	auto* layout = static_cast<QVBoxLayout*>(card->layout());
	if (!m_rows.isEmpty() && layout->count() > 0) layout->addWidget(makeSeparator(card));
	auto* row = new SettingsRow(key, label, control, card);
	layout->addWidget(row);
	m_rows.insert(key, row);
	connect(row, &SettingsRow::activated, this, [this](SettingsRow* r) { describe(r); });
	if (auto* box = qobject_cast<DarkCombo*>(control)) {
		m_combos.insert(key, box);
		connect(box, &QComboBox::currentIndexChanged, this, [this, key] { commit(key); });
		connect(box, &QComboBox::highlighted, this, [this, row](int index) { describe(row, index); });
	} else if (auto* toggle = qobject_cast<ToggleSwitch*>(control)) {
		m_toggles.insert(key, toggle);
		connect(toggle, &ToggleSwitch::toggled, this, [this, key] { commit(key); });
	}
	return row;
}

void SettingsPage::reload() {
	m_loading                 = true;
	const LauncherSettings& s = m_store->settings();
	for (auto it = m_combos.begin(); it != m_combos.end(); ++it) {
		const int index = it.value()->findData(Value(s, it.key()));
		it.value()->setCurrentIndex(std::max(0, index));
	}
	for (auto it = m_toggles.begin(); it != m_toggles.end(); ++it) it.value()->setChecked(Value(s, it.key()) == QStringLiteral("true"));
	m_loading = false;
	updateEnabled();
	if (m_active != nullptr) describe(m_active);
}

void SettingsPage::updateEnabled() {
	const LauncherSettings& s = m_store->settings();
	if (auto* row = m_rows.value(QStringLiteral("render"))) row->setRowEnabled(s.output == QStringLiteral("4k"));
	if (auto* row = m_rows.value(QStringLiteral("ratio"))) row->setRowEnabled(s.output == QStringLiteral("4k"));
	if (auto* row = m_rows.value(QStringLiteral("aspect"))) row->setRowEnabled(s.screen == QStringLiteral("fullscreen"));
}

void SettingsPage::commit(const QString& key) {
	if (m_loading) return;
	LauncherSettings& s        = m_store->settings();
	const QString     previous = Value(s, key);
	if (auto* box = m_combos.value(key)) {
		const QString value = box->currentData().toString();
		if (key == QStringLiteral("output")) s.output = value;
		else if (key == QStringLiteral("quality")) s.quality = value;
		else if (key == QStringLiteral("aa")) s.aa = value;
		else if (key == QStringLiteral("framegen")) s.framegen = value;
		else if (key == QStringLiteral("render")) s.render = value.toInt();
		else if (key == QStringLiteral("ratio")) s.ratio = value;
		else if (key == QStringLiteral("screen")) s.screen = value;
		else if (key == QStringLiteral("fps")) s.fps = value.toInt();
		else if (key == QStringLiteral("language")) s.language = value.toInt();
		else if (key == QStringLiteral("hud")) s.hud = value.toInt();
	} else if (auto* toggle = m_toggles.value(key)) {
		const bool on = toggle->isChecked();
		if (key == QStringLiteral("aspect")) s.aspect = on;
		else if (key == QStringLiteral("vsync")) s.vsync = on;
		else if (key == QStringLiteral("hdr")) s.hdr = on;
		else if (key == QStringLiteral("redzone")) s.redzone = on;
		else if (key == QStringLiteral("ecores")) s.ecores = on;
	}
	m_store->save();
	updateEnabled();
	if (auto* row = m_rows.value(key)) describe(row);
	emit changed(key, previous);
}

void SettingsPage::describe(SettingsRow* row, int highlighted) {
	if (row == nullptr) return;
	if (m_active != nullptr && m_active != row) m_active->setActive(false);
	m_active = row;
	row->setActive(true);
	const QString&          key = row->key();
	const LauncherSettings& s   = m_store->settings();
	QString                 label, text;
	if (const QList<Choice>* choices = ChoicesOf(key)) {
		const int index = highlighted >= 0 ? highlighted : Choices::indexOf(*choices, Value(s, key));
		label           = choices->value(index).label;
		text            = choices->value(index).description;
	}
	QString name;
	if (key == QStringLiteral("output")) name = tr("Output");
	else if (key == QStringLiteral("quality")) name = tr("Graphics detail");
	else if (key == QStringLiteral("aa")) name = tr("Anti-aliasing");
	else if (key == QStringLiteral("framegen")) name = tr("Frame generation");
	else if (key == QStringLiteral("render")) {
		name = tr("3D resolution");
		text = Choices::renderDescription();
	} else if (key == QStringLiteral("ratio")) {
		name = tr("Aspect ratio");
	} else if (key == QStringLiteral("screen")) {
		name = tr("Display mode");
		text = Choices::screenDescription();
	} else if (key == QStringLiteral("fps")) name = tr("Frame rate limit");
	else if (key == QStringLiteral("hud")) name = tr("Performance overlay");
	else if (key == QStringLiteral("language")) {
		name  = tr("Language");
		label = Choices::languages().value(highlighted >= 0 ? highlighted : s.language);
		text  = Choices::languageDescription();
	} else {
		const bool on = Value(s, key) == QStringLiteral("true");
		label         = on ? tr("On") : tr("Off");
		if (key == QStringLiteral("aspect")) {
			name = tr("Keep 16:9");
			text = Choices::aspectDescription();
			if (s.screen != QStringLiteral("fullscreen")) text += tr(" Only with fullscreen.");
		} else if (key == QStringLiteral("vsync")) {
			name = tr("VSync");
			text = Choices::vsyncDescription();
		} else if (key == QStringLiteral("hdr")) {
			name = tr("HDR");
			text = Choices::hdrDescription();
		} else if (key == QStringLiteral("redzone")) {
			name = tr("Memory protection");
			text = Choices::redzoneDescription();
		} else if (key == QStringLiteral("ecores")) {
			name = tr("Precompile on E-cores");
			text = Choices::ecoresDescription();
		}
	}
	m_description_title->setText(label.isEmpty() ? name : QStringLiteral("%1  ·  %2").arg(name, label));
	m_description_text->setText(text);
}

void SettingsPage::simulateHover(const QString& key) {
	SettingsRow* row = m_rows.value(key);
	if (row == nullptr) return;
	m_scroll->ensureWidgetVisible(row, 0, 24);
	row->setActive(true);
	describe(row);
}
