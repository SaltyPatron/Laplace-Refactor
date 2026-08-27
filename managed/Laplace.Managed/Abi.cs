using System.Runtime.InteropServices;

namespace Laplace.Managed;

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceId128 : IEquatable<LaplaceId128>
{
    public ulong Word0;
    public ulong Word1;

    public readonly bool Equals(LaplaceId128 other) =>
        Word0 == other.Word0 && Word1 == other.Word1;

    public override readonly bool Equals(object? obj) =>
        obj is LaplaceId128 other && Equals(other);

    public override readonly int GetHashCode() => HashCode.Combine(Word0, Word1);
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceDigest256 : IEquatable<LaplaceDigest256>
{
    public ulong Word0;
    public ulong Word1;
    public ulong Word2;
    public ulong Word3;

    public readonly bool Equals(LaplaceDigest256 other) =>
        Word0 == other.Word0 && Word1 == other.Word1 &&
        Word2 == other.Word2 && Word3 == other.Word3;

    public override readonly bool Equals(object? obj) =>
        obj is LaplaceDigest256 other && Equals(other);

    public override readonly int GetHashCode() => HashCode.Combine(Word0, Word1, Word2, Word3);
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceTrajectoryCarrier
{
    public double Slot0;
    public double Slot1;
    public double Slot2;
    public double Slot3;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceCompositionOccurrence
{
    public LaplaceId128 EntityId;
    public ulong LogicalOrdinal;
    public ulong Metadata;
    public uint Atom;
    public ushort PackedOrdinal;
    public ushort RunLength;
    public byte Tier;
    public byte HasAtom;
    public ushort Reserved;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceHighwayKey
{
    public uint Kind;
    public uint Reserved;
    public LaplaceId128 Authority;
    public LaplaceId128 Release;
    public LaplaceId128 Namespace;
    public LaplaceId128 LocalIdentifier;
    public ulong Version;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceHighwayCoordinate
{
    public LaplaceId128 Coordinate;
    public LaplaceDigest256 CollisionFingerprint;
    public uint Kind;
    public uint Reserved;
    public ulong Version;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceHighwayRegistryReceipt
{
    public LaplaceDigest256 ReceiptId;
    public LaplaceDigest256 ContextFingerprint;
    public LaplaceDigest256 RegistryFingerprint;
    public LaplaceId128 ActivationEpochId;
    public LaplaceDigest256 ActivationEpochFingerprint;
    public ulong RegistryVersion;
    public ulong KindCount;
    public ulong AliasCount;
    public ulong DispositionCount;
    public uint Status;
    public uint Reserved;
}

public enum LaplaceIsaStatus : uint
{
    Ok = 0,
    InvalidArgument = 1,
    UnsupportedVersion = 2,
    EmptyProgram = 3,
    UnknownFlags = 4,
    UnknownOpcode = 5,
    UnsupportedInstructionVersion = 6,
    RegisterOutOfRange = 7,
    ValueTypeMismatch = 8,
    ValueInvalid = 9,
    ResultCapacityInsufficient = 10,
    ValueOverlap = 11,
    InputOutOfRange = 12,
    ExecutionFailed = 13,
    ContextInvalid = 14,
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceIsaError
{
    public LaplaceIsaStatus Status;
    public ulong InstructionIndex;
    public uint ValueIndex;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceIsaReceipt
{
    public LaplaceDigest256 ReceiptId;
    public LaplaceDigest256 ContextFingerprint;
    public LaplaceDigest256 ProgramFingerprint;
    public LaplaceDigest256 InputFingerprint;
    public LaplaceDigest256 OutputFingerprint;
    public ulong InstructionCount;
    public ulong ExecutedInstructionCount;
    public ushort Major;
    public ushort Minor;
    public uint ReceiptDetail;
    public LaplaceIsaStatus Status;
    public uint Reserved;
}

public readonly record struct LaplaceValueTypeDescriptor(
    uint Type,
    string Name,
    string ManagedType);

public readonly record struct LaplaceOperationDescriptor(
    uint Opcode,
    uint Module,
    uint InputType,
    uint OutputType,
    ushort InstructionVersion,
    ushort IntroducedMinor,
    string Name);

public interface ILaplaceOperation<TInput, TOutput>
    where TInput : unmanaged
    where TOutput : unmanaged
{
    static abstract LaplaceOperationDescriptor Descriptor { get; }
}

public sealed class LaplaceExecutionContext
{
    public const int NativeAbiSize = 392;
    private readonly byte[] bytes;

    public LaplaceExecutionContext(ReadOnlySpan<byte> nativeAbiBytes)
    {
        if (nativeAbiBytes.Length != NativeAbiSize)
        {
            throw new ArgumentException(
                $"Framework context ABI must be exactly {NativeAbiSize} bytes.",
                nameof(nativeAbiBytes));
        }
        bytes = nativeAbiBytes.ToArray();
    }

    internal byte[] AbiBytes => bytes;
}

public readonly record struct LaplaceIsaExecution<TOutput>(
    TOutput[] Output,
    ulong OutputCount,
    LaplaceIsaStatus Status,
    LaplaceIsaReceipt Receipt,
    LaplaceIsaError Error)
    where TOutput : unmanaged
{
    public ReadOnlyMemory<TOutput> PublishedOutput =>
        Output.AsMemory(0, checked((int)OutputCount));
}

internal static class NativeAbi
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct ValueView
    {
        internal nint Data;
        internal ulong Count;
        internal ulong Capacity;
        internal uint StrideBytes;
        internal uint Type;
        internal uint Flags;
        internal uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Instruction
    {
        internal uint Opcode;
        internal uint InputValue;
        internal uint OutputValue;
        internal ushort Version;
        internal ushort Flags;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct Program
    {
        internal Instruction* Instructions;
        internal ValueView* Values;
        internal void* Context;
        internal ulong InstructionCount;
        internal ulong ValueCount;
        internal ushort Major;
        internal ushort Minor;
        internal uint Flags;
        internal uint ReceiptDetail;
        internal uint Reserved;
    }
}

public static class LaplaceNativeAbi
{
    public static uint[] Measure() =>
    [
        checked((uint)Marshal.SizeOf<LaplaceDigest256>()),
        checked((uint)Marshal.SizeOf<LaplaceId128>()),
        checked((uint)Marshal.SizeOf<LaplaceTrajectoryCarrier>()),
        checked((uint)Marshal.SizeOf<LaplaceCompositionOccurrence>()),
        OffsetOf<LaplaceCompositionOccurrence>(nameof(LaplaceCompositionOccurrence.EntityId)),
        OffsetOf<LaplaceCompositionOccurrence>(nameof(LaplaceCompositionOccurrence.LogicalOrdinal)),
        OffsetOf<LaplaceCompositionOccurrence>(nameof(LaplaceCompositionOccurrence.Metadata)),
        OffsetOf<LaplaceCompositionOccurrence>(nameof(LaplaceCompositionOccurrence.Atom)),
        OffsetOf<LaplaceCompositionOccurrence>(nameof(LaplaceCompositionOccurrence.PackedOrdinal)),
        OffsetOf<LaplaceCompositionOccurrence>(nameof(LaplaceCompositionOccurrence.RunLength)),
        OffsetOf<LaplaceCompositionOccurrence>(nameof(LaplaceCompositionOccurrence.Tier)),
        OffsetOf<LaplaceCompositionOccurrence>(nameof(LaplaceCompositionOccurrence.HasAtom)),
        OffsetOf<LaplaceCompositionOccurrence>(nameof(LaplaceCompositionOccurrence.Reserved)),
        checked((uint)Marshal.SizeOf<LaplaceHighwayKey>()),
        OffsetOf<LaplaceHighwayKey>(nameof(LaplaceHighwayKey.Kind)),
        OffsetOf<LaplaceHighwayKey>(nameof(LaplaceHighwayKey.Reserved)),
        OffsetOf<LaplaceHighwayKey>(nameof(LaplaceHighwayKey.Authority)),
        OffsetOf<LaplaceHighwayKey>(nameof(LaplaceHighwayKey.Release)),
        OffsetOf<LaplaceHighwayKey>(nameof(LaplaceHighwayKey.Namespace)),
        OffsetOf<LaplaceHighwayKey>(nameof(LaplaceHighwayKey.LocalIdentifier)),
        OffsetOf<LaplaceHighwayKey>(nameof(LaplaceHighwayKey.Version)),
        checked((uint)Marshal.SizeOf<LaplaceHighwayCoordinate>()),
        OffsetOf<LaplaceHighwayCoordinate>(nameof(LaplaceHighwayCoordinate.Coordinate)),
        OffsetOf<LaplaceHighwayCoordinate>(nameof(LaplaceHighwayCoordinate.CollisionFingerprint)),
        OffsetOf<LaplaceHighwayCoordinate>(nameof(LaplaceHighwayCoordinate.Kind)),
        OffsetOf<LaplaceHighwayCoordinate>(nameof(LaplaceHighwayCoordinate.Reserved)),
        OffsetOf<LaplaceHighwayCoordinate>(nameof(LaplaceHighwayCoordinate.Version)),
        checked((uint)Marshal.SizeOf<LaplaceHighwayRegistryReceipt>()),
        OffsetOf<LaplaceHighwayRegistryReceipt>(nameof(LaplaceHighwayRegistryReceipt.ReceiptId)),
        OffsetOf<LaplaceHighwayRegistryReceipt>(nameof(LaplaceHighwayRegistryReceipt.ContextFingerprint)),
        OffsetOf<LaplaceHighwayRegistryReceipt>(nameof(LaplaceHighwayRegistryReceipt.RegistryFingerprint)),
        OffsetOf<LaplaceHighwayRegistryReceipt>(nameof(LaplaceHighwayRegistryReceipt.ActivationEpochId)),
        OffsetOf<LaplaceHighwayRegistryReceipt>(nameof(LaplaceHighwayRegistryReceipt.ActivationEpochFingerprint)),
        OffsetOf<LaplaceHighwayRegistryReceipt>(nameof(LaplaceHighwayRegistryReceipt.RegistryVersion)),
        OffsetOf<LaplaceHighwayRegistryReceipt>(nameof(LaplaceHighwayRegistryReceipt.KindCount)),
        OffsetOf<LaplaceHighwayRegistryReceipt>(nameof(LaplaceHighwayRegistryReceipt.AliasCount)),
        OffsetOf<LaplaceHighwayRegistryReceipt>(nameof(LaplaceHighwayRegistryReceipt.DispositionCount)),
        OffsetOf<LaplaceHighwayRegistryReceipt>(nameof(LaplaceHighwayRegistryReceipt.Status)),
        OffsetOf<LaplaceHighwayRegistryReceipt>(nameof(LaplaceHighwayRegistryReceipt.Reserved)),
        checked((uint)Marshal.SizeOf<NativeAbi.ValueView>()),
        OffsetOf<NativeAbi.ValueView>("Data"),
        OffsetOf<NativeAbi.ValueView>("Count"),
        OffsetOf<NativeAbi.ValueView>("Capacity"),
        OffsetOf<NativeAbi.ValueView>("StrideBytes"),
        OffsetOf<NativeAbi.ValueView>("Type"),
        OffsetOf<NativeAbi.ValueView>("Flags"),
        OffsetOf<NativeAbi.ValueView>("Reserved"),
        checked((uint)Marshal.SizeOf<NativeAbi.Instruction>()),
        OffsetOf<NativeAbi.Instruction>("Opcode"),
        OffsetOf<NativeAbi.Instruction>("InputValue"),
        OffsetOf<NativeAbi.Instruction>("OutputValue"),
        OffsetOf<NativeAbi.Instruction>("Version"),
        OffsetOf<NativeAbi.Instruction>("Flags"),
        checked((uint)Marshal.SizeOf<NativeAbi.Program>()),
        OffsetOf<NativeAbi.Program>("Instructions"),
        OffsetOf<NativeAbi.Program>("Values"),
        OffsetOf<NativeAbi.Program>("Context"),
        OffsetOf<NativeAbi.Program>("InstructionCount"),
        OffsetOf<NativeAbi.Program>("ValueCount"),
        OffsetOf<NativeAbi.Program>("Major"),
        OffsetOf<NativeAbi.Program>("Minor"),
        OffsetOf<NativeAbi.Program>("Flags"),
        OffsetOf<NativeAbi.Program>("ReceiptDetail"),
        OffsetOf<NativeAbi.Program>("Reserved"),
        checked((uint)Marshal.SizeOf<LaplaceIsaError>()),
        OffsetOf<LaplaceIsaError>(nameof(LaplaceIsaError.Status)),
        OffsetOf<LaplaceIsaError>(nameof(LaplaceIsaError.InstructionIndex)),
        OffsetOf<LaplaceIsaError>(nameof(LaplaceIsaError.ValueIndex)),
        OffsetOf<LaplaceIsaError>(nameof(LaplaceIsaError.Reserved)),
        checked((uint)Marshal.SizeOf<LaplaceIsaReceipt>()),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.ReceiptId)),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.ContextFingerprint)),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.ProgramFingerprint)),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.InputFingerprint)),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.OutputFingerprint)),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.InstructionCount)),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.ExecutedInstructionCount)),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.Major)),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.Minor)),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.ReceiptDetail)),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.Status)),
        OffsetOf<LaplaceIsaReceipt>(nameof(LaplaceIsaReceipt.Reserved)),
        LaplaceExecutionContext.NativeAbiSize,
    ];

    private static uint OffsetOf<T>(string field) where T : struct =>
        checked((uint)Marshal.OffsetOf<T>(field).ToInt64());
}
