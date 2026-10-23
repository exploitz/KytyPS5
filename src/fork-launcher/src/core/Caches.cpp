#include "core/Caches.h"

#include "core/GameInfo.h"
#include "core/Package.h"
#include "platform/Platform.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace Caches {

QStringList renameTitleCaches(const Package& package, const GameInfo& game) {
	QStringList done;
	if (!game.paramRead || game.titleId.isEmpty() || game.contentVersion.isEmpty()) return done;
	const QString title = game.titleId;
	const QString id    = game.cacheId();
	QList<QPair<QString, QString>> moves;
	for (const QString& dir: {package.root, package.path(QStringLiteral("_Build/static-precompile"))}) {
		moves.append({QDir(dir).filePath(QStringLiteral("seeds.seeds")), QDir(dir).filePath(QStringLiteral("seeds-%1.seeds").arg(id))});
		moves.append({QDir(dir).filePath(QStringLiteral("recorded.seeds")), QDir(dir).filePath(QStringLiteral("recorded-%1.seeds").arg(id))});
	}
	const QDir static_dir(package.path(QStringLiteral("_PipelineCache/static")));
	for (const QFileInfo& file: static_dir.entryInfoList({title + QStringLiteral(".*")}, QDir::Files)) {
		moves.append({file.absoluteFilePath(), static_dir.filePath(id + file.fileName().mid(title.size()))});
	}
	const QDir warmup(package.path(QStringLiteral("_PipelineCache/warmup-v2")));
	for (const QString& sub: warmup.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		const QDir dir(warmup.filePath(sub));
		if (QFileInfo::exists(dir.filePath(title + QStringLiteral(".shaders")))) {
			moves.append({dir.filePath(title + QStringLiteral(".shaders")), dir.filePath(id + QStringLiteral(".shaders"))});
		}
	}
	for (const auto& move: moves) {
		if (!QFileInfo::exists(move.first) || QFileInfo::exists(move.second)) continue;
		if (QFile::rename(move.first, move.second)) {
			done.append(QStringLiteral("caches:   %1 is %2's").arg(QDir::toNativeSeparators(QDir(package.root).relativeFilePath(move.first)), id));
		}
	}
	return done;
}

QString programStamp(const QString& tool) {
	const QFileInfo info(tool);
	if (!info.exists()) return {};
	return QStringLiteral("%1 %2").arg(info.size()).arg(Platform::fileWriteTicks(tool));
}

QString readProgramStamp(const QString& seeds) {
	QFile file(seeds + QStringLiteral(".program"));
	if (!file.open(QIODevice::ReadOnly)) return {};
	QByteArray data = file.readAll();
	if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
	return QString::fromUtf8(data).trimmed();
}

bool writeProgramStamp(const QString& seeds, const QString& stamp) {
	// (Set-Content: the text and a line break.)
	QFile file(seeds + QStringLiteral(".program"));
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
	file.write((stamp + QStringLiteral("\r\n")).toLatin1());
	return true;
}

bool sameContent(const QString& a, const QString& b) {
	auto hash = [](const QString& path) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) return QByteArray();
		QCryptographicHash sha(QCryptographicHash::Sha256);
		sha.addData(&file);
		return sha.result();
	};
	const QByteArray first = hash(a);
	return !first.isEmpty() && first == hash(b);
}

qint64 seedPipelines(const QString& path) {
	// The body after the identity line and checksum: the records, each its word count and words, then the recipes' count.
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return -1;
	char c = 0;
	do {
		if (!file.getChar(&c)) return -1;
	} while (c != '\n');
	QDataStream stream(&file);
	stream.setByteOrder(QDataStream::LittleEndian);
	quint64 checksum = 0;
	quint32 records  = 0;
	stream >> checksum >> records;
	for (quint32 i = 0; i < records && stream.status() == QDataStream::Ok; i++) {
		quint32 words = 0;
		stream >> words;
		if (!file.seek(file.pos() + qint64(words) * 4)) return -1;
	}
	quint32 recipes = 0;
	stream >> recipes;
	return stream.status() == QDataStream::Ok ? qint64(recipes) : -1;
}

StaticState staticState(const Package& package, const QString& cacheId) {
	if (cacheId.isEmpty()) return StaticState::None;
	const QDir static_dir(package.path(QStringLiteral("_PipelineCache/static")));
	// (The shards are named after the store they join: <cacheId>.binaries.shard3of214-<seeds>.)
	for (const QFileInfo& file: static_dir.entryInfoList({cacheId + QStringLiteral("*.shard*")}, QDir::Files)) {
		if (file.size() > 1024 * 1024) return StaticState::Partial;
	}
	for (const char* extension: {".binaries", ".bin", ".blobs"}) {
		if (QFileInfo::exists(static_dir.filePath(cacheId + QString::fromLatin1(extension)))) return StaticState::Done;
	}
	return StaticState::None;
}

bool hasShardCheckpoints(const Package& package) {
	const QDir static_dir(package.path(QStringLiteral("_PipelineCache/static")));
	for (const QFileInfo& file: static_dir.entryInfoList({QStringLiteral("*.shard*")}, QDir::Files)) {
		if (file.size() > 1024 * 1024) return true;
	}
	return false;
}

} // namespace Caches
