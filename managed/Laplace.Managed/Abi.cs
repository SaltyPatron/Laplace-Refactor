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
public struct LaplaceReferenceCandidate
{
    public LaplaceDigest256 SourceProfileId;
    public LaplaceHighwayKey Key;
    public LaplaceId128 RowEntityId;
    public LaplaceId128 FieldEntityId;
    public LaplaceId128 ValueEntityId;
    public ulong SourceOrdinal;
    public ulong ArtifactOrdinal;
    public ulong RowOrdinal;
    public ulong ColumnOrdinal;
    public uint RuleFlags;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceReferenceRecord
{
    public LaplaceReferenceCandidate Candidate;
    public LaplaceHighwayCoordinate Coordinate;
    public LaplaceDigest256 OccurrenceId;
    public LaplaceDigest256 ReferenceId;
    public uint Disposition;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceReferenceMappingCandidate
{
    public LaplaceDigest256 BoundaryId;
    public LaplaceDigest256 SourceProfileId;
    public LaplaceDigest256 LeftReferenceId;
    public LaplaceDigest256 RightReferenceId;
    public LaplaceHighwayCoordinate LeftCoordinate;
    public LaplaceHighwayCoordinate RightCoordinate;
    public LaplaceId128 RelationId;
    public LaplaceId128 RowEntityId;
    public LaplaceId128 LeftFieldEntityId;
    public LaplaceId128 LeftValueEntityId;
    public LaplaceId128 RightFieldEntityId;
    public LaplaceId128 RightValueEntityId;
    public ulong SourceOrdinal;
    public ulong ArtifactOrdinal;
    public ulong RowOrdinal;
    public ulong RelationVersion;
    public uint RelationKind;
    public uint Flags;
    public uint LeftDisposition;
    public uint RightDisposition;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceReferenceMappingRecord
{
    public LaplaceReferenceMappingCandidate Candidate;
    public LaplaceDigest256 PropositionId;
    public LaplaceDigest256 OccurrenceId;
    public LaplaceDigest256 MappingId;
    public uint Disposition;
    public uint Reserved;
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

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceEvidenceLineageRecord
{
    public LaplaceDigest256 NodeId;
    public LaplaceId128 PropositionId;
    public LaplaceDigest256 OccurrenceId;
    public LaplaceDigest256 SourceId;
    public LaplaceDigest256 ContextId;
    public LaplaceDigest256 ParentNodeId;
    public ulong SourceOrdinal;
    public uint RecordKind;
    public uint EpistemicKind;
    public uint Flags;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceEvidenceRootRecord
{
    public LaplaceDigest256 NodeId;
    public LaplaceDigest256 RootNodeId;
    public LaplaceId128 PropositionId;
    public ulong PathDepth;
    public uint RootEpistemicKind;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceEvidenceTestimonyRecord
{
    public LaplaceDigest256 TestimonyId;
    public LaplaceDigest256 EvidenceNodeId;
    public LaplaceDigest256 SourceProfileId;
    public LaplaceDigest256 RecipeReceiptId;
    public LaplaceDigest256 TrustInputId;
    public LaplaceDigest256 OutcomeDetailId;
    public ulong UncertaintyNumerator;
    public ulong UncertaintyDenominator;
    public ulong SampleCount;
    public uint SourceType;
    public uint OutcomeType;
    public uint Disposition;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceEvidenceTestimonyReceipt
{
    public LaplaceDigest256 ReceiptId;
    public LaplaceDigest256 SourceProfileId;
    public LaplaceDigest256 InputFingerprint;
    public LaplaceDigest256 OutputFingerprint;
    public ulong TestimonyCount;
    public ulong SampleCount;
    public ulong UncertainCount;
    public ulong NegativeDispositionCount;
    public uint Version;
    public uint Status;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct LaplaceStandingRecipe
{
    public LaplaceDigest256 RecipeId;
    public LaplaceDigest256 AuthorityReceiptId;
    public LaplaceDigest256 EvaluationLawId;
    public LaplaceDigest256 WorldContextId;
    public LaplaceDigest256 LanguageModalityId;
    public LaplaceDigest256 ValidTimeScopeId;
    public LaplaceDigest256 EvidenceBoundaryId;
    public double DefaultRating;
    public double DefaultRatingDeviation;
    public double DefaultVolatility;
    public double VolatilityConstraint;
    public double ConvergenceTolerance;
    public fixed ulong ScoreNumerator[9];
    public fixed ulong ScoreDenominator[9];
    public uint RateableOutcomeMask;
    public uint ParticipantRole;
    public uint ArenaKind;
    public uint Version;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceStandingState
{
    public LaplaceDigest256 StateId;
    public LaplaceDigest256 CoordinateId;
    public LaplaceDigest256 ArenaScopeId;
    public LaplaceDigest256 PriorStateId;
    public LaplaceDigest256 EpochId;
    public LaplaceDigest256 RatingRecipeId;
    public double Rating;
    public double RatingDeviation;
    public double Volatility;
    public ulong EligibleMatchCount;
    public ulong PeriodOrdinal;
    public uint RatingRecipeVersion;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceStandingEvent
{
    public LaplaceDigest256 EventId;
    public LaplaceDigest256 ParticipantCoordinateId;
    public LaplaceDigest256 ParticipantPriorStateId;
    public LaplaceStandingState OpponentPriorState;
    public LaplaceDigest256 PeriodId;
    public LaplaceDigest256 EligibleRootId;
    public LaplaceDigest256 OutcomeMappingId;
    public LaplaceDigest256 ContextId;
    public LaplaceDigest256 ValidTimeId;
    public ulong ScoreNumerator;
    public ulong ScoreDenominator;
    public uint OutcomeKind;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceStandingPeriodReceipt
{
    public LaplaceDigest256 ReceiptId;
    public LaplaceDigest256 PriorStateId;
    public LaplaceDigest256 SuccessorStateId;
    public LaplaceDigest256 PeriodId;
    public LaplaceDigest256 InputFingerprint;
    public LaplaceDigest256 OutputFingerprint;
    public ulong EligibleEventCount;
    public ulong PriorMatchCount;
    public ulong SuccessorMatchCount;
    public uint VolatilityIterations;
    public uint Version;
    public uint Status;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceStandingPeriodInput
{
    public LaplaceStandingRecipe Recipe;
    public LaplaceStandingState PriorState;
    public LaplaceStandingEvent Event;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceStandingPeriodResult
{
    public LaplaceStandingState SuccessorState;
    public LaplaceStandingPeriodReceipt Receipt;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceSourceProfileManifest
{
    public LaplaceDigest256 ProfileId;
    public LaplaceHighwayKey Coordinate;
    public LaplaceDigest256 AuthorityReleaseFingerprint;
    public LaplaceDigest256 LicenseFingerprint;
    public LaplaceDigest256 ArtifactGraphFingerprint;
    public LaplaceDigest256 SyntaxAuthorityFingerprint;
    public LaplaceDigest256 RecipeProgramFingerprint;
    public LaplaceDigest256 UniversalAstMappingFingerprint;
    public LaplaceDigest256 HighwayReferencesFingerprint;
    public LaplaceDigest256 EpistemicWitnessingFingerprint;
    public LaplaceDigest256 DenominatorDeclarationFingerprint;
    public LaplaceDigest256 ConformanceFingerprint;
    public LaplaceDigest256 CompletionLawFingerprint;
    public LaplaceDigest256 SelectedBoundaryFingerprint;
    public ulong ByteCount;
    public ulong ContainerCount;
    public ulong MemberCount;
    public ulong FileCount;
    public ulong RecordCount;
    public ulong FieldCount;
    public ulong SyntaxNodeCount;
    public ulong SpanCount;
    public ulong EdgeCount;
    public ulong ReferenceCount;
    public ulong OccurrenceCount;
    public ulong ClaimCount;
    public ulong MappingCount;
    public ulong ErrorCount;
    public ulong UnknownCount;
    public ulong TransformationCount;
    public ulong OutputCount;
    public ulong ClosureSubjectCount;
    public ulong AcceptedCount;
    public ulong RejectedCount;
    public ulong DuplicateCount;
    public ulong ReusedCount;
    public ulong TransformedCount;
    public ulong LossyCount;
    public ulong UnsupportedCount;
    public ulong MalformedCount;
    public ulong UnresolvedCount;
    public ulong PersistedCount;
    public ulong DerivedCount;
    public ulong NotApplicableMask;
    public uint ReconstructionClass;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceSourceProfileReceipt
{
    public LaplaceDigest256 ReceiptId;
    public LaplaceDigest256 SelectedBoundaryFingerprint;
    public LaplaceDigest256 InputFingerprint;
    public LaplaceDigest256 OutputFingerprint;
    public ulong ProfileCount;
    public ulong ClosureSubjectCount;
    public ulong PersistedCount;
    public ulong NegativeCount;
    public ulong ExactReconstructionCount;
    public ulong SemanticReconstructionCount;
    public ulong NoReconstructionCount;
    public uint Version;
    public uint Status;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceWorldAdmissionRecord
{
    public LaplaceDigest256 AdmissionId;
    public LaplaceDigest256 SourceProfileId;
    public LaplaceDigest256 SelectedBoundaryFingerprint;
    public LaplaceDigest256 SourceProfileReceiptId;
    public LaplaceDigest256 RecipeReceiptId;
    public LaplaceDigest256 CompositionWorkingSetReceiptId;
    public LaplaceDigest256 CompositionPresenceReceiptId;
    public LaplaceDigest256 CompositionProducerReceiptId;
    public LaplaceDigest256 CompositionStreamReceiptId;
    public LaplaceDigest256 EvidenceLineageReceiptId;
    public LaplaceDigest256 EvidenceTestimonyReceiptId;
    public LaplaceDigest256 ReadbackFingerprint;
    public ulong ProfileOccurrenceCount;
    public ulong CompositionOccurrenceCount;
    public ulong ProfileClaimCount;
    public ulong EvidenceNodeCount;
    public ulong TestimonyCount;
    public ulong ProfileBoundTestimonyCount;
    public ulong RecipeBoundTestimonyCount;
    public ulong LineageBoundTestimonyCount;
    public ulong ClosureSubjectCount;
    public ulong ClosedSubjectCount;
    public uint ReconstructionClass;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
public struct LaplaceWorldAdmissionReceipt
{
    public LaplaceDigest256 ReceiptId;
    public LaplaceDigest256 SelectedBoundaryFingerprint;
    public LaplaceDigest256 InputFingerprint;
    public LaplaceDigest256 OutputFingerprint;
    public ulong AdmissionCount;
    public ulong OccurrenceCount;
    public ulong ClaimCount;
    public ulong EvidenceNodeCount;
    public ulong TestimonyCount;
    public ulong ClosureSubjectCount;
    public uint Version;
    public uint Status;
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
    DependenceCycle = 15,
    ResourceInsufficient = 16,
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
        checked((uint)Marshal.SizeOf<LaplaceEvidenceTestimonyRecord>()),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.TestimonyId)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.EvidenceNodeId)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.SourceProfileId)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.RecipeReceiptId)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.TrustInputId)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.OutcomeDetailId)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.UncertaintyNumerator)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.UncertaintyDenominator)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.SampleCount)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.SourceType)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.OutcomeType)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.Disposition)),
        OffsetOf<LaplaceEvidenceTestimonyRecord>(nameof(LaplaceEvidenceTestimonyRecord.Flags)),
        checked((uint)Marshal.SizeOf<LaplaceEvidenceTestimonyReceipt>()),
        OffsetOf<LaplaceEvidenceTestimonyReceipt>(nameof(LaplaceEvidenceTestimonyReceipt.ReceiptId)),
        OffsetOf<LaplaceEvidenceTestimonyReceipt>(nameof(LaplaceEvidenceTestimonyReceipt.SourceProfileId)),
        OffsetOf<LaplaceEvidenceTestimonyReceipt>(nameof(LaplaceEvidenceTestimonyReceipt.InputFingerprint)),
        OffsetOf<LaplaceEvidenceTestimonyReceipt>(nameof(LaplaceEvidenceTestimonyReceipt.OutputFingerprint)),
        OffsetOf<LaplaceEvidenceTestimonyReceipt>(nameof(LaplaceEvidenceTestimonyReceipt.TestimonyCount)),
        OffsetOf<LaplaceEvidenceTestimonyReceipt>(nameof(LaplaceEvidenceTestimonyReceipt.SampleCount)),
        OffsetOf<LaplaceEvidenceTestimonyReceipt>(nameof(LaplaceEvidenceTestimonyReceipt.UncertainCount)),
        OffsetOf<LaplaceEvidenceTestimonyReceipt>(nameof(LaplaceEvidenceTestimonyReceipt.NegativeDispositionCount)),
        OffsetOf<LaplaceEvidenceTestimonyReceipt>(nameof(LaplaceEvidenceTestimonyReceipt.Version)),
        OffsetOf<LaplaceEvidenceTestimonyReceipt>(nameof(LaplaceEvidenceTestimonyReceipt.Status)),
        checked((uint)Marshal.SizeOf<LaplaceStandingRecipe>()),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.RecipeId)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.AuthorityReceiptId)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.EvaluationLawId)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.WorldContextId)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.LanguageModalityId)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.ValidTimeScopeId)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.EvidenceBoundaryId)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.DefaultRating)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.DefaultRatingDeviation)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.DefaultVolatility)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.VolatilityConstraint)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.ConvergenceTolerance)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.ScoreNumerator)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.ScoreDenominator)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.RateableOutcomeMask)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.ParticipantRole)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.ArenaKind)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.Version)),
        OffsetOf<LaplaceStandingRecipe>(nameof(LaplaceStandingRecipe.Flags)),
        checked((uint)Marshal.SizeOf<LaplaceStandingState>()),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.StateId)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.CoordinateId)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.ArenaScopeId)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.PriorStateId)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.EpochId)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.RatingRecipeId)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.Rating)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.RatingDeviation)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.Volatility)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.EligibleMatchCount)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.PeriodOrdinal)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.RatingRecipeVersion)),
        OffsetOf<LaplaceStandingState>(nameof(LaplaceStandingState.Flags)),
        checked((uint)Marshal.SizeOf<LaplaceStandingEvent>()),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.EventId)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.ParticipantCoordinateId)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.ParticipantPriorStateId)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.OpponentPriorState)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.PeriodId)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.EligibleRootId)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.OutcomeMappingId)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.ContextId)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.ValidTimeId)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.ScoreNumerator)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.ScoreDenominator)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.OutcomeKind)),
        OffsetOf<LaplaceStandingEvent>(nameof(LaplaceStandingEvent.Flags)),
        checked((uint)Marshal.SizeOf<LaplaceStandingPeriodReceipt>()),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.ReceiptId)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.PriorStateId)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.SuccessorStateId)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.PeriodId)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.InputFingerprint)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.OutputFingerprint)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.EligibleEventCount)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.PriorMatchCount)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.SuccessorMatchCount)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.VolatilityIterations)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.Version)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.Status)),
        OffsetOf<LaplaceStandingPeriodReceipt>(nameof(LaplaceStandingPeriodReceipt.Flags)),
        checked((uint)Marshal.SizeOf<LaplaceStandingPeriodInput>()),
        OffsetOf<LaplaceStandingPeriodInput>(nameof(LaplaceStandingPeriodInput.Recipe)),
        OffsetOf<LaplaceStandingPeriodInput>(nameof(LaplaceStandingPeriodInput.PriorState)),
        OffsetOf<LaplaceStandingPeriodInput>(nameof(LaplaceStandingPeriodInput.Event)),
        checked((uint)Marshal.SizeOf<LaplaceStandingPeriodResult>()),
        OffsetOf<LaplaceStandingPeriodResult>(nameof(LaplaceStandingPeriodResult.SuccessorState)),
        OffsetOf<LaplaceStandingPeriodResult>(nameof(LaplaceStandingPeriodResult.Receipt)),
        checked((uint)Marshal.SizeOf<LaplaceSourceProfileManifest>()),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.ProfileId)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.Coordinate)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.AuthorityReleaseFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.LicenseFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.ArtifactGraphFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.SyntaxAuthorityFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.RecipeProgramFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.UniversalAstMappingFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.HighwayReferencesFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.EpistemicWitnessingFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.DenominatorDeclarationFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.ConformanceFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.CompletionLawFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.SelectedBoundaryFingerprint)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.ByteCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.ContainerCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.MemberCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.FileCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.RecordCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.FieldCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.SyntaxNodeCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.SpanCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.EdgeCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.ReferenceCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.OccurrenceCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.ClaimCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.MappingCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.ErrorCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.UnknownCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.TransformationCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.OutputCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.ClosureSubjectCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.AcceptedCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.RejectedCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.DuplicateCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.ReusedCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.TransformedCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.LossyCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.UnsupportedCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.MalformedCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.UnresolvedCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.PersistedCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.DerivedCount)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.NotApplicableMask)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.ReconstructionClass)),
        OffsetOf<LaplaceSourceProfileManifest>(nameof(LaplaceSourceProfileManifest.Flags)),
        checked((uint)Marshal.SizeOf<LaplaceSourceProfileReceipt>()),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.ReceiptId)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.SelectedBoundaryFingerprint)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.InputFingerprint)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.OutputFingerprint)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.ProfileCount)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.ClosureSubjectCount)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.PersistedCount)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.NegativeCount)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.ExactReconstructionCount)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.SemanticReconstructionCount)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.NoReconstructionCount)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.Version)),
        OffsetOf<LaplaceSourceProfileReceipt>(nameof(LaplaceSourceProfileReceipt.Status)),
        checked((uint)Marshal.SizeOf<LaplaceWorldAdmissionRecord>()),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.AdmissionId)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.SourceProfileId)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.SelectedBoundaryFingerprint)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.SourceProfileReceiptId)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.RecipeReceiptId)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.CompositionWorkingSetReceiptId)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.CompositionPresenceReceiptId)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.CompositionProducerReceiptId)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.CompositionStreamReceiptId)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.EvidenceLineageReceiptId)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.EvidenceTestimonyReceiptId)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.ReadbackFingerprint)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.ProfileOccurrenceCount)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.CompositionOccurrenceCount)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.ProfileClaimCount)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.EvidenceNodeCount)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.TestimonyCount)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.ProfileBoundTestimonyCount)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.RecipeBoundTestimonyCount)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.LineageBoundTestimonyCount)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.ClosureSubjectCount)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.ClosedSubjectCount)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.ReconstructionClass)),
        OffsetOf<LaplaceWorldAdmissionRecord>(nameof(LaplaceWorldAdmissionRecord.Flags)),
        checked((uint)Marshal.SizeOf<LaplaceWorldAdmissionReceipt>()),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.ReceiptId)),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.SelectedBoundaryFingerprint)),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.InputFingerprint)),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.OutputFingerprint)),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.AdmissionCount)),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.OccurrenceCount)),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.ClaimCount)),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.EvidenceNodeCount)),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.TestimonyCount)),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.ClosureSubjectCount)),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.Version)),
        OffsetOf<LaplaceWorldAdmissionReceipt>(nameof(LaplaceWorldAdmissionReceipt.Status)),
        checked((uint)Marshal.SizeOf<LaplaceReferenceCandidate>()),
        OffsetOf<LaplaceReferenceCandidate>(nameof(LaplaceReferenceCandidate.SourceProfileId)),
        OffsetOf<LaplaceReferenceCandidate>(nameof(LaplaceReferenceCandidate.Key)),
        OffsetOf<LaplaceReferenceCandidate>(nameof(LaplaceReferenceCandidate.RowEntityId)),
        OffsetOf<LaplaceReferenceCandidate>(nameof(LaplaceReferenceCandidate.FieldEntityId)),
        OffsetOf<LaplaceReferenceCandidate>(nameof(LaplaceReferenceCandidate.ValueEntityId)),
        OffsetOf<LaplaceReferenceCandidate>(nameof(LaplaceReferenceCandidate.SourceOrdinal)),
        OffsetOf<LaplaceReferenceCandidate>(nameof(LaplaceReferenceCandidate.ArtifactOrdinal)),
        OffsetOf<LaplaceReferenceCandidate>(nameof(LaplaceReferenceCandidate.RowOrdinal)),
        OffsetOf<LaplaceReferenceCandidate>(nameof(LaplaceReferenceCandidate.ColumnOrdinal)),
        OffsetOf<LaplaceReferenceCandidate>(nameof(LaplaceReferenceCandidate.RuleFlags)),
        OffsetOf<LaplaceReferenceCandidate>(nameof(LaplaceReferenceCandidate.Reserved)),
        checked((uint)Marshal.SizeOf<LaplaceReferenceRecord>()),
        OffsetOf<LaplaceReferenceRecord>(nameof(LaplaceReferenceRecord.Candidate)),
        OffsetOf<LaplaceReferenceRecord>(nameof(LaplaceReferenceRecord.Coordinate)),
        OffsetOf<LaplaceReferenceRecord>(nameof(LaplaceReferenceRecord.OccurrenceId)),
        OffsetOf<LaplaceReferenceRecord>(nameof(LaplaceReferenceRecord.ReferenceId)),
        OffsetOf<LaplaceReferenceRecord>(nameof(LaplaceReferenceRecord.Disposition)),
        OffsetOf<LaplaceReferenceRecord>(nameof(LaplaceReferenceRecord.Reserved)),
        checked((uint)Marshal.SizeOf<LaplaceReferenceMappingCandidate>()),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.BoundaryId)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.SourceProfileId)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.LeftReferenceId)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.RightReferenceId)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.LeftCoordinate)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.RightCoordinate)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.RelationId)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.RowEntityId)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.LeftFieldEntityId)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.LeftValueEntityId)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.RightFieldEntityId)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.RightValueEntityId)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.SourceOrdinal)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.ArtifactOrdinal)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.RowOrdinal)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.RelationVersion)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.RelationKind)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.Flags)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.LeftDisposition)),
        OffsetOf<LaplaceReferenceMappingCandidate>(nameof(LaplaceReferenceMappingCandidate.RightDisposition)),
        checked((uint)Marshal.SizeOf<LaplaceReferenceMappingRecord>()),
        OffsetOf<LaplaceReferenceMappingRecord>(nameof(LaplaceReferenceMappingRecord.Candidate)),
        OffsetOf<LaplaceReferenceMappingRecord>(nameof(LaplaceReferenceMappingRecord.PropositionId)),
        OffsetOf<LaplaceReferenceMappingRecord>(nameof(LaplaceReferenceMappingRecord.OccurrenceId)),
        OffsetOf<LaplaceReferenceMappingRecord>(nameof(LaplaceReferenceMappingRecord.MappingId)),
        OffsetOf<LaplaceReferenceMappingRecord>(nameof(LaplaceReferenceMappingRecord.Disposition)),
        OffsetOf<LaplaceReferenceMappingRecord>(nameof(LaplaceReferenceMappingRecord.Reserved)),
        LaplaceExecutionContext.NativeAbiSize,
    ];

    private static uint OffsetOf<T>(string field) where T : struct =>
        checked((uint)Marshal.OffsetOf<T>(field).ToInt64());
}
