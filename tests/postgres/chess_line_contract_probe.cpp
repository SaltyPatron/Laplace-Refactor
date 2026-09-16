#include "cutechess_trace_adapter.hpp"
#include "laplace/chess_line.h"
#include "laplace/composition.h"
#include "laplace/framework.h"
#include "laplace/persistence.h"
#include "blake3.h"
#include <libpq-fe.h>
#include <unistd.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QString>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace laplace::chess_provider;
constexpr qint64 MaximumFileBytes = 2 * 1024 * 1024;
constexpr int MaximumTransportBytes = 32 * 1024 * 1024;
void Require(const bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
QString Number(const std::uint64_t value) { return QString::number(static_cast<qulonglong>(value)); }
QByteArray Read(const QString& path) {
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "qualification input is unreadable");
    Require(file.size() > 0 && file.size() <= MaximumFileBytes, "qualification input size is outside bounds");
    const auto bytes = file.readAll();
    Require(bytes.size() == file.size(), "qualification input changed while reading");
    return bytes;
}
void Write(const QString& path, const QJsonObject& value) {
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
    Require(bytes.size() <= MaximumTransportBytes, "qualification receipt exceeds bound");
    QSaveFile file(path);
    Require(file.open(QIODevice::WriteOnly), "qualification receipt cannot be opened");
    Require(file.write(bytes) == bytes.size() && file.commit(), "qualification receipt was not committed");
}
QJsonValue Parse(const QByteArray& bytes) {
    Require(bytes.size() <= MaximumTransportBytes, "JSON transport exceeds bound");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    Require(error.error == QJsonParseError::NoError && !document.isNull(), "invalid qualification JSON");
    if (document.isArray()) return document.array();
    Require(document.isObject(), "qualification JSON is not structured");
    return document.object();
}
QByteArray Encode(const QJsonValue& value) {
    const auto document = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    const auto bytes = document.toJson(QJsonDocument::Compact);
    Require(bytes.size() <= MaximumTransportBytes, "native transport exceeds bound");
    return bytes;
}
std::uint64_t Unsigned(const QJsonValue& value) {
    Require(value.isString(), "unsigned transport scalar must be exact decimal text");
    const auto text = value.toString();
    Require(!text.isEmpty() && (text.size() == 1 || text[0] != QChar('0')),
        "unsigned scalar is not canonical");
    std::uint64_t result = 0U;
    for (const auto character : text) {
        const auto digit = character.unicode();
        Require(digit >= '0' && digit <= '9', "unsigned scalar contains a nondigit");
        const auto add = static_cast<std::uint64_t>(digit - '0');
        Require(result <= (std::numeric_limits<std::uint64_t>::max() - add) / 10U,
            "unsigned scalar overflows");
        result = result * 10U + add;
    }
    return result;
}
std::uint64_t JsonUnsigned(const QJsonValue& value) {
    if (value.isString()) return Unsigned(value);
    Require(value.isDouble(), "native result counter is not numeric");
    const double number = value.toDouble();
    Require(std::isfinite(number) && number >= 0.0 && number <= 9007199254740991.0 &&
        std::floor(number) == number, "native result counter is inexact");
    return static_cast<std::uint64_t>(number);
}
template <class T> T Narrow(const QJsonValue& value) {
    const auto number = Unsigned(value);
    Require(number <= std::numeric_limits<T>::max(), "native field does not fit its ABI");
    return static_cast<T>(number);
}
QByteArray Unhex(const QJsonValue& value, const qsizetype width) {
    Require(value.isString(), "hex transport scalar is not text");
    const auto text = value.toString().toLatin1();
    Require(text.size() == width * 2, "hex transport width mismatch");
    for (const auto character : text)
        Require((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'),
            "hex transport is not canonical lowercase");
    return QByteArray::fromHex(text);
}
QString Hex(const void* bytes, const std::size_t count) {
    Require(count <= static_cast<std::size_t>(MaximumTransportBytes / 2), "hex output exceeds bound");
    return QString::fromLatin1(QByteArray(static_cast<const char*>(bytes),
        static_cast<qsizetype>(count)).toHex());
}
template <class T> T Digest(const QJsonValue& value) {
    T result{};
    const auto bytes = Unhex(value, static_cast<qsizetype>(sizeof(result.bytes)));
    std::memcpy(result.bytes, bytes.constData(), sizeof(result.bytes));
    return result;
}
QString FloatHex(const double value) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    std::uint64_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    std::array<std::uint8_t, 8> network{};
    for (unsigned index = 0U; index < 8U; ++index)
        network[index] = static_cast<std::uint8_t>(bits >> (56U - index * 8U));
    return Hex(network.data(), network.size());
}
double Float(const QJsonValue& value) {
    const auto bytes = Unhex(value, 8);
    std::uint64_t bits{};
    for (const char raw : bytes) bits = (bits << 8U) | static_cast<unsigned char>(raw);
    double result{};
    std::memcpy(&result, &bits, sizeof(result));
    Require(std::isfinite(result), "nonfinite native atom coordinate");
    return result;
}
laplace_digest256 Fingerprint(const QByteArray& bytes) {
    laplace_digest256 result{};
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, bytes.constData(), static_cast<std::size_t>(bytes.size()));
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}
QString Sha256(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString FileSha256(const QString& path) {
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "selected native engine cannot be read");
    Require(file.size() > 0 && file.size() <= 512LL * 1024LL * 1024LL, "selected native engine size bound");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    Require(hash.addData(&file), "selected native engine digest failed");
    return QString::fromLatin1(hash.result().toHex());
}
struct Result {
    PGresult* value{};
    explicit Result(PGresult* result) : value(result) {}
    ~Result() { if (value != nullptr) PQclear(value); }
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    Result(Result&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
};
class Connection {
public:
    Connection(const char* socket, const char* port) {
        const char* keys[]{"host","port","dbname","connect_timeout","options",nullptr};
        const char* values[]{socket,port,"postgres","5",
            "-c statement_timeout=60000 -c lock_timeout=5000 -c synchronous_commit=on",nullptr};
        value_ = PQconnectdbParams(keys,values,0);
        if (value_ == nullptr || PQstatus(value_) != CONNECTION_OK) {
            if (value_ != nullptr) PQfinish(value_);
            value_ = nullptr;
            throw std::runtime_error("private PostgreSQL connection failed");
        }
    }
    ~Connection() { if (value_ != nullptr) PQfinish(value_); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    int Pid() const { return PQbackendPID(value_); }
    Result Query(const char* sql, const std::vector<QByteArray>& parameters = {}) {
        std::vector<const char*> pointers;
        for (const auto& parameter : parameters) pointers.push_back(parameter.constData());
        Require(pointers.size() <= 8U, "too many qualification query parameters");
        Result result(PQexecParams(value_,sql,static_cast<int>(pointers.size()),
            nullptr,pointers.data(),nullptr,nullptr,0));
        Require(result.value != nullptr, "PostgreSQL returned no result");
        const auto status = PQresultStatus(result.value);
        if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK)
            throw std::runtime_error(std::string(PQresultErrorMessage(result.value)).substr(0U,1024U));
        return result;
    }
    QJsonValue Json(const char* sql, const std::vector<QByteArray>& parameters = {}) {
        auto result = Query(sql,parameters);
        Require(PQntuples(result.value) == 1 && PQnfields(result.value) == 1 &&
            PQgetisnull(result.value,0,0) == 0, "JSON query did not return one value");
        const int bytes = PQgetlength(result.value,0,0);
        Require(bytes > 0 && bytes <= MaximumTransportBytes, "JSON database response exceeds bounds");
        return Parse(QByteArray(PQgetvalue(result.value,0,0),bytes));
    }
private:
    PGconn* value_{};
};
struct Plan {
    laplace_chess_line_plan* value{};
    ~Plan() { laplace_chess_line_plan_destroy(&value); }
};
struct WorkingSet {
    laplace_composition_working_set* value{};
    ~WorkingSet() { laplace_composition_working_set_destroy(&value); }
};
laplace_framework_context Context(const QJsonObject& object) {
    laplace_framework_context context{};
    const auto epochs = object.value("epochs").toArray();
    Require(epochs.size() == LAPLACE_FRAMEWORK_EPOCH_COUNT, "context epoch count mismatch");
    for (std::size_t index = 0U; index < LAPLACE_FRAMEWORK_EPOCH_COUNT; ++index)
        context.epochs[index] = Digest<laplace_digest256>(epochs[static_cast<qsizetype>(index)]);
    context.authority_fingerprint = Digest<laplace_digest256>(object.value("authority"));
    context.resource_grant.memory_bytes = Unsigned(object.value("memory_bytes"));
    context.resource_grant.cpu_slots = Narrow<std::uint32_t>(object.value("cpu_slots"));
    context.resource_grant.io_slots = Narrow<std::uint32_t>(object.value("io_slots"));
    context.epoch_mask = Unsigned(object.value("epoch_mask"));
    context.major = Narrow<std::uint16_t>(object.value("major"));
    context.minor = Narrow<std::uint16_t>(object.value("minor"));
    context.flags = Narrow<std::uint32_t>(object.value("flags"));
    Require(laplace_framework_context_validate(&context) == LAPLACE_FRAMEWORK_OK,
        "actual PostgreSQL context fails native validation");
    return context;
}
QString AtomParameter(const laplace_chess_line_plan_view& view) {
    Require(view.atom_count > 0U && view.atom_count <= 256U, "planner atom bound");
    QString result{"{"};
    for (std::uint64_t index = 0U; index < view.atom_count; ++index) {
        if (index != 0U) result += ',';
        result += Number(view.atom_positions[index]);
    }
    return result + '}';
}
std::vector<laplace_composition_known_entity> Atoms(
    const QJsonArray& values, const laplace_chess_line_plan_view& view) {
    Require(static_cast<std::uint64_t>(values.size()) == view.atom_count, "active atom response count");
    std::vector<laplace_composition_known_entity> result;
    for (std::uint64_t index = 0U; index < view.atom_count; ++index) {
        const auto object = values[static_cast<qsizetype>(index)].toObject();
        Require(Unsigned(object.value("ordinal")) == index + 1U &&
            Unsigned(object.value("atom")) == view.atom_positions[index], "active atom ordinal mismatch");
        laplace_composition_known_entity atom{};
        atom.entity_id = Digest<laplace_id128>(object.value("entity_id"));
        atom.identity_witness = Digest<laplace_digest256>(object.value("identity_witness"));
        atom.physicality_id = Digest<laplace_digest256>(object.value("physicality_id"));
        atom.atom = view.atom_positions[index]; atom.has_atom = 1U;
        const auto coordinates = object.value("centroid").toArray();
        Require(coordinates.size() == 4, "active atom coordinate count");
        for (std::size_t coordinate = 0U; coordinate < 4U; ++coordinate)
            atom.centroid.component[coordinate] = Float(coordinates[static_cast<qsizetype>(coordinate)]);
        laplace_id128 id{}; laplace_digest256 witness{};
        Require(laplace_identity_codepoint_witness(atom.atom,&id,&witness) == LAPLACE_IDENTITY_OK &&
            std::memcmp(id.bytes,atom.entity_id.bytes,sizeof(id.bytes)) == 0 &&
            std::memcmp(witness.bytes,atom.identity_witness.bytes,sizeof(witness.bytes)) == 0,
            "active atom identity differs from native identity owner");
        result.push_back(atom);
    }
    return result;
}
QJsonObject Transport(const laplace_chess_line_plan_view& view) {
    Require(view.request_count > 0U && view.request_count <= 100000U &&
        view.operand_count > 0U && view.operand_count <= 1000000U, "native plan exceeds transport bounds");
    QJsonArray atoms, operands, requests;
    for (std::uint64_t index = 0U; index < view.atom_count; ++index)
        atoms.append(Number(view.atom_positions[index]));
    for (std::uint64_t index = 0U; index < view.operand_count; ++index) {
        const auto& value = view.operands[index];
        operands.append(QJsonArray{Number(value.reference_index),Number(value.multiplicity),
            Number(value.relationship_metadata),Number(value.reference_kind),Number(value.flags)});
    }
    for (std::uint64_t index = 0U; index < view.request_count; ++index) {
        const auto& value = view.requests[index];
        Require(value.flags == 0U, "planner attempted occurrence publication");
        requests.append(QJsonArray{Number(value.first_operand),Number(value.operand_count),
            Number(value.source_ordinal),Number(value.recipe_version),Number(value.flags),
            Hex(value.recipe_fingerprint.bytes,32U),Hex(value.geometry_epoch.bytes,32U),
            Hex(value.occurrence_context_fingerprint.bytes,32U)});
    }
    return {{"schema","laplace.chess-plan-transport/v1"},{"atoms",atoms},
        {"operands",operands},{"requests",requests},
        {"recipe_fingerprint",Hex(view.recipe_fingerprint.bytes,32U)}};
}
QJsonObject Physicality(const laplace_persistence_physicality_record& value,
                       const laplace_trajectory_carrier* carriers, const std::size_t count) {
    Require(count == value.vertex_count, "native candidate carrier count mismatch");
    laplace_digest256 actual{};
    Require(laplace_persistence_physicality_identify(&value,&actual) == LAPLACE_PERSISTENCE_OK &&
        std::memcmp(actual.bytes,value.physicality_id.bytes,32U) == 0,
        "native physicality does not reproduce its own identity");
    Require(laplace_persistence_trajectory_fingerprint(carriers,count,&actual) == LAPLACE_PERSISTENCE_OK &&
        std::memcmp(actual.bytes,value.trajectory_fingerprint.bytes,32U) == 0,
        "native packed trajectory fingerprint mismatch");
    QJsonArray centroid;
    for (const auto coordinate : value.centroid.component) centroid.append(FloatHex(coordinate));
    return {{"physicality_id",Hex(value.physicality_id.bytes,32U)},
        {"entity_id",Hex(value.entity_id.bytes,16U)},
        {"physicality_type",Number(value.physicality_type)},{"vertex_class",Number(value.vertex_class)},
        {"recipe_version",Number(value.recipe_version)},{"structural_form",Number(value.structural_form)},
        {"dimension_count",Number(value.dimension_count)},{"flags",Number(value.flags)},
        {"recipe_fingerprint",Hex(value.recipe_fingerprint.bytes,32U)},
        {"geometry_epoch",Hex(value.geometry_epoch.bytes,32U)},
        {"trajectory_fingerprint",Hex(value.trajectory_fingerprint.bytes,32U)},
        {"centroid",centroid},{"radius",FloatHex(value.radius)},
        {"logical_count",Number(value.logical_count)},{"vertex_count",Number(value.vertex_count)},
        {"trajectory",Hex(carriers,count*sizeof(laplace_trajectory_carrier))}};
}
QJsonObject Expected(const WorkingSet& set) {
    laplace_composition_working_set_summary summary{};
    Require(laplace_composition_working_set_summary_get(set.value,&summary) == LAPLACE_COMPOSITION_OK &&
        summary.unique_entity_count <= 100000U && summary.unique_physicality_count <= 100000U &&
        summary.occurrence_count == 0U, "native candidate summary invalid");
    std::size_t count{};
    const auto* entities = laplace_composition_working_set_entity_candidates(set.value,&count);
    Require(entities != nullptr && count == summary.unique_entity_count, "native entity candidates incomplete");
    QJsonArray entity_rows, physicalities, result_entities, result_physicalities;
    for (std::size_t index = 0U; index < count; ++index)
        entity_rows.append(QJsonObject{{"entity_id",Hex(entities[index].entity.entity_id.bytes,16U)},
            {"identity_witness",Hex(entities[index].entity.identity_witness.bytes,32U)}});
    for (std::size_t index = 0U; index < summary.unique_physicality_count; ++index) {
        laplace_persistence_physicality_record value{};
        Require(laplace_composition_working_set_physicality_candidate_get(set.value,index,&value)
            == LAPLACE_COMPOSITION_OK, "native physicality candidate absent");
        const laplace_trajectory_carrier* carriers{};
        std::size_t carrier_count{};
        Require(laplace_composition_working_set_trajectory_candidate_view_get(
            set.value,index,&carriers,&carrier_count) == LAPLACE_COMPOSITION_OK,
            "native trajectory candidate absent");
        physicalities.append(Physicality(value,carriers,carrier_count));
    }
    const auto* results = laplace_composition_working_set_results(set.value,&count);
    Require(results != nullptr && count == summary.request_count, "native result mapping incomplete");
    for (std::size_t index = 0U; index < count; ++index) {
        result_entities.append(Hex(results[index].entity_id.bytes,16U));
        result_physicalities.append(Hex(results[index].physicality_id.bytes,32U));
    }
    return {{"entities",entity_rows},{"physicalities",physicalities},
        {"result_entities",result_entities},{"result_physicalities",result_physicalities},
        {"logical_occurrence_count",Number(summary.logical_occurrence_count)},
        {"trajectory_vertex_count",Number(summary.trajectory_vertex_count)}};
}
void LibraryMapping(const int pid, const QString& engine) {
    QFile file(QString("/proc/%1/maps").arg(pid));
    Require(file.open(QIODevice::ReadOnly), "actual backend library maps are unavailable");
    const auto suffix = QByteArray(" ") + engine.toUtf8();
    const auto mappings = file.readAll();
    Require(!mappings.isEmpty() && mappings.size() <= 4 * 1024 * 1024, "backend mappings are empty or unbounded");
    bool found = false;
    for (const auto& line : mappings.split('\n')) {
        Require(line.size() < 16384, "backend mapping line exceeds bound");
        if (line.trimmed().endsWith(suffix)) found = true;
    }
    Require(found, "backend did not load the selected native engine");
}
QJsonObject Observations(const TraceBatch& batch) {
    QJsonArray positions, moves, supplied;
    for (const auto& value : batch.position_observations)
        positions.append(QJsonObject{{"provider_fen",QString::fromStdString(value.provider_fen)},
            {"provider_en_passant_square",value.provider_en_passant_square},
            {"reversible_plies",value.reversible_plies},{"plies_since_setup",value.plies_since_setup},
            {"provider_prior_repetitions",value.provider_prior_repetitions}});
    for (const auto& value : batch.move_observations)
        moves.append(QJsonObject{{"supplied_spelling",QString::fromStdString(value.supplied_spelling)},
            {"canonical_san",QString::fromStdString(value.canonical_san)},
            {"provider_lan",QString::fromStdString(value.provider_lan)},
            {"provider_target_square",value.provider_target_square},
            {"lexically_canonical",value.lexically_canonical}});
    for (const auto& value : batch.supplied_setups) supplied.append(QString::fromStdString(value));
    return {{"supplied_setups",supplied},{"positions",positions},{"moves",moves}};
}
void Replayed(const QJsonObject& response) {
    const auto result = response.value("result").toObject();
    for (const auto* field : {"status","entity_inserted","physicality_inserted","trajectory_vertex_inserted",
                             "occurrence_inserted","occurrence_count",
                             "novel_entity_count","novel_physicality_count","novel_trajectory_vertex_count",
                             "batch_count","stream_record_count","stream_byte_count",
                             "effect_disposition","plan_count"})
        Require(JsonUnsigned(result.value(field)) == 0U, "replay emitted canonical state or failed");
    for (const auto* field : {"producer_receipt","staged_stream_receipt",
                             "sink_artifacts_fingerprint","plan_sequence_fingerprint"})
        Require(result.value(field).isNull(), "replay invented a publication receipt");
    Require(response.value("before") == response.value("after"), "replay changed canonical counts");
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc,argv);
    QJsonObject receipt{{"schema","laplace.chess-line-postgres-acceptance/v1"},{"status","failed"},
        {"playing_entities_admitted",0},{"recorded_game_rate_measured",false},
        {"full_pgn_parser_in_this_probe",false},{"raw_pgn_source_profile_admitted",false},
        {"source_to_trace","Authenticated attributed SAN transcription; no PGN grammar extraction in this probe."}};
    if (argc != 8) {
        std::cerr << "usage: probe SOCKET PORT MANIFEST RAW-PGN NATIVE-ENGINE RECEIPT OBSERVATIONS\n";
        return 64;
    }
    try {
        const QString manifest_path = QString::fromLocal8Bit(argv[3]);
        const QString pgn_path = QString::fromLocal8Bit(argv[4]);
        const QString engine = QFileInfo(QString::fromLocal8Bit(argv[5])).canonicalFilePath();
        Require(!engine.isEmpty(), "selected native engine is absent");
        LibraryMapping(static_cast<int>(getpid()),engine);
        const auto engine_digest = FileSha256(engine);
        receipt.insert("native_engine_sha256",engine_digest);
        const auto manifest_bytes = Read(manifest_path);
        const auto raw_pgn = Read(pgn_path);
        const auto manifest = Parse(manifest_bytes).toObject();
        QJsonObject selected;
        for (const auto& value : manifest.value("cases").toArray()) {
            const auto candidate = value.toObject();
            if (candidate.value("id").toString() == "upstream-117-ply") {
                Require(selected.isEmpty(), "duplicate attributed corpus case");
                selected = candidate;
            }
        }
        Require(!selected.isEmpty(), "attributed 117-ply corpus case is missing");
        const auto provenance = selected.value("source_attribution").toObject();
        const auto git_preimage = QByteArray("blob ") + QByteArray::number(static_cast<qlonglong>(raw_pgn.size())) + '\0' + raw_pgn;
        const auto git_blob = QString::fromLatin1(
            QCryptographicHash::hash(git_preimage,QCryptographicHash::Sha1).toHex());
        Require(git_blob == provenance.value("blob").toString() &&
            git_blob == "34d825f472c2758fffdcc9e26b25cdb242887555",
            "raw PGN does not match the retained attributed source");
        const auto request = selected.value("request").toObject();
        Require(request.value("notation_policy").toString() == "provider-legal", "corpus notation policy drift");
        const auto traces = request.value("traces").toArray();
        Require(traces.size() == 1, "corpus source occurrence count drift");
        const auto san = traces[0].toObject().value("san").toArray();
        Require(san.size() == 117, "full corpus trace was truncated");
        std::vector<std::string> owned;
        for (const auto& value : san) {
            Require(value.isString() && value.toString().size() <= 32, "invalid corpus spelling");
            owned.push_back(value.toString().toStdString());
        }
        std::vector<std::string_view> spellings;
        for (const auto& value : owned) spellings.emplace_back(value);
        const TraceInput trace{{},spellings.data(),spellings.size(),NotationPolicy::ProviderLegal};
        TraceBatch batch;
        Require(BuildTraceBatch(&trace,1U,{4U,4096U,1048576U},batch).status == TraceStatus::Ok &&
            batch.moves.size() == 117U && batch.positions.size() == 118U &&
            batch.lines.size() == 1U, "qualified native Board rejected the complete corpus trace");
        const auto source = Fingerprint(raw_pgn);
        const auto manifest_fingerprint = Fingerprint(manifest_bytes);
        receipt.insert("source_fingerprint",Hex(source.bytes,32U));
        receipt.insert("manifest_fingerprint",Hex(manifest_fingerprint.bytes,32U));
        receipt.insert("source_occurrences_retained",1);
        receipt.insert("plies",117);
        QJsonObject observations{{"schema","laplace.chess-line-source-observations/v1"},
            {"source_occurrences",1},{"source_attribution",provenance},
            {"selected_case",selected},{"raw_pgn_base64",QString::fromLatin1(raw_pgn.toBase64())},
            {"raw_pgn_git_blob",git_blob},{"raw_pgn_blake3",Hex(source.bytes,32U)},
            {"manifest_blake3",Hex(manifest_fingerprint.bytes,32U)},
            {"provider_observations",Observations(batch)},
            {"scope","One attributed source occurrence; subsequent processing is replay, not new PLAYING evidence."}};
        Write(QString::fromLocal8Bit(argv[7]),observations);
        receipt.insert("observations_sha256",Sha256(QJsonDocument(observations).toJson(QJsonDocument::Indented)));

        auto connection = std::make_unique<Connection>(argv[1],argv[2]);
        const int first_pid = connection->Pid();
        const auto context_json = connection->Json("SELECT chess_line_contract.context_json()").toObject();
        auto context = Context(context_json);
        receipt.insert("actual_context",context_json);
        laplace_chess_line_plan_input input{};
        input.positions=batch.positions.data(); input.position_count=batch.positions.size();
        input.moves=batch.moves.data(); input.move_count=batch.moves.size();
        input.lines=batch.lines.data(); input.line_count=batch.lines.size();
        input.geometry_epoch=context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY];
        input.maximum_positions=1024U; input.maximum_moves=1024U; input.maximum_lines=4U;
        input.maximum_requests=100000U; input.maximum_operands=1000000U;
        input.version=LAPLACE_CHESS_LINE_VERSION;
        Plan plan;
        Require(laplace_chess_line_plan_create(&input,&plan.value) == LAPLACE_CHESS_LINE_OK,
            "qualified trace was rejected by the native LINE planner");
        laplace_chess_line_plan_view view{};
        Require(laplace_chess_line_plan_view_get(plan.value,&view) == LAPLACE_CHESS_LINE_OK,
            "native LINE plan view unavailable");
        const auto atom_parameter = AtomParameter(view).toUtf8();
        const auto atom_json = connection->Json("SELECT chess_line_contract.atom_json($1::integer[])",
            {atom_parameter}).toArray();
        auto atoms = Atoms(atom_json,view);
        LibraryMapping(first_pid,engine);
        const laplace_composition_working_set_input composition{&context,&source,&view.recipe_fingerprint,
            atoms.data(),atoms.size(),view.operands,view.operand_count,
            view.requests,view.request_count,65536U,0U};
        WorkingSet expected_set;
        Require(laplace_composition_working_set_create(&composition,&expected_set.value) == LAPLACE_COMPOSITION_OK,
            "native composition with actual admitted atoms failed");
        const auto transport = Transport(view);
        const auto expected = Expected(expected_set);
        const auto root_index = view.line_result_indexes[0];
        const auto root_id = expected.value("result_entities").toArray()[static_cast<qsizetype>(root_index)].toString();
        const auto root_physicality =
            expected.value("result_physicalities").toArray()[static_cast<qsizetype>(root_index)].toString();
        receipt.insert("root_entity_id",root_id); receipt.insert("root_physicality_id",root_physicality);
        receipt.insert("native_request_count",Number(view.request_count));
        receipt.insert("native_operand_count",Number(view.operand_count));
        receipt.insert("active_atom_count",Number(view.atom_count));
        receipt.insert("structural_logical_children",expected.value("logical_occurrence_count"));
        const QByteArray transport_bytes=Encode(transport), expected_bytes=Encode(expected);
        const QByteArray source_hex=Hex(source.bytes,32U).toLatin1();
        const std::vector<QByteArray> arguments{transport_bytes,source_hex,expected_bytes};
        const char* deposit_sql =
            "SELECT chess_line_contract.deposit($1::jsonb,decode($2,'hex'),$3::jsonb)";
        const auto baseline = connection->Json("SELECT chess_line_contract.counts()");
        connection->Query("BEGIN");
        const auto rolled_back = connection->Json(deposit_sql,arguments).toObject();
        Require(JsonUnsigned(rolled_back.value("result").toObject().value("physicality_inserted")) > 0U,
            "fresh fixture did not exercise novel physicality publication");
        connection->Query("ROLLBACK");
        Require(connection->Json("SELECT chess_line_contract.counts()") == baseline,
            "caller rollback retained canonical publication");
        receipt.insert("caller_rollback_verified",true);

        const auto first = connection->Json(deposit_sql,arguments).toObject();
        Require(JsonUnsigned(first.value("result").toObject().value("entity_inserted")) > 0U &&
            JsonUnsigned(first.value("result").toObject().value("physicality_inserted")) > 0U,
            "fresh committed trace did not add actual canonical content");
        const auto warm = connection->Json(deposit_sql,arguments).toObject();
        Replayed(warm);
        receipt.insert("first_deposition",first);
        receipt.insert("warm_replay",warm);
        const auto controls = connection->Json(
            "SELECT jsonb_build_object('controls',chess_line_contract.corruption_controls("
            "$1::jsonb,decode($2,'hex'))::text)",{expected_bytes,root_physicality.toLatin1()}).toObject();
        Require(Unsigned(controls.value("controls")) == 3U, "full-body corruption controls incomplete");
        receipt.insert("corruption_controls",3);
        const auto after_warm = connection->Json("SELECT chess_line_contract.counts()");
        connection.reset();
        connection = std::make_unique<Connection>(argv[1],argv[2]);
        const int cold_pid = connection->Pid();
        Require(cold_pid != first_pid, "replay reused the original PostgreSQL backend");
        Require(connection->Json("SELECT chess_line_contract.context_json()") == QJsonValue(context_json),
            "active context changed across reconnect");
        Require(connection->Json("SELECT chess_line_contract.atom_json($1::integer[])",
            {atom_parameter}) == QJsonValue(atom_json), "active atom tuples changed across reconnect");
        LibraryMapping(cold_pid,engine);
        const auto cold_readback = connection->Json("SELECT chess_line_contract.verify($1::jsonb)",{expected_bytes});
        const auto cold = connection->Json(deposit_sql,arguments).toObject();
        Replayed(cold);
        Require(cold.value("result") == warm.value("result"), "fresh-backend replay changed the exact native result");
        Require(connection->Json("SELECT chess_line_contract.counts()") == after_warm,
            "fresh-backend read/replay changed canonical counts");
        receipt.insert("cold_readback",cold_readback);
        receipt.insert("cold_replay",cold);
        receipt.insert("backend_before",first_pid); receipt.insert("backend_after",cold_pid);
        receipt.insert("fresh_backend_verified",true);
        receipt.insert("exact_warm_cold_result",true);
        receipt.insert("selected_native_engine",engine);
        receipt.insert("backend_engine_mappings_verified",true);
        receipt.insert("fsync",true); receipt.insert("synchronous_commit",true);
        receipt.insert("full_page_writes",true);
        receipt.insert("native_trace_execution",true);
        receipt.insert("active_unicode_composition_execution",true);
        receipt.insert("postgresql_line_admission",true);
        Require(FileSha256(engine) == engine_digest, "selected native engine changed during qualification");
        Require(Read(manifest_path) == manifest_bytes && Read(pgn_path) == raw_pgn,
            "source artifacts changed during qualification");
        receipt.insert("status","passed");
    } catch (const std::exception& error) {
        receipt.insert("error",QString::fromUtf8(error.what()).left(2048));
    }
    try { Write(QString::fromLocal8Bit(argv[6]),receipt); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    const auto encoded=QJsonDocument(receipt).toJson(QJsonDocument::Compact);
    std::cout << "LAPLACE_QA_RECEIPT chess_line_postgres " << encoded.constData() << '\n';
    return receipt.value("status").toString() == "passed" ? 0 : 1;
}
