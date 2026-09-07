using System.Runtime.InteropServices;

namespace Laplace.Managed;

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceSourceClaim
{
    public LaplaceEvidenceLineageRecord Lineage;
    public uint OutcomeType;
}

public static unsafe class LaplaceSourceEvidence
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int BuildTestimonyBatch(
        LaplaceSourceClaim* claims, nuint claimCount,
        LaplaceSourceProfileManifest* profile, LaplaceDigest256* sourceFingerprint,
        LaplaceEvidenceTestimonyRecord* records, nuint recordCapacity);

    // Marshaling only: native C constructs, validates, identifies, and orders
    // the testimony. The caller owns the finite input and output buffers.
    public static void BuildTestimony(
        string nativeLibrary, ReadOnlySpan<LaplaceSourceClaim> claims,
        in LaplaceSourceProfileManifest profile, in LaplaceDigest256 sourceFingerprint,
        Span<LaplaceEvidenceTestimonyRecord> records)
    {
        if (claims.IsEmpty || records.Length < claims.Length)
            throw new ArgumentException("Testimony output must cover the complete claim batch.");
        nint library = NativeLibrary.Load(Path.GetFullPath(nativeLibrary));
        try
        {
            BuildTestimonyBatch build = Marshal.GetDelegateForFunctionPointer<BuildTestimonyBatch>(
                NativeLibrary.GetExport(library, "laplace_source_testimony_build_batch"));
            fixed (LaplaceSourceClaim* claimPointer = claims)
            fixed (LaplaceSourceProfileManifest* profilePointer = &profile)
            fixed (LaplaceDigest256* sourcePointer = &sourceFingerprint)
            fixed (LaplaceEvidenceTestimonyRecord* recordPointer = records)
            {
                int status = build(claimPointer, checked((nuint)claims.Length), profilePointer,
                    sourcePointer, recordPointer, checked((nuint)records.Length));
                if (status != 0)
                {
                    records[..claims.Length].Clear();
                    throw new InvalidDataException($"Native source testimony construction failed: status={status}.");
                }
            }
        }
        finally { NativeLibrary.Free(library); }
    }
}
