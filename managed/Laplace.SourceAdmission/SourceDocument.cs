using System.Globalization;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Laplace.SourceAdmission;

internal sealed class SourceDocument
{
    internal JsonElement Root { get; }
    internal JsonElement[] Artifacts { get; }
    internal ulong ByteCount { get; }
    internal static readonly UTF8Encoding Utf8 = new(false, true);
    internal static readonly string[] FingerprintSections = [
        "authority_and_release", "license", "syntax_authority", "recipe_program",
        "universal_ast_mapping", "highway_and_references", "epistemic_witnessing",
        "denominators", "conformance", "completion"];

    internal SourceDocument(string path)
    {
        using JsonDocument document = JsonDocument.Parse(File.ReadAllBytes(path));
        Root = document.RootElement.Clone();
        Require(Text(Root, "schema") == "laplace.tabular-source-profile/v1", "Unknown source profile schema.");
        Artifacts = Root.GetProperty("artifacts").EnumerateArray().ToArray();
        Require(Artifacts.Length != 0, "Source profile has no artifacts.");
        var names = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
        var paths = new HashSet<string>(StringComparer.Ordinal);
        string? previousName = null;
        ulong bytes = 0, records = 0, fields = 0, claims = 0, mappings = 0;
        ulong containers = 0, members = 0;
        foreach (JsonElement artifact in Artifacts)
        {
            string name = Text(artifact, "name");
            Require(previousName is null || CompareUtf8(previousName, name) < 0, "Artifact names must be canonical and unique.");
            previousName = name;
            string relative = RelativePath(Text(artifact, "local_discovery_path"));
            Require(relative.Split(Path.DirectorySeparatorChar)[0] != "acquisition-receipt.json", "Artifact overlaps receipt.");
            Require(paths.All(prior => prior != relative &&
                !relative.StartsWith(prior + Path.DirectorySeparatorChar, StringComparison.Ordinal) &&
                !prior.StartsWith(relative + Path.DirectorySeparatorChar, StringComparison.Ordinal)), "Artifact paths overlap.");
            paths.Add(relative);
            _ = Hex(Text(artifact, "sha256"), 32);
            ulong size = Unsigned(artifact, "byte_count");
            Require(size != 0, "Empty source artifact.");
            bytes = checked(bytes + size);
            Require(Text(artifact, "media_type").Length != 0, "Artifact media type is required.");
            bool container = Roles(artifact).Contains("container"), member = Roles(artifact).Contains("member");
            containers += container ? 1UL : 0UL;
            members += member ? 1UL : 0UL;
            string? parent = OptionalText(artifact, "parent");
            if (parent is null)
            {
                Require(container, "Acquisition root must be a container.");
                JsonElement acquisition = artifact.GetProperty("acquisition");
                Require(Text(acquisition, "transport") == "https" &&
                    new Uri(Text(acquisition, "url")).Scheme == Uri.UriSchemeHttps, "Acquisition must use HTTPS.");
                Require(Unsigned(acquisition, "retry_attempts") is >= 1 and <= 5, "Invalid acquisition retry bound.");
            }
            else
            {
                Require(member && names.TryGetValue(parent, out JsonElement parentArtifact) &&
                    Roles(parentArtifact).Contains("container"), "Artifact parent must precede its member.");
                _ = RelativePath(Text(artifact, "archive_member"));
            }
            names.Add(name, artifact);
            uint mode = Mode(artifact);
            string[] columns = Strings(artifact, "columns");
            ulong header = Unsigned(artifact, "header_record_count");
            if (mode == 1)
            {
                Require(parent is null && !artifact.GetProperty("exact_distribution").GetBoolean() &&
                    columns.Length == 0 && header == 0, "Raw distribution wrapper has invalid table declarations.");
                continue;
            }
            Require(artifact.GetProperty("exact_distribution").GetBoolean(), "Selected table must be exact.");
            ulong rows = Unsigned(artifact, "record_count"), fieldCount = Unsigned(artifact, "field_count");
            // Transport keeps exact declared totals for committed readback.
            // The shared native graph/admission implementation validates columns,
            // grammar, reference coverage, mapping order, and denominator closure.
            records = checked(records + rows);
            fields = checked(fields + fieldCount);
            claims = checked(claims + Unsigned(artifact, "claim_count"));
            mappings = checked(mappings + Unsigned(artifact, "mapping_count"));
        }
        ByteCount = bytes;
        JsonElement denominators = Root.GetProperty("denominators");
        foreach ((string name, ulong value) in new[] {
            ("bytes", bytes), ("files", (ulong)Artifacts.Length), ("containers", containers),
            ("members", members), ("records", records), ("fields", fields), ("claims", claims), ("mappings", mappings) })
            Require(Unsigned(denominators, name) == value, $"Source {name} denominator differs.");
        JsonElement coordinate = Root.GetProperty("coordinate"), profile = Root.GetProperty("profile");
        Require(Unsigned(coordinate, "kind") == 17 && Unsigned(profile, "version") > 0 &&
            Unsigned(coordinate, "version") == Unsigned(profile, "version"), "Invalid source coordinate.");
        JsonElement license = Root.GetProperty("license");
        Require(SHA256.HashData(Utf8.GetBytes(Text(license, "exact_notice_utf8"))).AsSpan()
            .SequenceEqual(Hex(Text(license, "exact_notice_sha256"), 32)), "License notice digest differs.");
        Require(Unsigned(Root.GetProperty("execution"), "preferred_batch_bytes") > 0, "Batch byte bound must be positive.");
    }

    internal static void Require(bool condition, string message)
    { if (!condition) throw new InvalidDataException(message); }
    internal static string Text(JsonElement value, string name) => value.GetProperty(name).GetString()
        ?? throw new InvalidDataException($"{name} must be a string.");
    internal static string? OptionalText(JsonElement value, string name) =>
        value.TryGetProperty(name, out JsonElement item) && item.ValueKind != JsonValueKind.Null ? item.GetString() : null;
    internal static ulong Unsigned(JsonElement value, string name) =>
        value.TryGetProperty(name, out JsonElement item) ? item.GetUInt64() : 0;
    internal static JsonElement[] Array(JsonElement value, string name) =>
        value.TryGetProperty(name, out JsonElement item) ? item.EnumerateArray().ToArray() : [];
    internal static string[] Strings(JsonElement value, string name) => Array(value, name)
        .Select(item => item.GetString() ?? throw new InvalidDataException("Null string declaration.")).ToArray();
    internal static string[] Roles(JsonElement value) => Strings(value, "roles");
    internal static byte[] Hex(string text, int bytes)
    {
        byte[] result = Convert.FromHexString(text);
        Require(result.Length == bytes, $"Expected {bytes}-byte identity.");
        return result;
    }
    internal static string RelativePath(string value)
    {
        Require(value.Length > 0 && !Path.IsPathRooted(value) && !value.Contains('\0') &&
            !value.Contains('\\') && value.Split('/').All(part => part is not "" and not "." and not ".."),
            "Source path must be an exact relative path.");
        return value.Replace('/', Path.DirectorySeparatorChar);
    }
    internal static uint Mode(JsonElement value) => Text(value, "mode") switch
    { "raw_octets" => 1, "utf8_delimited" => 2, "utf8_fixed_width" => 3, _ => throw new InvalidDataException("Unknown artifact mode.") };
    internal static uint Terminator(JsonElement value) => OptionalText(value, "line_terminator") switch
    { null => 0, "lf" => 1, "crlf" => 2, _ => throw new InvalidDataException("Unknown line terminator.") };
    internal static uint Outcome(JsonElement value) => OptionalText(value, "outcome_type") switch
    { null => 0, "assertion" => 1, "measurement" => 2, "prediction" => 3, "observed_consequence" => 4,
      "mapping" => 5, "definition" => 6, "example" => 7, "counterexample" => 8, "unknown_boundary" => 9,
      _ => throw new InvalidDataException("Unknown evidence outcome.") };
    internal static uint ReferenceFlags(JsonElement value) => Roles(value).Aggregate(0U, (flags, role) => flags | (role switch
    { "endpoint" => 1U, "present-declaration" => 2U, "retired-declaration" => 4U, _ => throw new InvalidDataException("Unknown reference role.") }));
    internal static uint MappingFlags(JsonElement value) => Text(value, "direction") switch
    { "directed" => 1, "symmetric" => 2, _ => throw new InvalidDataException("Unknown mapping direction.") };
    internal static uint TrimFlags(JsonElement value) => Text(value, "trim") switch
    { "none" => 0, "left" => 1, "right" => 2, "both" => 3, _ => throw new InvalidDataException("Unknown trim declaration.") };
    internal static uint OverflowField(JsonElement value) => value.TryGetProperty("overflow_field_index", out JsonElement field) &&
        field.ValueKind != JsonValueKind.Null ? field.GetUInt32() : Mode(value) == 3 ? uint.MaxValue : 0;
    internal static int CompareUtf8(string left, string right) => Utf8.GetBytes(left).AsSpan().SequenceCompareTo(Utf8.GetBytes(right));

    internal static byte[] Canonical(JsonElement value)
    {
        var output = new StringBuilder();
        WriteCanonical(value, output);
        return Utf8.GetBytes(output.ToString());
    }
    private static void Quote(string value, StringBuilder output)
    {
        output.Append('"');
        foreach (char character in value)
        {
            string? escape = character switch { '"' => "\\\"", '\\' => "\\\\", '\b' => "\\b", '\f' => "\\f",
                '\n' => "\\n", '\r' => "\\r", '\t' => "\\t", _ => null };
            if (escape is not null) output.Append(escape);
            else if (character < 32) output.Append("\\u").Append(((int)character).ToString("x4", CultureInfo.InvariantCulture));
            else output.Append(character);
        }
        output.Append('"');
    }
    private static void WriteCanonical(JsonElement value, StringBuilder output)
    {
        switch (value.ValueKind)
        {
            case JsonValueKind.Object:
                output.Append('{');
                bool firstProperty = true;
                var names = new HashSet<string>(StringComparer.Ordinal);
                foreach (JsonProperty property in value.EnumerateObject().OrderBy(p => p.Name, Comparer<string>.Create(CompareUtf8)))
                {
                    Require(names.Add(property.Name), "Duplicate JSON property in source declaration or receipt.");
                    if (!firstProperty) output.Append(',');
                    firstProperty = false;
                    Quote(property.Name, output); output.Append(':'); WriteCanonical(property.Value, output);
                }
                output.Append('}'); break;
            case JsonValueKind.Array:
                output.Append('['); bool firstItem = true;
                foreach (JsonElement item in value.EnumerateArray())
                { if (!firstItem) output.Append(','); firstItem = false; WriteCanonical(item, output); }
                output.Append(']'); break;
            case JsonValueKind.String: Quote(value.GetString()!, output); break;
            case JsonValueKind.True: output.Append("true"); break;
            case JsonValueKind.False: output.Append("false"); break;
            case JsonValueKind.Null: output.Append("null"); break;
            case JsonValueKind.Number:
                string raw = value.GetRawText();
                output.Append(raw.IndexOfAny(['.', 'e', 'E']) < 0
                    ? BigInteger.Parse(raw, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture)
                    : FloatRepresentation(value.GetDouble()));
                break;
            default: throw new InvalidDataException("Undefined JSON value.");
        }
    }
    private static string FloatRepresentation(double value)
    {
        Require(double.IsFinite(value), "Non-finite declaration number.");
        bool negative = BitConverter.DoubleToInt64Bits(value) < 0;
        if (value == 0) return negative ? "-0.0" : "0.0";
        string[] parts = Math.Abs(value).ToString("R", CultureInfo.InvariantCulture).ToLowerInvariant().Split('e');
        int exponent = parts.Length == 2 ? int.Parse(parts[1], CultureInfo.InvariantCulture) : 0;
        int point = parts[0].IndexOf('.');
        if (point < 0) point = parts[0].Length;
        string digits = parts[0].Replace(".", "", StringComparison.Ordinal);
        int leading = digits.Length - digits.TrimStart('0').Length;
        exponent += point - leading - 1;
        digits = digits.TrimStart('0').TrimEnd('0');
        string result;
        if (exponent < -4 || exponent >= 16)
            result = digits[0] + (digits.Length == 1 ? "" : "." + digits[1..]) + "e" +
                (exponent < 0 ? "-" : "+") + Math.Abs(exponent).ToString("D2", CultureInfo.InvariantCulture);
        else if (exponent < 0) result = "0." + new string('0', -exponent - 1) + digits;
        else if (exponent + 1 >= digits.Length) result = digits + new string('0', exponent + 1 - digits.Length) + ".0";
        else result = digits.Insert(exponent + 1, ".");
        return negative ? "-" + result : result;
    }
    internal static byte[] Fingerprint(JsonElement value) => SHA256.HashData(Canonical(value));
    internal static byte[] Scope(string field, string value) =>
        SHA256.HashData(Utf8.GetBytes("laplace-source-profile-scope-v1\0" + field + "\0" + value))[..16];
    internal byte[] SelectedBoundary() => Fingerprint(JsonSerializer.SerializeToElement(new {
        coordinate = Root.GetProperty("coordinate"),
        artifacts = Artifacts.Select(artifact => new {
            name = Text(artifact, "name"), parent = OptionalText(artifact, "parent"),
            byte_count = Unsigned(artifact, "byte_count"), sha256 = Text(artifact, "sha256"),
            media_type = Text(artifact, "media_type") }),
        completion = Root.GetProperty("completion") }));
}
