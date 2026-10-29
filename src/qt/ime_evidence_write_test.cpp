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
struct SharedReader {
    HANDLE handle;
    explicit SharedReader(const QString& path) : handle(CreateFileW(
        reinterpret_cast<const wchar_t*>(path.utf16()), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        require(handle != INVALID_HANDLE_VALUE, "Could not retain the controlled R/W/Delete reader");
    }
    ~SharedReader() { CloseHandle(handle); }
};
QByteArray file_bytes(const QString& path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "Could not read the synthetic immutable packet");
    return file.readAll();
}
QString snapshot_path(const QString& directory, int generation) {
    return directory + QStringLiteral("/snapshots/state-%1.json").arg(generation, 8, 10, QLatin1Char('0'));
}
QJsonObject events(const QString& target) {
    return {{"schema", "termforge-qt-ime-native-events-v1"}, {"target", target}, {"evidence_gap", false},
            {"events", QJsonArray{QJsonObject{{"kind", "input-method"}, {"sequence", 3}, {"preedit_utf16le_hex", "604f7d59"}}}}};
}
void snapshot_tests(const QString& directory, QJsonArray& cases) {
    const auto a = events(QStringLiteral("a")), b = events(QStringLiteral("b"));
    const QJsonObject state{{"schema", "termforge-qt-ime-native-observer-v1"}, {"elapsed_ms", 50},
                            {"cursor_rectangle_logical", QJsonArray{62, 31.1171875, 1, 20}}};
    const auto normal = directory + QStringLiteral("/immutable-normal");
    EvidenceWriteFailure failure; ImmutableSnapshots producer(normal, failure);
    producer.publish(state, a, b);
    const auto first = snapshot_path(normal, 1);
    const auto first_bytes = file_bytes(first);
    {
        SharedReader held(first);
        producer.publish(state, a, b);
        require(file_bytes(first) == first_bytes, "Publishing a new name changed the held old generation");
    }
    const auto packet = QJsonDocument::fromJson(file_bytes(snapshot_path(normal, 2))).object();
    require(packet.size() == 6 && packet.value("schema") == QStringLiteral("termforge-qt-ime-snapshot-v1") &&
            packet.value("generation").toInteger() == 2 && packet.value("process_id").toInteger() == QCoreApplication::applicationPid() &&
            packet.value("state").toObject() == state &&
            packet.value("a_events_json").toString().toUtf8() == QJsonDocument(a).toJson() &&
            packet.value("b_events_json").toString().toUtf8() == QJsonDocument(b).toJson(),
            "Packet schema, sequence, PID, range or exact event JSON text differs");
    require(failure.snapshot().isNull() && producer.published_count() == 2, "Successful generations recorded a failure");
    cases.append("immutable-new-name-with-held-RWD-reader");

    EvidenceWriteFailure collision; ImmutableSnapshots conflicting(normal, collision);
    expect_failure([&] { conflicting.publish(state, a, b); });
    require(context(collision).value("operation") == QStringLiteral("snapshot-existing-target") && file_bytes(first) == first_bytes,
            "Existing generation was replaced or its refusal was lost");
    const auto first_error = collision.snapshot();
    expect_failure([&] { conflicting.publish({}, {}, {}); });
    require(collision.snapshot() == first_error && conflicting.published_count() == 0, "Stopped publisher lost its first failure");
    cases.append("immutable-existing-target-and-sticky-stop");

    const auto calibration = directory + QStringLiteral("/calibration");
    EvidenceWriteFailure calibration_failure; ImmutableSnapshots calibrate(calibration, calibration_failure);
    calibrate.publish({{"padding", ""}}, {}, {});
    const qint64 base_bytes = file_bytes(snapshot_path(calibration, 1)).size();
    const auto exact = directory + QStringLiteral("/packet-exact");
    EvidenceWriteFailure exact_failure; ImmutableSnapshots exact_packet(exact, exact_failure);
    exact_packet.publish({{"padding", QString(ImmutableSnapshots::maximum_packet_bytes - base_bytes, QLatin1Char('x'))}}, {}, {});
    require(file_bytes(snapshot_path(exact, 1)).size() == ImmutableSnapshots::maximum_packet_bytes,
            "Exact 1MiB packet was rejected or truncated");
    cases.append("immutable-exact-packet-byte-bound");
    const auto too_large = directory + QStringLiteral("/packet-over");
    EvidenceWriteFailure over_failure; ImmutableSnapshots over_packet(too_large, over_failure);
    over_packet.publish({}, {}, {});
    const auto over_first = file_bytes(snapshot_path(too_large, 1));
    expect_failure([&] { over_packet.publish({{"padding", QString(ImmutableSnapshots::maximum_packet_bytes - base_bytes + 1, QLatin1Char('x'))}}, {}, {}); });
    require(context(over_failure).value("operation") == QStringLiteral("snapshot-packet-bound") &&
            !QFileInfo::exists(snapshot_path(too_large, 2)) && file_bytes(snapshot_path(too_large, 1)) == over_first,
            "Oversize packet was published or changed existing evidence");
    cases.append("immutable-packet-byte-bound-refusal");

    const auto total = directory + QStringLiteral("/packet-total");
    EvidenceWriteFailure total_failure; ImmutableSnapshots total_packets(total, total_failure);
    for (int generation = 1; generation <= 16; ++generation) {
        const auto digits = QString::number(generation).size();
        total_packets.publish({{"padding", QString(ImmutableSnapshots::maximum_packet_bytes - base_bytes - digits + 1, QLatin1Char('x'))}}, {}, {});
        require(file_bytes(snapshot_path(total, generation)).size() == ImmutableSnapshots::maximum_packet_bytes,
                "Aggregate boundary fixture is not exactly 1MiB per packet");
    }
    const auto total_first = file_bytes(snapshot_path(total, 1)), total_last = file_bytes(snapshot_path(total, 16));
    require(total_packets.saved_bytes() == ImmutableSnapshots::maximum_saved_bytes, "Exact 16MiB aggregate was rejected");
    expect_failure([&] { total_packets.publish({}, {}, {}); });
    require(context(total_failure).value("operation") == QStringLiteral("snapshot-total-bound") &&
            !QFileInfo::exists(snapshot_path(total, 17)) && file_bytes(snapshot_path(total, 1)) == total_first &&
            file_bytes(snapshot_path(total, 16)) == total_last, "Aggregate refusal changed a published generation");
    cases.append("immutable-total-byte-bound-refusal");

    const auto count = directory + QStringLiteral("/packet-count");
    EvidenceWriteFailure count_failure; ImmutableSnapshots count_packets(count, count_failure);
    for (int generation = 1; generation <= ImmutableSnapshots::maximum_count; ++generation) count_packets.publish({}, {}, {});
    const auto count_first = file_bytes(snapshot_path(count, 1)), count_last = file_bytes(snapshot_path(count, 1024));
    expect_failure([&] { count_packets.publish({}, {}, {}); });
    require(context(count_failure).value("operation") == QStringLiteral("snapshot-count-bound") && count_packets.published_count() == 1024 &&
            QDir(count + QStringLiteral("/snapshots")).entryList({QStringLiteral("state-*.json")}, QDir::Files).size() == 1024 &&
            !QFileInfo::exists(snapshot_path(count, 1025)) && file_bytes(snapshot_path(count, 1)) == count_first &&
            file_bytes(snapshot_path(count, 1024)) == count_last, "Count refusal changed a published generation");
    cases.append("immutable-count-bound-refusal");

    const auto ending = directory + QStringLiteral("/packet-final");
    EvidenceWriteFailure final_failure; ImmutableSnapshots final_packets(ending, final_failure);
    final_packets.publish(state, a, b);
    auto final_state = state;
    require(publish_final_snapshot(final_packets, final_state, a, b, 0, 0, final_failure) &&
            final_state.value("immutable_snapshot_generation").toInteger() == 2 &&
            QJsonDocument::fromJson(file_bytes(snapshot_path(ending, 2))).object().value("state").toObject() == state,
            "Complete metadata is not bound to the actual final packet");
    cases.append("immutable-final-generation-binding");
    auto pending_state = state;
    require(!publish_final_snapshot(final_packets, pending_state, a, b, 1, 0, final_failure) &&
            pending_state.value("failed").toBool() && pending_state.value("immutable_snapshot_generation").isNull() &&
            context(final_failure).value("operation") == QStringLiteral("snapshot-final-incomplete-events") &&
            !QFileInfo::exists(snapshot_path(ending, 3)), "Pending queued events were passed off as a final packet");
    cases.append("immutable-pending-final-frame-refusal");
    auto failed_state = state;
    require(!publish_final_snapshot(count_packets, failed_state, a, b, 0, 0, count_failure) &&
            failed_state.value("failed").toBool() && failed_state.value("immutable_snapshot_generation").isNull() &&
            failed_state.value("evidence_write_failure") == count_failure.snapshot(),
            "Final publication failure reused an old generation or hid its context");
    cases.append("immutable-final-publication-failure-refusal");
}
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
        snapshot_tests(directory, cases);
        const QJsonObject report{{"status", "passed"}, {"cases", cases}, {"sharing_lock_failure", commit},
                                 {"native_gui", false}, {"ime_input", false}, {"vm", false}};
        std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Evidence diagnostic test failed: " << error.what() << '\n'; return 1;
    }
}
