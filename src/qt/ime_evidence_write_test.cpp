// Console-only evidence transactions: no windows, receivers or input APIs.
#include "ime_native_observer.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

using namespace termforge::qt::ime_native;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
QJsonObject context(const EvidenceWriteFailure& failure) {
    require(failure.snapshot().isObject(), "Expected a structured evidence failure");
    return failure.snapshot().toObject();
}
void identity(const QJsonObject& row, const char* operation, const char* basename,
              const QString& private_directory) {
    require(row.value("target") == QStringLiteral("a") && row.value("operation") == QLatin1String(operation) &&
            row.value("basename") == QLatin1String(basename), "Failure identity differs");
    const auto error = row.value("qt_error_string").toString();
    require(!error.isEmpty() && !error.contains(private_directory, Qt::CaseInsensitive) &&
            !error.contains(QDir::toNativeSeparators(private_directory), Qt::CaseInsensitive),
            "Failure error is empty or exposes the private directory");
}
template<class Action> void expect_failure(Action action) {
    bool failed = false;
    try { action(); } catch (const std::runtime_error&) { failed = true; }
    require(failed, "Evidence transaction unexpectedly succeeded");
}
struct ShortFile {
    qint64 result;
    int commits = 0, writes = 0;
    bool open(QIODevice::OpenMode) { return true; }
    qint64 write(const QByteArray&) { ++writes; return result; }
    bool commit() { ++commits; return true; }
    QFileDevice::FileError error() const { return QFileDevice::WriteError; }
    QString errorString() const { return QStringLiteral("controlled short write"); }
};
struct LockedFile {
    HANDLE handle;
    explicit LockedFile(const QString& path) : handle(CreateFileW(
        reinterpret_cast<const wchar_t*>(path.utf16()), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        require(handle != INVALID_HANDLE_VALUE, "Could not retain the controlled sharing lock");
    }
    ~LockedFile() { CloseHandle(handle); }
};
}
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(), "Controlled evidence directory is unavailable");
        const auto directory = temporary.path(), path = directory + QStringLiteral("/a-events.json");
        const QJsonObject original{{"synthetic", true}};
        QJsonArray cases;
        EvidenceWriteFailure good;
        save_json(path, original, good, QStringLiteral("a"));
        require(good.snapshot().isNull(), "Success created a failure context");
        cases.append("json-success");

        EvidenceWriteFailure missing;
        expect_failure([&] { save_json(directory + QStringLiteral("/missing/a-events.json"), original,
                                       missing, QStringLiteral("a")); });
        const auto open = context(missing);
        identity(open, "json-open", "a-events.json", directory);
        require(open.value("qt_file_error").toInt() != 0 && open.value("actual_bytes").toInteger() == 0 &&
                open.value("expected_bytes").toInteger() > 0, "Open failure details differ");
        cases.append("json-open");

        for (const qint64 written : {qint64(3), qint64(-1)}) {
            ShortFile file{written}; EvidenceWriteFailure short_write;
            expect_failure([&] { detail::save_json_file(file, path, QByteArray("1234"), short_write, QStringLiteral("a")); });
            const auto row = context(short_write);
            identity(row, "json-write", "a-events.json", directory);
            require(row.value("expected_bytes").toInteger() == 4 && row.value("actual_bytes").toInteger() == written &&
                    file.writes == 1 && file.commits == 0, "Incomplete write was retried or committed");
            cases.append(written < 0 ? "json-write-error" : "json-short-write");
        }

        EvidenceWriteFailure locked;
        {
            LockedFile lock(path);
            expect_failure([&] { save_json(path, {{"replacement", true}}, locked, QStringLiteral("a")); });
        }
        const auto commit = context(locked);
        identity(commit, "json-commit", "a-events.json", directory);
        require(commit.value("qt_file_error").toInt() != 0 && commit.value("expected_bytes").toInteger() > 0 &&
                commit.value("expected_bytes") == commit.value("actual_bytes"), "Commit failure lost the complete write facts");
        QFile retained(path); require(retained.open(QIODevice::ReadOnly), "Could not inspect the retained original");
        require(QJsonDocument::fromJson(retained.readAll()).object() == original, "Failed replacement changed the original JSON");
        retained.close(); cases.append("json-commit-sharing-lock");

        QImage image(2, 2, QImage::Format_ARGB32); image.fill(Qt::black);
        EvidenceWriteFailure png_failure;
        require(!save_png(directory + QStringLiteral("/missing/a-frame-0.png"), image, png_failure, QStringLiteral("a")),
                "PNG failure unexpectedly succeeded");
        const auto png = context(png_failure);
        identity(png, "png-save", "a-frame-0.png", directory);
        require(png.value("png_writer_error").toInt() != 0, "PNG writer error was not retained");
        cases.append("png-save");

        save_json(path, original, locked, QStringLiteral("a"));
        require(!save_png(directory + QStringLiteral("/missing/later.png"), image, locked, QStringLiteral("b")),
                "Later controlled failure unexpectedly succeeded");
        require(context(locked) == commit, "A later success or failure replaced the first failure");
        for (const auto* name : {"about-to-quit.json", "complete.json"}) {
            const auto final_path = directory + QLatin1Char('/') + QLatin1String(name);
            save_json(final_path, {{"evidence_write_failure", locked.snapshot()}}, locked);
            QFile final_file(final_path); require(final_file.open(QIODevice::ReadOnly), "Could not inspect the synthetic final JSON");
            require(QJsonDocument::fromJson(final_file.readAll()).object().value("evidence_write_failure").toObject() == commit,
                    "Synthetic final JSON lost the sticky failure");
        }
        cases.append("sticky-first-failure-and-final-json");
        const auto final_path = directory + QStringLiteral("/complete.json");
        EvidenceWriteFailure final_failure;
        QJsonObject final{{"failed", false}, {"failure", ""}, {"observer_complete", true}};
        {
            LockedFile lock(final_path);
            require(!save_complete_json(final_path, final, final_failure), "A failed final write reported success");
        }
        require(final.value("failed").toBool() && !final.value("observer_complete").toBool() &&
                final.value("evidence_write_failure") == final_failure.snapshot() &&
                context(final_failure).value("basename") == QStringLiteral("complete.json") &&
                context(final_failure).value("operation") == QStringLiteral("json-commit"),
                "The structured final fallback lost its own failure");
        cases.append("complete-write-failure-structured-fallback");
        const QJsonObject report{{"status", "passed"}, {"cases", cases}, {"sharing_lock_failure", commit},
                                 {"native_gui", false}, {"ime_input", false}, {"vm", false}};
        std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Evidence diagnostic test failed: " << error.what() << '\n'; return 1;
    }
}
