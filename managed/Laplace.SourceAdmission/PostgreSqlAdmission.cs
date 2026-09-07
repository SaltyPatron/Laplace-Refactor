using System.Buffers.Binary;
using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;
using static Laplace.SourceAdmission.SourceDocument;

namespace Laplace.SourceAdmission;

internal static class PostgreSqlAdmission
{
    private static string Number(ulong value) => value.ToString(CultureInfo.InvariantCulture);
    private static string Bytes(ReadOnlySpan<byte> value) => $"decode('{Convert.ToHexStringLower(value)}','hex')";
    private static string TextValue(string value) => $"convert_from({Bytes(Utf8.GetBytes(value))},'UTF8')";
    private static string Row(IEnumerable<string> values, string type) => "ROW(" + string.Join(',', values) + ")::laplace." + type;
    private static string Vector(IEnumerable<string> values, string type) => "ARRAY[" + string.Join(',', values) + "]::" + type + "[]";

    internal static string Context(JsonElement context, JsonElement framework, out byte[] geometry)
    {
        string[] slots = framework.GetProperty("epoch_slots").EnumerateObject()
            .OrderBy(property => property.Value.GetInt32()).Select(property => property.Name).ToArray();
        JsonElement epochs = context.GetProperty("epochs");
        Require(epochs.EnumerateObject().Select(property => property.Name).ToHashSet(StringComparer.Ordinal)
            .SetEquals(slots), "Every named context epoch is required.");
        byte[][] values = slots.Select(name => Hex(Text(epochs, name), 32)).ToArray();
        ulong mask = Unsigned(context, "epoch_mask");
        int geometrySlot = framework.GetProperty("epoch_slots").GetProperty("geometry").GetInt32();
        geometry = values[geometrySlot];
        ulong memory = Unsigned(context, "memory_bytes"), cpu = Unsigned(context, "cpu_slots"), io = Unsigned(context, "io_slots");
        ulong flags = Unsigned(context, "flags");
        ulong major = Unsigned(context, "framework_major"), minor = Unsigned(context, "framework_minor");
        // Check SQL storage widths only. laplace_pg_read_execution_context calls
        // the shared native framework validator; operation-specific requirements
        // remain with the native operation. In particular, native contexts allow
        // an io_slots value of zero and the transport must preserve it.
        Require(memory <= long.MaxValue && cpu <= int.MaxValue && io <= int.MaxValue &&
            mask <= long.MaxValue && flags <= int.MaxValue && major <= (ulong)short.MaxValue && minor <= (ulong)short.MaxValue,
            "Execution context exceeds its SQL transport width.");
        return Row([Vector(values.Select(value => Bytes(value)), "bytea"), Bytes(Hex(Text(context, "authority_fingerprint"), 32)),
            Number(memory), Number(cpu), Number(io), Number(mask), Number(major), Number(minor), Number(flags)], "execution_context");
    }

    private static string Profile(SourceDocument document, byte[] graph, JsonElement execution)
    {
        JsonElement coordinate = document.Root.GetProperty("coordinate"), profile = document.Root.GetProperty("profile");
        JsonElement flagContract = execution.GetProperty("flags");
        string sourceClass = Text(profile, "source_class"), evidenceType = Text(profile, "evidence_source_type");
        ulong flags = flagContract.GetProperty(sourceClass).GetUInt64() << checked((int)Unsigned(flagContract, "epistemic_class_shift")) |
            flagContract.GetProperty("evidence_source_types").GetProperty(evidenceType).GetUInt64()
            << checked((int)Unsigned(flagContract, "evidence_source_type_shift"));
        var fields = new List<string> { Bytes(new byte[32]), Number(Unsigned(coordinate, "kind")) };
        fields.AddRange(new[] { "authority", "release", "namespace", "local_identifier" }
            .Select(name => Bytes(Scope(name, Text(coordinate, name)))));
        fields.Add(Number(Unsigned(coordinate, "version")));
        foreach (string? section in new string?[] { "authority_and_release", "license", null, "syntax_authority",
            "recipe_program", "universal_ast_mapping", "highway_and_references", "epistemic_witnessing", "denominators", "conformance", "completion" })
            fields.Add(Bytes(section is null ? graph : Fingerprint(document.Root.GetProperty(section))));
        fields.Add(Bytes(document.SelectedBoundary()));
        fields.AddRange(Enumerable.Repeat("0", 30));
        fields.Add(Text(profile, "reconstruction_class") switch { "exact" => "1", "semantic" => "2", "none" => "3",
            _ => throw new InvalidDataException("Unknown reconstruction class.") });
        fields.Add(Number(flags));
        return Row(fields, "source_profile_manifest");
    }

    private static string Artifact(SourceDocument document, int index, string? serverRoot)
    {
        JsonElement artifact = document.Artifacts[index];
        string? parent = OptionalText(artifact, "parent");
        byte[] parentId = parent is null ? new byte[32] : Hex(Text(document.Artifacts.Single(item => Text(item, "name") == parent), "sha256"), 32);
        string[] columns = Strings(artifact, "columns");
        ulong referenceMask = Array(artifact, "reference_columns").Aggregate(0UL,
            (mask, column) => mask | (1UL << checked((int)column.GetUInt64())));
        uint flags = (Roles(artifact).Contains("container") ? 1U : 0) | (Roles(artifact).Contains("member") ? 2U : 0) |
            (artifact.GetProperty("exact_distribution").GetBoolean() ? 4U : 0);
        string content = serverRoot is null ? $"(SELECT content FROM pg_temp.laplace_source_input WHERE artifact_index={index})" :
            TextValue(Path.Combine(serverRoot, RelativePath(Text(artifact, "local_discovery_path"))));
        var fields = new List<string> {
            Bytes(Hex(Text(artifact, "sha256"), 32)), Bytes(parentId), Bytes(Hex(Text(artifact, "sha256"), 32)), content,
            Bytes(Utf8.GetBytes(Text(artifact, "name"))), Bytes(Utf8.GetBytes(Text(artifact, "media_type"))),
            Number(Unsigned(artifact, "record_count")), Number(Unsigned(artifact, "field_count")), Number(referenceMask),
            Number(Mode(artifact)), Number(Unsigned(artifact, "delimiter")), Number(Terminator(artifact)), Number((ulong)columns.Length),
            Number(Outcome(artifact)), Number(flags), Vector(columns.Select(column => Bytes(Utf8.GetBytes(column))), "bytea"),
            Number(Unsigned(artifact, "header_record_count")),
            Vector(Array(artifact, "fixed_width_fields").Select(field => Row([Number(Unsigned(field, "width")), Number(TrimFlags(field))],
                "tabular_fixed_width_field")), "laplace.tabular_fixed_width_field"),
            Number(Unsigned(artifact, "padding_byte")), Number(OverflowField(artifact)),
            Number(Unsigned(artifact, "maximum_overflow_bytes")), Number(Unsigned(artifact, "overflow_record_count")) };
        if (serverRoot is not null) fields.Add(Number(Unsigned(artifact, "byte_count")));
        return Row(fields, serverRoot is null ? "tabular_source_artifact_v2" : "tabular_source_artifact_file");
    }

    internal static string Render(SourceDocument document, JsonElement context, JsonElement framework,
        JsonElement execution, byte[] graph, string? copyPath, string? serverRoot, uint timeoutMilliseconds)
    {
        Require((copyPath is null) != (serverRoot is null), "Select exactly one source content transport.");
        string contextSql = Context(context, framework, out byte[] geometry);
        var references = new List<string>(); var mappings = new List<string>();
        for (int index = 0; index < document.Artifacts.Length; ++index)
        {
            foreach (JsonElement binding in Array(document.Artifacts[index], "reference_bindings"))
                references.Add(Row([Number((ulong)index), Number(Unsigned(binding, "column")), Bytes(Scope("namespace", Text(binding, "namespace"))),
                    Number(Unsigned(binding, "kind")), Number(ReferenceFlags(binding))], "tabular_reference_rule"));
            foreach (JsonElement binding in Array(document.Artifacts[index], "mapping_bindings"))
                mappings.Add(Row([Number((ulong)index), Number(Unsigned(binding, "left_column")), Number(Unsigned(binding, "right_column")),
                    Bytes(Utf8.GetBytes(Text(binding, "relation_content"))), Number(Unsigned(binding, "relation_version")),
                    Number(Unsigned(binding, "relation_kind")), Number(MappingFlags(binding))], "tabular_mapping_rule"));
        }
        string arguments = string.Join(",\n", new[] { contextSql, Profile(document, graph, execution), Bytes(geometry),
            Bytes(Fingerprint(document.Root.GetProperty("execution").GetProperty("occurrence_context"))),
            Vector(Enumerable.Range(0, document.Artifacts.Length).Select(index => Artifact(document, index, serverRoot)),
                serverRoot is null ? "laplace.tabular_source_artifact_v2" : "laplace.tabular_source_artifact_file"),
            Vector(references, "laplace.tabular_reference_rule"), Vector(mappings, "laplace.tabular_mapping_rule"),
            Number(Unsigned(document.Root.GetProperty("execution"), "preferred_batch_bytes")) });
        if (copyPath is not null)
            Require(copyPath.IndexOfAny(['\r', '\n', '\\', '\'']) < 0, "Unsupported COPY spool path.");
        var values = new Dictionary<string, string>(StringComparer.Ordinal) {
            ["SOURCE_COPY"] = copyPath is null ? "false" : "true", ["COPY_PATH"] = copyPath ?? "",
            ["TIMEOUT_MS"] = Number(timeoutMilliseconds), ["SOURCE_GRAPH"] = Bytes(graph),
            ["SELECTED_BOUNDARY"] = Bytes(document.SelectedBoundary()),
            ["DENOMINATORS"] = TextValue(document.Root.GetProperty("denominators").GetRawText()),
            ["ARGUMENTS"] = arguments };
        string template = File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "sql", "source-admit.sql"), Utf8);
        // One substitution pass preserves literal token-like bytes in a path.
        // The SQL module owns transaction, admission and committed readback.
        return System.Text.RegularExpressions.Regex.Replace(template, "@@([A-Z_]+)@@",
            match => values[match.Groups[1].Value]);
    }

    internal static void WriteCopy(SourceDocument document, string root, string path, CancellationToken cancellation)
    {
        using var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        output.Write([0x50, 0x47, 0x43, 0x4f, 0x50, 0x59, 0x0a, 0xff, 0x0d, 0x0a, 0x00]);
        WriteInt32(output, 0); WriteInt32(output, 0);
        byte[] block = new byte[1024 * 1024];
        for (int index = 0; index < document.Artifacts.Length; ++index)
        {
            JsonElement artifact = document.Artifacts[index];
            ulong remaining = Unsigned(artifact, "byte_count");
            Require(remaining < (1UL << 30) - 4, "Artifact exceeds the bytea transport limit.");
            WriteInt16(output, 2); WriteInt32(output, 4); WriteInt32(output, index); WriteInt32(output, checked((int)remaining));
            using IncrementalHash digest = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            using FileStream input = File.OpenRead(Path.Combine(root, RelativePath(Text(artifact, "local_discovery_path"))));
            while (remaining != 0)
            {
                cancellation.ThrowIfCancellationRequested();
                int count = checked((int)Math.Min((ulong)block.Length, remaining));
                input.ReadExactly(block.AsSpan(0, count)); output.Write(block, 0, count); digest.AppendData(block, 0, count);
                remaining -= checked((ulong)count);
            }
            Require(input.ReadByte() == -1 && digest.GetHashAndReset().AsSpan().SequenceEqual(Hex(Text(artifact, "sha256"), 32)),
                "Artifact changed during binary transport.");
        }
        WriteInt16(output, -1);
    }
    private static void WriteInt32(Stream output, int value)
    { Span<byte> bytes = stackalloc byte[4]; BinaryPrimitives.WriteInt32BigEndian(bytes, value); output.Write(bytes); }
    private static void WriteInt16(Stream output, short value)
    { Span<byte> bytes = stackalloc byte[2]; BinaryPrimitives.WriteInt16BigEndian(bytes, value); output.Write(bytes); }
}
