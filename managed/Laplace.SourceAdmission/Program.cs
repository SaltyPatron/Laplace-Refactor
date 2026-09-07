using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;
using static Laplace.SourceAdmission.SourceDocument;

namespace Laplace.SourceAdmission;

internal static class Program
{
    private static JsonElement ReadJson(string path)
    { using JsonDocument value = JsonDocument.Parse(File.ReadAllBytes(path)); return value.RootElement.Clone(); }

    private static async Task<int> Main(string[] arguments)
    {
        if (arguments.Length == 1 && arguments[0] == "--help")
        {
            Console.WriteLine("laplace-source-admit --profile FILE --source-root DIRECTORY --context FILE --maximum-input-bytes N --timeout-seconds N --receipt FILE [--transport copy|server-files] [--server-source-root DIRECTORY] [--host HOST] [--port PORT] [--user ROLE] [--database DATABASE] [--contracts-root DIRECTORY]");
            Console.WriteLine("laplace-source-admit activate-unicode --request FILE --receipt FILE");
            return 0;
        }
        try
        {
            if (arguments.Length == 5 && arguments[0] == "activate-unicode" &&
                arguments[1] == "--request" && arguments[3] == "--receipt")
                return await UnicodeActivation.Run(arguments[2], arguments[4]);
            var options = new Dictionary<string, string>(StringComparer.Ordinal);
            string[] names = ["profile", "source-root", "context", "maximum-input-bytes", "timeout-seconds", "receipt",
                "transport", "server-source-root", "host", "port", "user", "database", "contracts-root"];
            for (int index = 0; index < arguments.Length; index += 2)
            {
                Require(index + 1 < arguments.Length && arguments[index].StartsWith("--", StringComparison.Ordinal), "Expected named argument and value.");
                string name = arguments[index][2..];
                Require(names.Contains(name) && options.TryAdd(name, arguments[index + 1]), "Unknown or repeated argument.");
            }
            string Required(string name) => options.TryGetValue(name, out string? value) ? value :
                throw new ArgumentException($"--{name} is required.");
            string contracts = options.GetValueOrDefault("contracts-root", Path.Combine(AppContext.BaseDirectory, "contracts"));
            JsonElement cluster = ReadJson(Path.Combine(contracts, "postgresql-cluster.json"));
            JsonElement framework = ReadJson(Path.Combine(contracts, "framework.json"));
            JsonElement profileExecution = ReadJson(Path.Combine(contracts, "source-profile-execution.json"));
            JsonElement context = ReadJson(Required("context"));
            _ = PostgreSqlAdmission.Context(context, framework, out _);
            var document = new SourceDocument(Required("profile"));
            ulong maximumInput = ulong.Parse(Required("maximum-input-bytes"), CultureInfo.InvariantCulture);
            uint timeoutSeconds = uint.Parse(Required("timeout-seconds"), CultureInfo.InvariantCulture);
            Require(timeoutSeconds is > 0 and <= 2147483, "Invalid finite command timeout.");
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(timeoutSeconds));
            Console.CancelKeyPress += (_, eventArgs) => { eventArgs.Cancel = true; deadline.Cancel(); };
            CancellationToken cancellation = deadline.Token;
            Require(document.ByteCount <= maximumInput, "Source exceeds the declared input byte bound.");
            string transport = options.GetValueOrDefault("transport", "copy");
            Require(transport is "copy" or "server-files", "Unknown source transport.");
            ulong metadata = checked((ulong)Canonical(document.Root).Length * 4 + (ulong)document.Artifacts.Length * 4096);
            ulong grant = Unsigned(context, "memory_bytes");
            Require(checked(document.ByteCount + metadata) <= grant, "Source input exceeds the supplied memory grant.");
            Require(checked(metadata + (transport == "copy" ? document.ByteCount : 0)) < (1UL << 30) - 4,
                "Source exceeds the PostgreSQL array transport limit; server-files carries file descriptors.");
            string root = Path.GetFullPath(Required("source-root"));
            string receiptPath = Path.GetFullPath(Required("receipt"));
            Require(receiptPath != root && !receiptPath.StartsWith(root.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.Ordinal) &&
                receiptPath != Path.GetFullPath(Required("profile")) && receiptPath != Path.GetFullPath(Required("context")),
                "Admission receipt must be separate from source and execution inputs.");
            string? serverRoot = options.GetValueOrDefault("server-source-root");
            if (transport == "server-files")
            {
                if (serverRoot is null)
                {
                    Require(!options.TryGetValue("host", out string? host) || host.StartsWith('/'),
                        "Remote server-files transport requires --server-source-root.");
                    serverRoot = root;
                }
                Require(Path.IsPathFullyQualified(serverRoot), "Server source root must be absolute.");
            }
            else Require(serverRoot is null, "Server source root applies only to server-files transport.");
            var selected = new DirectoryInfo(Text(cluster.GetProperty("package"), "active_link"));
            string package = selected.ResolveLinkTarget(true)?.FullName ?? selected.FullName;
            string commandPackage = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", ".."));
            Require(package == commandPackage,
                "Source transport and selected native engine must belong to the same installed package.");
            JsonElement acquisition = await SourceAcquisition.Acquire(document, root, cancellation);
            byte[] graph;
            using (var input = new NativeSourceInputs(document, root, grant))
                graph = input.Identify(Path.Combine(package, "lib", "liblaplace_engine.so"));
            cancellation.ThrowIfCancellationRequested();
            var receipt = new Dictionary<string, object?> {
                ["schema"] = "laplace.tabular-source-admission-command-receipt/v1",
                ["started_at"] = DateTimeOffset.UtcNow.ToString("O"), ["phase"] = "prepared",
                ["profile_sha256"] = Convert.ToHexStringLower(Fingerprint(document.Root)),
                ["acquisition_receipt_id"] = Text(acquisition, "receipt_id"),
                ["source_graph_fingerprint"] = Convert.ToHexStringLower(graph), ["source_root"] = root,
                ["package_root"] = package, ["execution_context_sha256"] = Convert.ToHexStringLower(Fingerprint(context)),
                ["input_bytes"] = document.ByteCount, ["maximum_input_bytes"] = maximumInput,
                ["timeout_seconds"] = timeoutSeconds, ["transport"] = transport, ["server_source_root"] = serverRoot };
            string temporary = Path.Combine(Path.GetTempPath(), "laplace-source-admission-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(temporary);
            try
            {
                string? copyPath = transport == "copy" ? Path.Combine(temporary, "source.copy") : null;
                if (copyPath is not null) PostgreSqlAdmission.WriteCopy(document, root, copyPath, cancellation);
                string sql = PostgreSqlAdmission.Render(document, context, framework, profileExecution,
                    graph, copyPath, serverRoot, checked(timeoutSeconds * 1000));
                string sqlPath = Path.Combine(temporary, "admission.sql");
                await File.WriteAllTextAsync(sqlPath, sql, Utf8, cancellation);
                receipt["sql_sha256"] = Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(Utf8.GetBytes(sql)));
                DurableFile.WriteReceipt(receiptPath, receipt);
                JsonElement instance = cluster.GetProperty("instance");
                var start = new ProcessStartInfo(Path.Combine(package, "pgsql-" +
                    Number(Unsigned(cluster.GetProperty("package"), "postgresql_major")), "bin", "psql")) {
                    UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true,
                    StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8 };
                foreach (string value in new[] { "-X", "-w", "-q", "-A", "-t", "-v", "ON_ERROR_STOP=1",
                    "-h", options.GetValueOrDefault("host", Text(instance, "socket_directory")),
                    "-p", options.GetValueOrDefault("port", Number(Unsigned(instance, "port"))),
                    "-U", options.GetValueOrDefault("user", Text(instance, "admin_role")),
                    "-d", options.GetValueOrDefault("database", Text(instance, "database")), "--file", sqlPath })
                    start.ArgumentList.Add(value);
                start.Environment["PGCONNECT_TIMEOUT"] = "15";
                receipt["phase"] = "executing";
                DurableFile.WriteReceipt(receiptPath, receipt);
                try
                {
                    using Process process = Process.Start(start) ?? throw new IOException("Could not start PostgreSQL transport.");
                    Task<string> stdout = process.StandardOutput.ReadToEndAsync();
                    Task<string> stderr = process.StandardError.ReadToEndAsync();
                    try { await process.WaitForExitAsync(cancellation); }
                    catch
                    {
                        if (!process.HasExited) process.Kill(true);
                        await process.WaitForExitAsync(CancellationToken.None).WaitAsync(TimeSpan.FromSeconds(5));
                        throw;
                    }
                    receipt["stdout"] = await stdout; receipt["stderr"] = await stderr; receipt["returncode"] = process.ExitCode;
                    Require(process.ExitCode == 0, $"PostgreSQL admission failed; details: {receiptPath}");
                    using JsonDocument output = JsonDocument.Parse((string)receipt["stdout"]!);
                    JsonElement readback = output.RootElement;
                    foreach (string field in new[] { "declared_profile_persisted", "root_entity_persisted", "root_physicality_persisted", "world_admission_persisted" })
                        Require(readback.TryGetProperty(field, out JsonElement value) && value.ValueKind == JsonValueKind.True,
                            "Committed source admission readback did not close.");
                    receipt["readback"] = readback.Clone(); receipt["phase"] = "admitted_and_read_back";
                    receipt["completed_at"] = DateTimeOffset.UtcNow.ToString("O");
                    Console.WriteLine(DurableFile.WriteReceipt(receiptPath, receipt).GetRawText());
                }
                catch (Exception error)
                {
                    receipt["phase"] = "outcome_unknown"; receipt["error"] = error.Message;
                    receipt["completed_at"] = DateTimeOffset.UtcNow.ToString("O");
                    DurableFile.WriteReceipt(receiptPath, receipt);
                    throw;
                }
            }
            finally { Directory.Delete(temporary, true); }
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine("laplace-source-admit: " + error.Message);
            return 1;
        }
    }
    private static string Number(ulong value) => value.ToString(CultureInfo.InvariantCulture);
}
