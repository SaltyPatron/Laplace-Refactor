using System.Runtime.InteropServices;

namespace Laplace.Managed;

// Transport layouts for the public tabular_source.h ABI. Source parsing,
// canonical identity, decomposition, and admission remain native operations.
[StructLayout(LayoutKind.Sequential)]
public unsafe struct LaplaceTabularColumn
{
    public byte* Bytes;
    public ulong ByteCount;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceTabularFixedWidthField
{
    public uint Width;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct LaplaceTabularArtifact
{
    public LaplaceDigest256 ArtifactId;
    public LaplaceDigest256 ParentArtifactId;
    public LaplaceDigest256 ExpectedSha256;
    public byte* Bytes;
    public byte* Name;
    public byte* MediaType;
    public LaplaceTabularColumn* Columns;
    public ulong ByteCount;
    public ulong NameByteCount;
    public ulong MediaTypeByteCount;
    public ulong ExpectedRecordCount;
    public ulong ExpectedFieldCount;
    public ulong ReferenceColumnMask;
    public uint Mode;
    public uint Delimiter;
    public uint LineTerminator;
    public uint ExpectedColumnCount;
    public uint OutcomeType;
    public uint HeaderRecordCount;
    public uint Flags;
    public uint Reserved;
    public LaplaceTabularFixedWidthField* FixedWidthFields;
    public uint PaddingByte;
    public uint OverflowFieldIndex;
    public uint MaximumOverflowBytes;
    public uint ExpectedOverflowRecordCount;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceTabularReferenceRule
{
    public LaplaceId128 Namespace;
    public ulong ArtifactIndex;
    public ulong ColumnIndex;
    public uint Kind;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct LaplaceTabularMappingRule
{
    public byte* RelationContent;
    public ulong RelationContentByteCount;
    public ulong ArtifactIndex;
    public ulong LeftColumnIndex;
    public ulong RightColumnIndex;
    public ulong RelationVersion;
    public uint RelationKind;
    public uint Flags;
}

public static unsafe class LaplaceTabularSource
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int IdentifyGraph(
        LaplaceTabularArtifact* artifacts, nuint artifactCount,
        LaplaceTabularReferenceRule* references, nuint referenceCount,
        LaplaceTabularMappingRule* mappings, nuint mappingCount,
        LaplaceDigest256* result);

    public static LaplaceDigest256 IdentifySourceGraph(
        string nativeLibrary,
        ReadOnlySpan<LaplaceTabularArtifact> artifacts,
        ReadOnlySpan<LaplaceTabularReferenceRule> references,
        ReadOnlySpan<LaplaceTabularMappingRule> mappings)
    {
        nint library = NativeLibrary.Load(Path.GetFullPath(nativeLibrary));
        try
        {
            IdentifyGraph identify = Marshal.GetDelegateForFunctionPointer<IdentifyGraph>(
                NativeLibrary.GetExport(library, "laplace_tabular_source_graph_identify"));
            LaplaceDigest256 result = default;
            fixed (LaplaceTabularArtifact* artifactPointer = artifacts)
            fixed (LaplaceTabularReferenceRule* referencePointer = references)
            fixed (LaplaceTabularMappingRule* mappingPointer = mappings)
            {
                int status = identify(
                    artifactPointer, checked((nuint)artifacts.Length),
                    references.IsEmpty ? null : referencePointer, checked((nuint)references.Length),
                    mappings.IsEmpty ? null : mappingPointer, checked((nuint)mappings.Length), &result);
                if (status != 0)
                    throw new InvalidDataException($"Native source graph calculation failed: status={status}.");
            }
            return result;
        }
        finally
        {
            NativeLibrary.Free(library);
        }
    }
}
