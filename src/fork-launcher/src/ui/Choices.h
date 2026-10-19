#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct LauncherSettings;

// The settings' choices: the value saved in launcher-settings.json, the label shown and its explanation.
struct Choice {
	QString value;
	QString label;
	QString description;
};

namespace Choices {

const QList<Choice>& output();
const QList<Choice>& quality();
const QList<Choice>& antiAliasing();
const QList<Choice>& frameGeneration();
const QList<Choice>& render();
const QList<Choice>& ratio();
const QList<Choice>& screen();
const QList<Choice>& fps();
const QList<Choice>& overlay();
// The PS5's 30 console languages, 0-29.
const QStringList& languages();

int     indexOf(const QList<Choice>& choices, const QString& value);
QString label(const QList<Choice>& choices, const QString& value);

// Explanations of the rows without per-choice ones.
QString renderDescription();
QString screenDescription();
QString aspectDescription();
QString vsyncDescription();
QString hdrDescription();
QString languageDescription();
QString redzoneDescription();
QString ecoresDescription();

// "1080p Quality · Standard detail · Fullscreen" (and what else differs from the defaults).
QString summary(const LauncherSettings& settings);

} // namespace Choices
