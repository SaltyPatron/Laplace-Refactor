using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using static Laplace.SourceAdmission.SourceDocument;

namespace Laplace.SourceAdmission;

// Transport for the existing native sibling-sink operation. The native engine
// constructs Unicode identities, coordinates, deposition plans and cache files.
internal static class UnicodeActivation
{
    private static JsonElement Read(string path)
    {
        using JsonDocument document = JsonDocument.Parse(File.ReadAllBytes(path));
        return document.RootElement.Clone();
    }

    private static async Task<JsonElement> Identify(string package, string requestPath)
    {
        var start = new ProcessStartInfo(Path.Combine(package, "bin", "laplace_unicode_activation_identify")) {
            UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = Utf8, StandardErrorEncoding = Utf8 };
        start.ArgumentList.Add("--request");
        start.ArgumentList.Add(Path.GetFullPath(requestPath));
        using Process process = Process.Start(start) ?? throw new IOException("Cannot start native activation identity provider.");
        Task<string> stdout = process.StandardOutput.ReadToEndAsync();
        Task<string> stderr = process.StandardError.ReadToEndAsync();
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        try { await process.WaitForExitAsync(deadline.Token); }
        catch
        {
            if (!process.HasExited) process.Kill(true);
            await process.WaitForExitAsync(CancellationToken.None).WaitAsync(TimeSpan.FromSeconds(5));
            throw;
        }
        string output = await stdout, error = await stderr;
        Require(process.ExitCode == 0, "Native activation identity provider failed: " + error);
        using JsonDocument identities = JsonDocument.Parse(output);
        return identities.RootElement.Clone();
    }

    internal static async Task<int> Run(string requestPath, string receiptPath)
    {
        JsonElement request = Read(requestPath);
        string contracts = Path.Combine(AppContext.BaseDirectory, "contracts");
        JsonElement cluster = Read(Path.Combine(contracts, "postgresql-cluster.json"));
        JsonElement framework = Read(Path.Combine(contracts, "framework.json"));
        JsonElement contract = Read(Path.Combine(contracts, "unicode-product-activation.json"));
        JsonElement operation = contract.GetProperty("operation");
        Require(operation.GetProperty("initial_activation_only").GetBoolean(), "Unsupported activation operation.");
        JsonElement context = request.GetProperty("context");
        string contextSql = PostgreSqlAdmission.Context(context, framework, out _);
        _ = Hex(Text(request, "activation_epoch_id"), 16);
        _ = Hex(Text(request, "activation_epoch_fingerprint"), 32);
        string package = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", ".."));
        var selected = new DirectoryInfo(Text(cluster.GetProperty("package"), "active_link"));
        Require(package == (selected.ResolveLinkTarget(true)?.FullName ?? selected.FullName),
            "Activation transport and selected native engine must belong to the same installed package.");
        string identityRequest = Path.GetFullPath(Text(request, "native_identity_request"));
        Require(identityRequest != Path.GetFullPath(receiptPath), "Receipt cannot overwrite the native identity request.");
        JsonElement identities = await Identify(package, identityRequest);
        JsonElement provider = contract.GetProperty("identity_provider");
        Require(Text(identities, "schema") == Text(provider, "output_schema"), "Native activation identity schema differs.");
        foreach (JsonProperty field in provider.GetProperty("fields").EnumerateObject())
        {
            byte[] value = Hex(Text(identities, field.Name), checked((int)Unsigned(field.Value, "bytes")));
            Require(value.Any(item => item != 0), "Native activation identity cannot be zero.");
        }
        foreach (string field in new[] { "activation_epoch_id", "activation_epoch_fingerprint" })
            Require(Text(request, field) == Text(identities, field), "Activation identity differs from the native request.");
        Require(Text(context, "authority_fingerprint") == Text(identities, "authority_fingerprint"),
            "Activation authority differs from the native request.");
        foreach (JsonProperty slot in framework.GetProperty("epoch_slots").EnumerateObject())
            Require(Text(context.GetProperty("epochs"), slot.Name) == Text(identities, slot.Name + "_epoch"),
                "Activation context epoch differs from the native request: " + slot.Name);
        Require(Path.GetFullPath(requestPath) != Path.GetFullPath(receiptPath), "Receipt cannot overwrite the activation request.");
        uint seconds = checked((uint)Unsigned(operation, "statement_timeout_seconds"));
        Require(seconds is > 0 and <= 2147483, "Invalid activation timeout.");
        string Number(ulong value) => value.ToString(CultureInfo.InvariantCulture);
        foreach (string field in new[] { "source_root", "spool_directory", "tier0_path", "reverse_path" })
        {
            string path = Text(request, field);
            Require(Path.IsPathFullyQualified(path) && !path.Contains('\0'), "Activation paths must be absolute.");
            string output = Path.GetFullPath(receiptPath), input = Path.GetFullPath(path);
            Require(output != input && !output.StartsWith(input.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.Ordinal),
                "Activation receipt must be separate from source and native output paths.");
        }
        string sqlPath = Path.Combine(AppContext.BaseDirectory, "sql", "unicode-activate.sql");
        byte[] sqlBytes = await File.ReadAllBytesAsync(sqlPath);
        var receipt = new Dictionary<string, object?> {
            ["schema"] = "laplace.unicode-native-command-receipt/v1", ["phase"] = "prepared",
            ["package_root"] = package, ["request_sha256"] = Convert.ToHexStringLower(Fingerprint(request)),
            ["native_identities"] = identities,
            ["sql_sha256"] = Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(sqlBytes)),
            ["started_at"] = DateTimeOffset.UtcNow.ToString("O") };
        DurableFile.WriteReceipt(receiptPath, receipt);
        JsonElement instance = cluster.GetProperty("instance");
        var start = new ProcessStartInfo(Path.Combine(package, "pgsql-" + Number(Unsigned(cluster.GetProperty("package"), "postgresql_major")), "bin", "psql")) {
            UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = Utf8, StandardErrorEncoding = Utf8 };
        foreach (string argument in new[] { "-X", "-w", "-q", "-A", "-t", "-v", "ON_ERROR_STOP=1",
            "-h", Text(instance, "socket_directory"), "-p", Number(Unsigned(instance, "port")),
            "-U", Text(instance, "admin_role"), "-d", Text(instance, "database") }) start.ArgumentList.Add(argument);
        foreach (var parameter in new Dictionary<string, string> {
            ["activation_request"] = request.GetRawText(), ["activation_contract"] = contract.GetRawText(),
            ["execution_context"] = contextSql, ["timeout_ms"] = Number(seconds * 1000UL) })
        {
            start.ArgumentList.Add("--set");
            start.ArgumentList.Add(parameter.Key + "=" + parameter.Value);
        }
        start.ArgumentList.Add("--file");
        start.ArgumentList.Add(sqlPath);
        start.Environment["PGCONNECT_TIMEOUT"] = "15";
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(seconds + 30));
        try
        {
            receipt["phase"] = "executing";
            DurableFile.WriteReceipt(receiptPath, receipt);
            using Process process = Process.Start(start) ?? throw new IOException("Cannot start native PostgreSQL activation transport.");
            Task<string> stdout = process.StandardOutput.ReadToEndAsync();
            Task<string> stderr = process.StandardError.ReadToEndAsync();
            try
            {
                await process.WaitForExitAsync(deadline.Token);
            }
            catch
            {
                if (!process.HasExited) process.Kill(true);
                await process.WaitForExitAsync(CancellationToken.None).WaitAsync(TimeSpan.FromSeconds(5));
                receipt["stdout"] = await stdout; receipt["stderr"] = await stderr;
                throw;
            }
            receipt["stdout"] = await stdout; receipt["stderr"] = await stderr;
            Require(process.ExitCode == 0, "Native Unicode activation failed; the command receipt contains the database error.");
            using JsonDocument result = JsonDocument.Parse((string)receipt["stdout"]!);
            Require(result.RootElement.ValueKind == JsonValueKind.Object, "Committed Unicode root readback is absent.");
            receipt["native_result"] = result.RootElement.Clone();
            // This records the native transaction only. Product restart and cold
            // inversion remain distinct requirements of the activation receipt.
            receipt["phase"] = "native_committed_and_read_back";
            receipt["completed_at"] = DateTimeOffset.UtcNow.ToString("O");
            Console.WriteLine(DurableFile.WriteReceipt(receiptPath, receipt).GetRawText());
            return 0;
        }
        catch (Exception error)
        {
            receipt["phase"] = "outcome_unknown"; receipt["error"] = error.Message;
            receipt["completed_at"] = DateTimeOffset.UtcNow.ToString("O");
            DurableFile.WriteReceipt(receiptPath, receipt);
            throw;
        }
    }
}
