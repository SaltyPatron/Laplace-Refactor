using System.Runtime.InteropServices;
using System.Text.Json;
using Laplace.Managed;
using static Laplace.SourceAdmission.SourceDocument;

namespace Laplace.SourceAdmission;

internal sealed unsafe class NativeSourceInputs : IDisposable
{
    private readonly List<nint> allocations = [];
    private readonly ulong maximumBytes;
    private ulong allocatedBytes;
    internal LaplaceTabularArtifact[] Artifacts { get; }
    internal LaplaceTabularReferenceRule[] References { get; }
    internal LaplaceTabularMappingRule[] Mappings { get; }

    internal NativeSourceInputs(SourceDocument document, string sourceRoot, ulong maximumBytes)
    {
        Require(document.ByteCount <= maximumBytes, "Native input exceeds its declared byte bound.");
        this.maximumBytes = maximumBytes;
        Artifacts = new LaplaceTabularArtifact[document.Artifacts.Length];
        var references = new List<LaplaceTabularReferenceRule>();
        var mappings = new List<LaplaceTabularMappingRule>();
        var declarations = document.Artifacts.ToDictionary(item => Text(item, "name"), StringComparer.Ordinal);
        try
        {
            for (int index = 0; index < Artifacts.Length; ++index)
            {
                JsonElement source = document.Artifacts[index];
                ref LaplaceTabularArtifact artifact = ref Artifacts[index];
                artifact.ByteCount = Unsigned(source, "byte_count");
                artifact.Bytes = Allocate(artifact.ByteCount);
                // A private input copy keeps the bytes stable for native digest
                // calculation, including if the discovery file changes later.
                using (FileStream stream = File.OpenRead(Path.Combine(sourceRoot, RelativePath(Text(source, "local_discovery_path")))))
                {
                    Require(checked((ulong)stream.Length) == artifact.ByteCount, "Source extent changed before native calculation.");
                    ulong offset = 0;
                    while (offset < artifact.ByteCount)
                    {
                        int count = checked((int)Math.Min(1024UL * 1024, artifact.ByteCount - offset));
                        stream.ReadExactly(new Span<byte>(artifact.Bytes + checked((long)offset), count));
                        offset += checked((ulong)count);
                    }
                    Require(stream.ReadByte() == -1, "Source extent grew during native acquisition.");
                }
                artifact.ArtifactId = Digest(Hex(Text(source, "sha256"), 32));
                artifact.ExpectedSha256 = artifact.ArtifactId;
                string? parent = OptionalText(source, "parent");
                artifact.ParentArtifactId = parent is null ? default : Digest(Hex(Text(declarations[parent], "sha256"), 32));
                artifact.Name = StringBytes(Text(source, "name"), out artifact.NameByteCount);
                artifact.MediaType = StringBytes(Text(source, "media_type"), out artifact.MediaTypeByteCount);
                artifact.ExpectedRecordCount = Unsigned(source, "record_count");
                artifact.ExpectedFieldCount = Unsigned(source, "field_count");
                artifact.Mode = Mode(source);
                artifact.Delimiter = checked((uint)Unsigned(source, "delimiter"));
                artifact.LineTerminator = Terminator(source);
                artifact.OutcomeType = Outcome(source);
                artifact.HeaderRecordCount = checked((uint)Unsigned(source, "header_record_count"));
                artifact.Flags = (Roles(source).Contains("container") ? 1U : 0) |
                    (Roles(source).Contains("member") ? 2U : 0) | (source.GetProperty("exact_distribution").GetBoolean() ? 4U : 0);
                string[] columns = Strings(source, "columns");
                artifact.ExpectedColumnCount = checked((uint)columns.Length);
                if (columns.Length > 0)
                {
                    artifact.Columns = (LaplaceTabularColumn*)Allocate(checked((ulong)columns.Length * (ulong)sizeof(LaplaceTabularColumn)));
                    for (int column = 0; column < columns.Length; ++column)
                        artifact.Columns[column].Bytes = StringBytes(columns[column], out artifact.Columns[column].ByteCount);
                }
                foreach (JsonElement column in Array(source, "reference_columns"))
                {
                    ulong columnIndex = column.GetUInt64();
                    Require(columnIndex < 64, "Reference column exceeds the native mask width.");
                    artifact.ReferenceColumnMask |= 1UL << (int)columnIndex;
                }
                JsonElement[] fixedFields = Array(source, "fixed_width_fields");
                Require(artifact.Mode == 3 ? fixedFields.Length == columns.Length : fixedFields.Length == 0,
                    "Fixed-width descriptor storage must match the native column extent.");
                if (fixedFields.Length > 0)
                {
                    artifact.FixedWidthFields = (LaplaceTabularFixedWidthField*)Allocate(
                        checked((ulong)fixedFields.Length * (ulong)sizeof(LaplaceTabularFixedWidthField)));
                    for (int field = 0; field < fixedFields.Length; ++field)
                        artifact.FixedWidthFields[field] = new() {
                            Width = checked((uint)Unsigned(fixedFields[field], "width")), Flags = TrimFlags(fixedFields[field]) };
                }
                artifact.PaddingByte = checked((uint)Unsigned(source, "padding_byte"));
                artifact.OverflowFieldIndex = OverflowField(source);
                artifact.MaximumOverflowBytes = checked((uint)Unsigned(source, "maximum_overflow_bytes"));
                artifact.ExpectedOverflowRecordCount = checked((uint)Unsigned(source, "overflow_record_count"));
                foreach (JsonElement reference in Array(source, "reference_bindings"))
                    references.Add(new() {
                        Namespace = MemoryMarshal.Read<LaplaceId128>(Scope("namespace", Text(reference, "namespace"))),
                        ArtifactIndex = checked((ulong)index), ColumnIndex = Unsigned(reference, "column"),
                        Kind = checked((uint)Unsigned(reference, "kind")), Flags = ReferenceFlags(reference) });
                foreach (JsonElement mapping in Array(source, "mapping_bindings"))
                {
                    byte* relation = StringBytes(Text(mapping, "relation_content"), out ulong length);
                    mappings.Add(new() { RelationContent = relation, RelationContentByteCount = length,
                        ArtifactIndex = checked((ulong)index), LeftColumnIndex = Unsigned(mapping, "left_column"),
                        RightColumnIndex = Unsigned(mapping, "right_column"), RelationVersion = Unsigned(mapping, "relation_version"),
                        RelationKind = checked((uint)Unsigned(mapping, "relation_kind")), Flags = MappingFlags(mapping) });
                }
            }
            References = references.ToArray();
            Mappings = mappings.ToArray();
        }
        catch { Dispose(); throw; }
    }

    private byte* Allocate(ulong count)
    {
        count = Math.Max(count, 1UL);
        Require(count <= maximumBytes - allocatedBytes, "Native source allocations exceed the supplied memory grant.");
        Require(count <= checked((ulong)nint.MaxValue), "Native allocation exceeds addressable extent.");
        byte* pointer = (byte*)NativeMemory.Alloc(checked((nuint)Math.Max(count, 1UL)));
        if (pointer == null) throw new OutOfMemoryException("Native source input allocation failed.");
        try { allocations.Add((nint)pointer); }
        catch { NativeMemory.Free(pointer); throw; }
        allocatedBytes += count;
        return pointer;
    }
    private byte* StringBytes(string value, out ulong count)
    {
        byte[] encoded = Utf8.GetBytes(value);
        count = checked((ulong)encoded.Length);
        byte* pointer = Allocate(count + 1);
        encoded.CopyTo(new Span<byte>(pointer, encoded.Length));
        pointer[encoded.Length] = 0;
        return pointer;
    }
    private static LaplaceDigest256 Digest(ReadOnlySpan<byte> value) => MemoryMarshal.Read<LaplaceDigest256>(value);
    internal byte[] Identify(string library)
    {
        LaplaceDigest256 digest = LaplaceTabularSource.IdentifySourceGraph(library, Artifacts, References, Mappings);
        return MemoryMarshal.AsBytes(MemoryMarshal.CreateReadOnlySpan(ref digest, 1)).ToArray();
    }
    public void Dispose()
    {
        foreach (nint pointer in allocations) NativeMemory.Free((void*)pointer);
        allocations.Clear();
        allocatedBytes = 0;
    }
}
