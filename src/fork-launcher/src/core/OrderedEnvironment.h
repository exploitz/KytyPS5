#pragma once

#include <QList>
#include <QPair>
#include <QString>

// The switches of a launch in the order they were set: PowerShell's [ordered]@{} as run-windows.ps1 uses it (keys
// compare case-insensitively; setting an existing key keeps its place; a new key goes to the end).
class OrderedEnvironment {
public:
	[[nodiscard]] bool contains(const QString& key) const { return indexOf(key) >= 0; }

	// The value, or an empty string when the key is not set (PowerShell's $null compares like '').
	[[nodiscard]] QString value(const QString& key) const {
		const int at = indexOf(key);
		return at >= 0 ? m_entries.at(at).second : QString();
	}

	void set(const QString& key, const QString& value) {
		const int at = indexOf(key);
		if (at >= 0) m_entries[at].second = value;
		else m_entries.append({key, value});
	}

	void setIfMissing(const QString& key, const QString& value) {
		if (!contains(key)) set(key, value);
	}

	void remove(const QString& key) {
		const int at = indexOf(key);
		if (at >= 0) m_entries.removeAt(at);
	}

	[[nodiscard]] int                                count() const { return static_cast<int>(m_entries.size()); }
	[[nodiscard]] const QList<QPair<QString, QString>>& entries() const { return m_entries; }

private:
	[[nodiscard]] int indexOf(const QString& key) const {
		for (int i = 0; i < m_entries.size(); i++) {
			if (m_entries.at(i).first.compare(key, Qt::CaseInsensitive) == 0) return i;
		}
		return -1;
	}

	QList<QPair<QString, QString>> m_entries;
};
