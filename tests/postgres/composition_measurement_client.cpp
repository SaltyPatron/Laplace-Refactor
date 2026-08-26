#include <libpq-fe.h>

#include <sys/utsname.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "laplace/contract/framework.h"

namespace {

constexpr std::uint64_t ExpectedRequests = 65812U;
constexpr std::uint64_t ExpectedOperands = ExpectedRequests * 2U;
constexpr std::uint64_t ExpectedEntities = ExpectedRequests + 2U;
constexpr std::uint64_t ExpectedPhysicalities = ExpectedRequests;
constexpr std::uint64_t ExpectedVertices = ExpectedOperands;
constexpr std::uint64_t ExpectedOccurrences = ExpectedRequests;
constexpr std::uint64_t ExpectedStreamRecords =
    ExpectedEntities + ExpectedPhysicalities + ExpectedVertices +
    ExpectedOccurrences;

class Result {
public:
    explicit Result(PGresult* value) : value_(value) {}
    ~Result() { PQclear(value_); }
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    Result(Result&& other) noexcept
        : value_(std::exchange(other.value_, nullptr)) {}
    Result& operator=(Result&&) = delete;
    PGresult* get() const { return value_; }

private:
    PGresult* value_;
};

Result Execute(PGconn* connection, const std::string& sql) {
    Result result(PQexec(connection, sql.c_str()));
    const auto status = PQresultStatus(result.get());
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        throw std::runtime_error(PQresultErrorMessage(result.get()));
    }
    return result;
}

std::string Scalar(PGconn* connection, const std::string& sql) {
    auto result = Execute(connection, sql);
    if (PQntuples(result.get()) != 1 || PQnfields(result.get()) != 1 ||
        PQgetisnull(result.get(), 0, 0) != 0) {
        throw std::runtime_error("scalar SQL did not return one non-null value");
    }
    return PQgetvalue(result.get(), 0, 0);
}

std::uint64_t Unsigned(std::string_view value) {
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(std::string(value), &consumed, 10);
    if (consumed != value.size()) {
        throw std::runtime_error("non-canonical unsigned scalar");
    }
    return parsed;
}

bool Hex(std::string_view value, std::size_t bytes) {
    if (value.size() != bytes * 2U) {
        return false;
    }
    for (const char character : value) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

std::uint64_t Lsn(std::string_view value) {
    const auto slash = value.find('/');
    if (slash == std::string_view::npos) {
        throw std::runtime_error("PostgreSQL returned an invalid WAL LSN");
    }
    const auto high = std::stoull(std::string(value.substr(0U, slash)), nullptr, 16);
    const auto low = std::stoull(std::string(value.substr(slash + 1U)), nullptr, 16);
    return (high << 32U) | low;
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::map<std::string, std::uint64_t> ReadKeyValues(
    const std::filesystem::path& path) {
    std::map<std::string, std::uint64_t> values;
    std::istringstream input(ReadText(path));
    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::istringstream scalar(line.substr(colon + 1U));
        std::uint64_t value = 0U;
        if (scalar >> value) {
            values.emplace(line.substr(0U, colon), value);
        }
    }
    return values;
}

struct ProcessUsage {
    std::uint64_t user_ticks{};
    std::uint64_t system_ticks{};
    std::uint64_t resident_pages{};
    std::uint64_t rss_bytes{};
    std::uint64_t high_water_bytes{};
    std::map<std::string, std::uint64_t> io;
};

ProcessUsage ReadProcessUsage(int process_id, std::uint64_t page_bytes) {
    const auto root = std::filesystem::path("/proc") /
        std::to_string(process_id);
    const auto stat = ReadText(root / "stat");
    const auto close = stat.rfind(')');
    if (close == std::string::npos || close + 2U >= stat.size()) {
        throw std::runtime_error("cannot parse PostgreSQL backend stat");
    }
    std::istringstream fields(stat.substr(close + 2U));
    std::vector<std::string> values;
    for (std::string value; fields >> value;) {
        values.push_back(value);
    }
    if (values.size() <= 21U) {
        throw std::runtime_error("PostgreSQL backend stat is incomplete");
    }
    ProcessUsage result{};
    result.user_ticks = Unsigned(values[11]);
    result.system_ticks = Unsigned(values[12]);
    result.resident_pages = Unsigned(values[21]);
    result.rss_bytes = result.resident_pages * page_bytes;
    result.io = ReadKeyValues(root / "io");

    std::istringstream status(ReadText(root / "status"));
    for (std::string line; std::getline(status, line);) {
        if (line.rfind("VmHWM:", 0U) == 0U) {
            std::istringstream scalar(line.substr(6U));
            std::uint64_t kib = 0U;
            scalar >> kib;
            result.high_water_bytes = kib * 1024U;
        }
    }
    return result;
}

std::uint64_t Delta(
    const std::map<std::string, std::uint64_t>& after,
    const std::map<std::string, std::uint64_t>& before,
    const std::string& field) {
    const auto after_value = after.count(field) == 0U ? 0U : after.at(field);
    const auto before_value = before.count(field) == 0U ? 0U : before.at(field);
    return after_value >= before_value ? after_value - before_value : 0U;
}

std::string JsonString(std::string_view value) {
    std::ostringstream output;
    output << '"';
    for (const char character : value) {
        switch (character) {
            case '\\': output << "\\\\"; break;
            case '"': output << "\\\""; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default: output << character; break;
        }
    }
    output << '"';
    return output.str();
}

std::string CpuModel() {
    std::istringstream input(ReadText("/proc/cpuinfo"));
    for (std::string line; std::getline(input, line);) {
        if (line.rfind("model name", 0U) == 0U) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                const auto first = line.find_first_not_of(" \t", colon + 1U);
                return line.substr(first);
            }
        }
    }
    return "unknown";
}

std::string SetupSql(
    const std::string& entity_a,
    const std::string& witness_a,
    const std::string& entity_b,
    const std::string& witness_b) {
    std::ostringstream sql;
    sql << "CREATE EXTENSION laplace;"
        << "CREATE FUNCTION pg_temp.composition_measurement_context() "
           "RETURNS laplace.execution_context LANGUAGE SQL IMMUTABLE "
           "PARALLEL SAFE AS $$ SELECT ROW(ARRAY["
        << "decode(repeat('01',32),'hex'),decode(repeat('02',32),'hex'),"
           "decode(repeat('03',32),'hex'),decode(repeat('04',32),'hex'),"
           "decode(repeat('05',32),'hex'),decode(repeat('06',32),'hex'),"
           "decode(repeat('07',32),'hex'),decode(repeat('08',32),'hex'),"
           "decode(repeat('09',32),'hex'),decode(repeat('0a',32),'hex')],"
           "decode(repeat('a0',32),'hex'),2147483648::bigint,4,1,1023::bigint,"
        << LAPLACE_FRAMEWORK_MAJOR << "::smallint,"
        << LAPLACE_FRAMEWORK_MINOR << "::smallint,0)::laplace.execution_context $$;"
        << "CREATE TEMP TABLE composition_measurement_fixture("
           "entity_a bytea NOT NULL,entity_a_witness bytea NOT NULL,"
           "entity_b bytea NOT NULL,entity_b_witness bytea NOT NULL);"
        << "INSERT INTO composition_measurement_fixture VALUES(decode('"
        << entity_a << "','hex'),decode('" << witness_a
        << "','hex'),decode('" << entity_b << "','hex'),decode('"
        << witness_b << "','hex'));"
        << R"SQL(
CREATE TEMP TABLE composition_measurement_request(
    result_index bigint PRIMARY KEY,
    left_kind integer NOT NULL,
    left_index bigint NOT NULL,
    right_kind integer NOT NULL,
    right_index bigint NOT NULL);
INSERT INTO composition_measurement_request
SELECT (left_index * 2 + right_index)::bigint, 1, left_index, 1, right_index
FROM generate_series(0, 1) left_index
CROSS JOIN generate_series(0, 1) right_index;
INSERT INTO composition_measurement_request
SELECT (4 + left_index * 4 + right_index)::bigint,
       2, left_index, 2, right_index
FROM generate_series(0, 3) left_index
CROSS JOIN generate_series(0, 3) right_index;
INSERT INTO composition_measurement_request
SELECT (20 + left_index * 16 + right_index)::bigint,
       2, 4 + left_index, 2, 4 + right_index
FROM generate_series(0, 15) left_index
CROSS JOIN generate_series(0, 15) right_index;
INSERT INTO composition_measurement_request
SELECT (276 + left_index * 256 + right_index)::bigint,
       2, 20 + left_index, 2, 20 + right_index
FROM generate_series(0, 255) left_index
CROSS JOIN generate_series(0, 255) right_index;
CREATE TEMP TABLE composition_measurement_input AS
SELECT
    ARRAY[
        ROW(entity_a,entity_a_witness,decode(repeat('e1',32),'hex'),
            1.0::double precision,0.0::double precision,
            0.0::double precision,0.0::double precision,
            0::bigint,0::smallint,false)::laplace.composition_known_entity_record,
        ROW(entity_b,entity_b_witness,decode(repeat('e2',32),'hex'),
            0.0::double precision,1.0::double precision,
            0.0::double precision,0.0::double precision,
            0::bigint,0::smallint,false)::laplace.composition_known_entity_record
    ] AS known,
    (SELECT array_agg(
        ROW(reference_index::numeric,1::numeric,0::bigint,reference_kind,0)
            ::laplace.composition_operand_record
        ORDER BY result_index,operand_slot)
     FROM composition_measurement_request
     CROSS JOIN LATERAL (VALUES
        (0,left_kind,left_index),(1,right_kind,right_index))
        AS operand(operand_slot,reference_kind,reference_index)) AS operands,
    (SELECT array_agg(
        ROW(result_index * 2,2::numeric,result_index + 1,1,0,
            decode(repeat('b1',32),'hex'),decode(repeat('c1',32),'hex'),
            decode(repeat('d1',32),'hex'))::laplace.composition_request_record
        ORDER BY result_index)
     FROM composition_measurement_request) AS requests
FROM composition_measurement_fixture;
CREATE FUNCTION pg_temp.composition_measurement_deposit()
RETURNS TABLE(
    working_set_receipt text,
    presence_semantic_receipt text,
    presence_execution_receipt text,
    stream_fingerprint text,
    unique_entity_count bigint,
    unique_physicality_count bigint,
    trajectory_vertex_count bigint,
    occurrence_count bigint,
    stream_record_count bigint,
    stream_byte_count bigint,
    entity_inserted bigint,
    physicality_inserted bigint,
    trajectory_vertex_inserted bigint,
    occurrence_inserted bigint,
    plan_count integer,
    entity_presence_round_count bigint,
    physicality_presence_round_count bigint,
    estimated_peak_working_bytes bigint,
    status integer)
LANGUAGE SQL VOLATILE PARALLEL UNSAFE AS $$
    SELECT
        encode((result).working_set_receipt,'hex'),
        encode((result).presence_semantic_receipt,'hex'),
        encode((result).presence_execution_receipt,'hex'),
        encode((result).stream_fingerprint,'hex'),
        (result).unique_entity_count::bigint,
        (result).unique_physicality_count::bigint,
        (result).trajectory_vertex_count::bigint,
        (result).occurrence_count::bigint,
        (result).stream_record_count::bigint,
        (result).stream_byte_count::bigint,
        (result).entity_inserted::bigint,
        (result).physicality_inserted::bigint,
        (result).trajectory_vertex_inserted::bigint,
        (result).occurrence_inserted::bigint,
        (result).plan_count,
        (result).entity_presence_round_count::bigint,
        (result).physicality_presence_round_count::bigint,
        (result).estimated_peak_working_bytes::bigint,
        (result).status
    FROM (
        SELECT laplace.composition_deposit_batch(
            pg_temp.composition_measurement_context(),
            decode(repeat('91',32),'hex'),decode(repeat('a1',32),'hex'),
            known,operands,requests,8388608::numeric) AS result
        FROM composition_measurement_input) operation
$$;
)SQL";
    return sql.str();
}

struct Sample {
    std::uint64_t wall_nanoseconds{};
    std::uint64_t user_cpu_microseconds{};
    std::uint64_t system_cpu_microseconds{};
    std::uint64_t rss_before_bytes{};
    std::uint64_t rss_after_bytes{};
    std::uint64_t backend_high_water_bytes{};
    std::uint64_t read_syscalls{};
    std::uint64_t write_syscalls{};
    std::uint64_t filesystem_read_bytes{};
    std::uint64_t filesystem_write_bytes{};
    std::uint64_t wal_bytes{};
    std::uint64_t database_calls{};
    std::uint64_t durable_outputs{};
    std::uint64_t stream_bytes{};
    std::uint64_t estimated_peak_working_bytes{};
    std::string working_set_receipt;
    std::string semantic_receipt;
    std::string execution_receipt;
    std::string stream_fingerprint;
};

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 9) {
            throw std::runtime_error(
                "usage: measurement-client SOCKET PORT RECEIPT ENTITY-A "
                "WITNESS-A ENTITY-B WITNESS-B SAMPLE-COUNT");
        }
        const std::string socket(argv[1]);
        const std::string port(argv[2]);
        const std::filesystem::path receipt_path(argv[3]);
        const std::string entity_a(argv[4]);
        const std::string witness_a(argv[5]);
        const std::string entity_b(argv[6]);
        const std::string witness_b(argv[7]);
        const auto sample_count = Unsigned(argv[8]);
        if (!Hex(entity_a, 16U) || !Hex(entity_b, 16U) ||
            !Hex(witness_a, 32U) || !Hex(witness_b, 32U) ||
            sample_count < 3U) {
            throw std::runtime_error("measurement inputs violate the exact fixture");
        }
        const char* keywords[] = {"host", "port", "dbname", nullptr};
        const char* values[] = {socket.c_str(), port.c_str(), "postgres", nullptr};
        PGconn* connection = PQconnectdbParams(keywords, values, 0);
        if (connection == nullptr || PQstatus(connection) != CONNECTION_OK) {
            const std::string error = connection == nullptr
                ? "libpq allocation failed"
                : PQerrorMessage(connection);
            if (connection != nullptr) {
                PQfinish(connection);
            }
            throw std::runtime_error(error);
        }

        Execute(connection, SetupSql(entity_a, witness_a, entity_b, witness_b));
        const auto backend_pid = PQbackendPID(connection);
        const auto page_bytes = static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
        const auto ticks_per_second =
            static_cast<std::uint64_t>(sysconf(_SC_CLK_TCK));
        const auto input_bytes = Unsigned(Scalar(
            connection,
            "SELECT pg_column_size(known)::bigint + "
            "pg_column_size(operands)::bigint + pg_column_size(requests)::bigint "
            "FROM composition_measurement_input"));
        std::vector<Sample> samples;
        samples.reserve(static_cast<std::size_t>(sample_count));
        for (std::uint64_t index = 0U; index < sample_count; ++index) {
            Execute(connection,
                "TRUNCATE laplace.canonical_deposit_receipt,"
                "laplace.observed_occurrence,"
                "laplace.composition_trajectory_vertex,"
                "laplace.physicality,laplace.canonical_entity CASCADE");
            const auto wal_before = Lsn(Scalar(
                connection, "SELECT pg_current_wal_insert_lsn()::text"));
            const auto usage_before = ReadProcessUsage(backend_pid, page_bytes);
            const auto wall_before = std::chrono::steady_clock::now();
            auto result = Execute(
                connection, "SELECT * FROM pg_temp.composition_measurement_deposit()");
            const auto wall_after = std::chrono::steady_clock::now();
            const auto usage_after = ReadProcessUsage(backend_pid, page_bytes);
            const auto wal_after = Lsn(Scalar(
                connection, "SELECT pg_current_wal_insert_lsn()::text"));
            if (PQntuples(result.get()) != 1 || PQnfields(result.get()) != 19) {
                throw std::runtime_error("measurement result shape changed");
            }
            auto field = [&](int column) {
                if (PQgetisnull(result.get(), 0, column) != 0) {
                    throw std::runtime_error("measurement returned null");
                }
                return std::string(PQgetvalue(result.get(), 0, column));
            };
            if (Unsigned(field(4)) != ExpectedEntities ||
                Unsigned(field(5)) != ExpectedPhysicalities ||
                Unsigned(field(6)) != ExpectedVertices ||
                Unsigned(field(7)) != ExpectedOccurrences ||
                Unsigned(field(8)) != ExpectedStreamRecords ||
                Unsigned(field(10)) != ExpectedEntities ||
                Unsigned(field(11)) != ExpectedPhysicalities ||
                Unsigned(field(12)) != ExpectedVertices ||
                Unsigned(field(13)) != ExpectedOccurrences ||
                Unsigned(field(14)) != 11U || Unsigned(field(15)) != 5U ||
                Unsigned(field(16)) != 1U || Unsigned(field(18)) != 0U) {
                throw std::runtime_error("whole-boundary durable counts changed");
            }
            const auto durable_outputs = Unsigned(Scalar(
                connection,
                "SELECT (SELECT count(*) FROM laplace.canonical_entity) + "
                "(SELECT count(*) FROM laplace.physicality) + "
                "(SELECT count(*) FROM laplace.composition_trajectory_vertex) + "
                "(SELECT count(*) FROM laplace.observed_occurrence) + "
                "(SELECT count(*) FROM laplace.canonical_deposit_receipt)"));
            Sample sample{};
            sample.wall_nanoseconds = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    wall_after - wall_before).count());
            sample.user_cpu_microseconds =
                (usage_after.user_ticks - usage_before.user_ticks) * 1000000U /
                ticks_per_second;
            sample.system_cpu_microseconds =
                (usage_after.system_ticks - usage_before.system_ticks) * 1000000U /
                ticks_per_second;
            sample.rss_before_bytes = usage_before.rss_bytes;
            sample.rss_after_bytes = usage_after.rss_bytes;
            sample.backend_high_water_bytes = usage_after.high_water_bytes;
            sample.read_syscalls = Delta(usage_after.io, usage_before.io, "syscr");
            sample.write_syscalls = Delta(usage_after.io, usage_before.io, "syscw");
            sample.filesystem_read_bytes =
                Delta(usage_after.io, usage_before.io, "read_bytes");
            sample.filesystem_write_bytes =
                Delta(usage_after.io, usage_before.io, "write_bytes");
            sample.wal_bytes = wal_after - wal_before;
            sample.database_calls = Unsigned(field(14)) +
                Unsigned(field(15)) + Unsigned(field(16));
            sample.durable_outputs = durable_outputs;
            sample.stream_bytes = Unsigned(field(9));
            sample.estimated_peak_working_bytes = Unsigned(field(17));
            sample.working_set_receipt = field(0);
            sample.semantic_receipt = field(1);
            sample.execution_receipt = field(2);
            sample.stream_fingerprint = field(3);
            samples.push_back(std::move(sample));
        }
        const auto postgres_version = Scalar(connection, "SELECT version()");
        PQfinish(connection);

        struct utsname machine{};
        if (uname(&machine) != 0) {
            throw std::runtime_error("uname failed");
        }
        std::filesystem::create_directories(receipt_path.parent_path());
        std::ofstream output(receipt_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            throw std::runtime_error("cannot create measurement receipt");
        }
        output << "{\n"
            << "  \"schema\":\"laplace.composition-whole-boundary-measurement/v1\",\n"
            << "  \"generator\":\"tests/postgres/composition_measurement_client.cpp\",\n"
            << "  \"source_contract\":\"contracts/composition.json\",\n"
            << "  \"proof_state\":\"isolated-integration-measurement-not-product-activation\",\n"
            << "  \"timing_boundary\":\"one-libpq-call-around-composition_deposit_batch-including-native-working-set-postgresql-presence-transactional-persistence-and-result-materialization\",\n"
            << "  \"cache_state\":\"fresh-logical-canonical-state-per-sample-with-runtime-and-filesystem-caches-not-flushed\",\n"
            << "  \"durability_mode\":\"fsync-on-synchronous-commit-isolated-postgresql\",\n"
            << "  \"memory_boundary\":\"estimated_peak_working_bytes-is-the-native-planned-working-set-peak-while-backend-high-water-bytes-is-the-process-lifetime-VmHWM-and-includes-setup-and-prior-samples\",\n"
            << "  \"command\":" << JsonString(
                "ctest -R ^postgres.composition-whole-boundary-measurement$")
            << ",\n"
            << "  \"postgresql_version\":" << JsonString(postgres_version) << ",\n"
            << "  \"machine\":{\"sysname\":" << JsonString(machine.sysname)
            << ",\"release\":" << JsonString(machine.release)
            << ",\"architecture\":" << JsonString(machine.machine)
            << ",\"cpu_model\":" << JsonString(CpuModel())
            << ",\"online_processors\":" << sysconf(_SC_NPROCESSORS_ONLN)
            << ",\"page_bytes\":" << page_bytes << "},\n"
            << "  \"input\":{\"known_entities\":2,\"requests\":"
            << ExpectedRequests << ",\"operands\":" << ExpectedOperands
            << ",\"postgresql_stored_input_datum_bytes\":" << input_bytes
            << ",\"preferred_batch_bytes\":8388608},\n"
            << "  \"expected_durable_counts\":{\"entities\":"
            << ExpectedEntities << ",\"physicalities\":"
            << ExpectedPhysicalities << ",\"trajectory_vertices\":"
            << ExpectedVertices << ",\"occurrences\":"
            << ExpectedOccurrences << ",\"stream_records\":"
            << ExpectedStreamRecords << "},\n"
            << "  \"sample_count\":" << samples.size() << ",\n"
            << "  \"samples\":[\n";
        for (std::size_t index = 0U; index < samples.size(); ++index) {
            const auto& sample = samples[index];
            output << "    {\"ordinal\":" << index
                << ",\"wall_nanoseconds\":" << sample.wall_nanoseconds
                << ",\"user_cpu_microseconds\":" << sample.user_cpu_microseconds
                << ",\"system_cpu_microseconds\":" << sample.system_cpu_microseconds
                << ",\"rss_before_bytes\":" << sample.rss_before_bytes
                << ",\"rss_after_bytes\":" << sample.rss_after_bytes
                << ",\"backend_high_water_bytes\":" << sample.backend_high_water_bytes
                << ",\"estimated_peak_working_bytes\":"
                << sample.estimated_peak_working_bytes
                << ",\"read_syscalls\":" << sample.read_syscalls
                << ",\"write_syscalls\":" << sample.write_syscalls
                << ",\"filesystem_read_bytes\":" << sample.filesystem_read_bytes
                << ",\"filesystem_write_bytes\":" << sample.filesystem_write_bytes
                << ",\"wal_bytes\":" << sample.wal_bytes
                << ",\"database_calls\":" << sample.database_calls
                << ",\"durable_outputs\":" << sample.durable_outputs
                << ",\"stream_bytes\":" << sample.stream_bytes
                << ",\"working_set_receipt\":"
                << JsonString(sample.working_set_receipt)
                << ",\"presence_semantic_receipt\":"
                << JsonString(sample.semantic_receipt)
                << ",\"presence_execution_receipt\":"
                << JsonString(sample.execution_receipt)
                << ",\"stream_fingerprint\":"
                << JsonString(sample.stream_fingerprint) << "}"
                << (index + 1U == samples.size() ? "\n" : ",\n");
        }
        output << "  ]\n}\n";
        if (!output.good()) {
            throw std::runtime_error("measurement receipt write failed");
        }
        std::cout << receipt_path << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
