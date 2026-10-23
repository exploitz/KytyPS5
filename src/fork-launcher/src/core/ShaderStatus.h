#pragma once

#include "core/Caches.h"
#include "core/GameInfo.h"
#include "core/Package.h"

#include <QObject>
#include <QPointer>
#include <QProcess>

// The shader state the window shows: the static cache as files tell (at once), refined by
// kyty_shader_precompile --status (whether the inputs and the cache are this GPU's and driver's) in the background.
class ShaderStatus final: public QObject {
	Q_OBJECT

public:
	struct State {
		Caches::StaticState files = Caches::StaticState::None;
		bool                queried       = false; // --status answered
		bool                inputsCurrent = false;
		QString             staticCache;           // "current", "outdated" or "stale" (when queried)

		// A precompile ran before, and the driver or the GPU changed since (the inputs or the cache are not current).
		[[nodiscard]] bool driverChanged() const {
			return files == Caches::StaticState::Done && queried &&
			       (staticCache == QStringLiteral("stale") || (staticCache == QStringLiteral("current") && !inputsCurrent));
		}
		// The emulator lists more shaders than the ones the cache was made from.
		[[nodiscard]] bool outdated() const { return files == Caches::StaticState::Done && queried && staticCache == QStringLiteral("outdated"); }
		[[nodiscard]] bool ready() const { return files == Caches::StaticState::Done && !driverChanged() && !outdated(); }
	};

	explicit ShaderStatus(QObject* parent = nullptr);
	~ShaderStatus() override;

	// `query`: also run --status (not in --screenshots).
	void refresh(const Package& package, const GameInfo& game, bool query);
	// What a play launch's --status said.
	void setQueried(bool inputsCurrent, const QString& staticCache);
	[[nodiscard]] const State& state() const { return m_state; }

signals:
	void changed();

private:
	State              m_state;
	QPointer<QProcess> m_process;
};
