using System.IO.Compression;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using static Laplace.SourceAdmission.SourceDocument;

namespace Laplace.SourceAdmission;

internal static class DurableFile
{
    [DllImport("libc", EntryPoint = "open", SetLastError = true)]
    private static extern int OpenDirectory([MarshalAs(UnmanagedType.LPUTF8Str)] string path, int flags);
    [DllImport("libc", EntryPoint = "fsync", SetLastError = true)]
    private static extern int Sync(int descriptor);
    [DllImport("libc", EntryPoint = "close", SetLastError = true)]
    private static extern int Close(int descriptor);

    internal static void SyncDirectory(string path)
    {
        if (OperatingSystem.IsWindows())
            throw new PlatformNotSupportedException("Durable source publication requires the POSIX directory provider.");
        int descriptor = OpenDirectory(path, 0);
        if (descriptor < 0) throw new IOException($"Cannot open directory for sync: {path}; errno={Marshal.GetLastPInvokeError()}.");
        try { if (Sync(descriptor) != 0) throw new IOException($"Directory sync failed: {path}; errno={Marshal.GetLastPInvokeError()}."); }
        finally { _ = Close(descriptor); }
    }
    internal static void CreateDirectory(string path)
    {
        string full = Path.GetFullPath(path);
        if (Directory.Exists(full)) return;
        string parent = Path.GetDirectoryName(full) ?? throw new IOException("Directory has no parent.");
        CreateDirectory(parent);
        Directory.CreateDirectory(full);
        SyncDirectory(parent);
    }
    internal static JsonElement WriteReceipt(string path, IDictionary<string, object?> body)
    {
        var receipt = new Dictionary<string, object?>(body, StringComparer.Ordinal);
        receipt.Remove("receipt_id");
        receipt["receipt_id"] = Convert.ToHexStringLower(Fingerprint(JsonSerializer.SerializeToElement(receipt)));
        JsonElement value = JsonSerializer.SerializeToElement(receipt);
        string full = Path.GetFullPath(path), directory = Path.GetDirectoryName(full)!;
        CreateDirectory(directory);
        string temporary = Path.Combine(directory, "." + Path.GetFileName(full) + "." + Guid.NewGuid().ToString("N"));
        try
        {
            using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                JsonSerializer.Serialize(output, value, new JsonSerializerOptions { WriteIndented = true });
                output.WriteByte((byte)'\n'); output.Flush(true);
            }
            File.Move(temporary, full, true);
            SyncDirectory(directory);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
        return value;
    }
}

internal static class SourceAcquisition
{
    private const int BlockBytes = 1024 * 1024;
    private const string ReceiptSchema = "laplace.tabular-source-acquisition-receipt/v1";

    internal static async Task<JsonElement> Acquire(SourceDocument document, string root, CancellationToken cancellation)
    {
        root = Path.GetFullPath(root);
        string profileHash = Convert.ToHexStringLower(Fingerprint(document.Root));
        if (Directory.Exists(root))
        {
            JsonElement[] verified = VerifyTree(document, root, cancellation);
            using JsonDocument existing = JsonDocument.Parse(File.ReadAllBytes(Path.Combine(root, "acquisition-receipt.json")));
            JsonElement receipt = existing.RootElement;
            var body = receipt.EnumerateObject().Where(item => item.Name != "receipt_id")
                .ToDictionary(item => item.Name, item => item.Value.Clone(), StringComparer.Ordinal);
            Require(Text(receipt, "schema") == ReceiptSchema && Text(receipt, "profile_sha256") == profileHash &&
                Canonical(receipt.GetProperty("artifacts")).AsSpan().SequenceEqual(Canonical(JsonSerializer.SerializeToElement(verified))) &&
                Text(receipt, "receipt_id") == Convert.ToHexStringLower(Fingerprint(JsonSerializer.SerializeToElement(body))),
                "Existing acquisition receipt differs from the verified source.");
            return receipt.Clone();
        }
        string parent = Path.GetDirectoryName(root)!;
        DurableFile.CreateDirectory(parent);
        string stage = Path.Combine(parent, ".laplace-source-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(stage);
        try
        {
            var byName = document.Artifacts.ToDictionary(item => Text(item, "name"), StringComparer.Ordinal);
            using var client = new HttpClient(new HttpClientHandler { AllowAutoRedirect = false });
            client.DefaultRequestHeaders.UserAgent.ParseAdd("Laplace-locked-source-acquisition/1");
            foreach (JsonElement artifact in document.Artifacts)
            {
                cancellation.ThrowIfCancellationRequested();
                string target = Path.Combine(stage, RelativePath(Text(artifact, "local_discovery_path")));
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                string? owner = OptionalText(artifact, "parent");
                if (owner is null)
                    await Download(client, artifact, target, cancellation);
                else
                {
                    string archivePath = Path.Combine(stage, RelativePath(Text(byName[owner], "local_discovery_path")));
                    using ZipArchive archive = ZipFile.OpenRead(archivePath);
                    ZipArchiveEntry entry = SelectedMember(archive, artifact);
                    using Stream input = entry.Open();
                    using var output = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None);
                    await CopyExact(input, output, artifact, cancellation);
                    output.Flush(true);
                }
            }
            JsonElement[] verified = VerifyTree(document, stage, cancellation);
            JsonElement receipt = DurableFile.WriteReceipt(Path.Combine(stage, "acquisition-receipt.json"),
                new Dictionary<string, object?> { ["schema"] = ReceiptSchema, ["profile_sha256"] = profileHash, ["artifacts"] = verified });
            foreach (string directory in Directory.EnumerateDirectories(stage, "*", SearchOption.AllDirectories).OrderByDescending(value => value.Length))
                DurableFile.SyncDirectory(directory);
            DurableFile.SyncDirectory(stage);
            Directory.Move(stage, root);
            DurableFile.SyncDirectory(parent);
            return receipt;
        }
        finally { if (Directory.Exists(stage)) Directory.Delete(stage, true); }
    }

    private static ZipArchiveEntry SelectedMember(ZipArchive archive, JsonElement artifact)
    {
        string member = Text(artifact, "archive_member");
        ZipArchiveEntry[] entries = archive.Entries.Where(entry => entry.FullName == member).ToArray();
        Require(entries.Length == 1 && checked((ulong)entries[0].Length) == Unsigned(artifact, "byte_count") &&
            entries[0].Name.Length != 0, "Archive member is absent, ambiguous, or has a different extent.");
        return entries[0];
    }
    private static JsonElement[] VerifyTree(SourceDocument document, string root, CancellationToken cancellation)
    {
        var byName = document.Artifacts.ToDictionary(item => Text(item, "name"), StringComparer.Ordinal);
        var result = new List<JsonElement>();
        byte[] left = new byte[BlockBytes], right = new byte[BlockBytes];
        foreach (JsonElement artifact in document.Artifacts)
        {
            cancellation.ThrowIfCancellationRequested();
            string path = Path.Combine(root, RelativePath(Text(artifact, "local_discovery_path")));
            using (FileStream input = File.OpenRead(path))
                Require(checked((ulong)input.Length) == Unsigned(artifact, "byte_count") &&
                    SHA256.HashData(input).AsSpan().SequenceEqual(Hex(Text(artifact, "sha256"), 32)), "Acquired artifact differs.");
            string? owner = OptionalText(artifact, "parent");
            if (owner is not null)
            {
                using ZipArchive archive = ZipFile.OpenRead(Path.Combine(root, RelativePath(Text(byName[owner], "local_discovery_path"))));
                using Stream member = SelectedMember(archive, artifact).Open();
                using FileStream selected = File.OpenRead(path);
                ulong remaining = Unsigned(artifact, "byte_count");
                while (remaining != 0)
                {
                    cancellation.ThrowIfCancellationRequested();
                    int count = checked((int)Math.Min((ulong)BlockBytes, remaining));
                    member.ReadExactly(left.AsSpan(0, count)); selected.ReadExactly(right.AsSpan(0, count));
                    Require(left.AsSpan(0, count).SequenceEqual(right.AsSpan(0, count)), "Selected member differs from its archive.");
                    remaining -= checked((ulong)count);
                }
                Require(member.ReadByte() == -1 && selected.ReadByte() == -1, "Archive member extent changed.");
            }
            result.Add(JsonSerializer.SerializeToElement(new { name = Text(artifact, "name"),
                local_discovery_path = Text(artifact, "local_discovery_path"), byte_count = Unsigned(artifact, "byte_count"),
                sha256 = Text(artifact, "sha256") }));
        }
        return result.ToArray();
    }
    private static async Task CopyExact(Stream input, Stream output, JsonElement artifact, CancellationToken cancellation)
    {
        byte[] block = new byte[BlockBytes];
        ulong remaining = Unsigned(artifact, "byte_count");
        using IncrementalHash digest = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        while (remaining != 0)
        {
            int count = await input.ReadAsync(block.AsMemory(0, checked((int)Math.Min((ulong)block.Length, remaining))), cancellation);
            Require(count != 0, "Acquired content was truncated.");
            digest.AppendData(block, 0, count);
            await output.WriteAsync(block.AsMemory(0, count), cancellation);
            remaining -= checked((ulong)count);
        }
        Require(await input.ReadAsync(block.AsMemory(0, 1), cancellation) == 0 &&
            digest.GetHashAndReset().AsSpan().SequenceEqual(Hex(Text(artifact, "sha256"), 32)),
            "Acquired content differs from its declared extent or digest.");
    }
    private static async Task Download(HttpClient client, JsonElement artifact, string target, CancellationToken cancellation)
    {
        JsonElement acquisition = artifact.GetProperty("acquisition");
        int attempts = checked((int)Unsigned(acquisition, "retry_attempts"));
        for (int attempt = 1; ; ++attempt)
        {
            try
            {
                var url = new Uri(Text(acquisition, "url"));
                for (int redirect = 0; ; ++redirect)
                {
                    Require(url.Scheme == Uri.UriSchemeHttps && redirect <= 10, "Invalid HTTPS acquisition redirect.");
                    using HttpResponseMessage response = await client.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, cancellation);
                    if (response.StatusCode is HttpStatusCode.MovedPermanently or HttpStatusCode.Redirect or
                        HttpStatusCode.SeeOther or HttpStatusCode.TemporaryRedirect or HttpStatusCode.PermanentRedirect)
                    {
                        Uri location = response.Headers.Location ?? throw new InvalidDataException("Redirect omitted its location.");
                        url = location.IsAbsoluteUri ? location : new Uri(url, location);
                        continue;
                    }
                    response.EnsureSuccessStatusCode();
                    await using Stream input = await response.Content.ReadAsStreamAsync(cancellation);
                    await using var output = new FileStream(target, FileMode.Create, FileAccess.Write, FileShare.None);
                    await CopyExact(input, output, artifact, cancellation);
                    output.Flush(true);
                    return;
                }
            }
            catch (Exception error) when (!cancellation.IsCancellationRequested && attempt < attempts &&
                error is HttpRequestException or IOException or TaskCanceledException)
            {
                if (File.Exists(target)) File.Delete(target);
                await Task.Delay(TimeSpan.FromSeconds(attempt), cancellation);
            }
        }
    }
}
