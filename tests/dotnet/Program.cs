using System.Runtime.InteropServices;
using Laplace.Managed;

internal static class Program
{
    private static int Main(string[] args)
    {
        try
        {
            Require(args.Length == 1, "expected direct-native fixture path");
            Fixture fixture = Fixture.Read(args[0]);
            VerifyGeneratedContract();
            VerifyAbi(fixture);
            VerifyNativeParity(fixture);
            VerifyScalarLowering(fixture);
            VerifyPreflightRejection(fixture);
            VerifyTransportLifetime(fixture);
            Console.WriteLine("managed ISA transport parity verified");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    private static void VerifyGeneratedContract()
    {
        Require(LaplaceIsaContract.Major == 1,
            "generated ISA major version differs");
        Require(LaplaceIsaContract.ReceiptDigestAlgorithm == "BLAKE3-256" &&
            LaplaceIsaContract.ReceiptDigestBytes == 32,
            "generated receipt descriptor differs");
        Require(LaplaceIsaContract.Minor == 11,
            "generated ISA minor version differs");
        Require(LaplaceIsaContract.ValueTypes.Length == 19,
            "generated value type inventory differs");
        Require(LaplaceIsaContract.Operations.Length == 11,
            "generated operation inventory differs");
        Require(IdentityCodepointBatch.Descriptor == LaplaceIsaContract.Operations[0],
            "generated identity declaration differs from descriptor inventory");
        Require(TrajectoryCompositionDecodeBatch.Descriptor == LaplaceIsaContract.Operations[1],
            "generated trajectory declaration differs from descriptor inventory");
        Require(HighwayCoordinateCalculateBatch.Descriptor == LaplaceIsaContract.Operations[2],
            "generated highway declaration differs from descriptor inventory");
        Require(HighwayRegistryMaterializeBatch.Descriptor == LaplaceIsaContract.Operations[3],
            "generated highway registry declaration differs from descriptor inventory");
        Require(EvidenceRecordLineageBatch.Descriptor == LaplaceIsaContract.Operations[4],
            "generated evidence lineage declaration differs from descriptor inventory");
        Require(EvidenceRecordTestimonyBatch.Descriptor == LaplaceIsaContract.Operations[5],
            "generated evidence testimony declaration differs from descriptor inventory");
        Require(SourceProfileValidateBatch.Descriptor == LaplaceIsaContract.Operations[6],
            "generated source-profile declaration differs from descriptor inventory");
        Require(WorldAdmissionCloseBatch.Descriptor == LaplaceIsaContract.Operations[7],
            "generated world-admission declaration differs from descriptor inventory");
        Require(ReferenceTopologyResolveBatch.Descriptor == LaplaceIsaContract.Operations[8],
            "generated reference-topology declaration differs from descriptor inventory");
        Require(ReferenceMappingResolveBatch.Descriptor == LaplaceIsaContract.Operations[9],
            "generated reference-mapping declaration differs from descriptor inventory");
        Require(CognitionSolvePacket.Descriptor == LaplaceIsaContract.Operations[10],
            "generated cognition declaration differs from descriptor inventory");
        Require(LaplaceHighwayContract.Version == 2U &&
            LaplaceHighwayContract.KindLanguage == 3U &&
            LaplaceHighwayContract.KindOperation == 14U &&
            LaplaceHighwayContract.KindSourceProfile == 17U,
            "generated highway registry mirror differs");
    }

    private static void VerifyAbi(Fixture fixture)
    {
        uint[] managed = LaplaceNativeAbi.Measure();
        Require(managed.AsSpan().SequenceEqual(fixture.Layout),
            "managed/native ABI size or offset differs");
    }

    private static void VerifyNativeParity(Fixture fixture)
    {
        var context = new LaplaceExecutionContext(fixture.Context);
        using var transport = new NativeIsaTransport();
        var client = new LaplaceIsaClient(transport);

        var identity = client.ExecuteBatch<IdentityCodepointBatch, uint, LaplaceId128>(
            fixture.Positions,
            context);
        Require(identity.Status == LaplaceIsaStatus.Ok, "managed identity execution failed");
        Require(identity.OutputCount == (ulong)fixture.Identities.Length,
            "managed identity output count differs");
        Require(RawEqual(identity.Output, fixture.Identities),
            "managed identity output differs from direct native output");
        Require(RawEqual(identity.Receipt, fixture.IdentityReceipt),
            "managed identity receipt differs from direct native receipt");
        Require(RawEqual(identity.Error, fixture.IdentityError),
            "managed identity error fields differ from direct native result");

        var trajectory = client.ExecuteBatch<
            TrajectoryCompositionDecodeBatch,
            LaplaceTrajectoryCarrier,
            LaplaceCompositionOccurrence>(fixture.Carriers, context);
        Require(trajectory.Status == LaplaceIsaStatus.Ok,
            "managed trajectory execution failed");
        Require(trajectory.OutputCount == (ulong)fixture.Occurrences.Length,
            "managed trajectory output count differs");
        Require(RawEqual(trajectory.Output, fixture.Occurrences),
            "managed trajectory output differs from direct native output");
        Require(RawEqual(trajectory.Receipt, fixture.TrajectoryReceipt),
            "managed trajectory receipt differs from direct native receipt");
        Require(RawEqual(trajectory.Error, fixture.TrajectoryError),
            "managed trajectory error fields differ from direct native result");

        var highway = client.ExecuteBatch<
            HighwayCoordinateCalculateBatch,
            LaplaceHighwayKey,
            LaplaceHighwayCoordinate>(fixture.HighwayKeys, context);
        Require(highway.Status == LaplaceIsaStatus.Ok,
            "managed highway execution failed");
        Require(highway.OutputCount == (ulong)fixture.HighwayCoordinates.Length,
            "managed highway output count differs");
        Require(RawEqual(highway.Output, fixture.HighwayCoordinates),
            "managed highway output differs from direct native output");
        Require(RawEqual(highway.Receipt, fixture.HighwayReceipt),
            "managed highway receipt differs from direct native receipt");
        Require(RawEqual(highway.Error, fixture.HighwayError),
            "managed highway error fields differ from direct native result");

        var highwayRegistry = client.ExecuteBatch<
            HighwayRegistryMaterializeBatch,
            uint,
            LaplaceHighwayRegistryReceipt>(fixture.HighwayRegistryVersions, context);
        Require(highwayRegistry.Status == LaplaceIsaStatus.Ok,
            "managed highway registry execution failed");
        Require(highwayRegistry.OutputCount ==
            (ulong)fixture.HighwayRegistryOutputs.Length,
            "managed highway registry output count differs");
        Require(RawEqual(highwayRegistry.Output, fixture.HighwayRegistryOutputs),
            "managed highway registry output differs from direct native output");
        Require(RawEqual(highwayRegistry.Receipt, fixture.HighwayRegistryReceipt),
            "managed highway registry ISA receipt differs from direct native receipt");
        Require(RawEqual(highwayRegistry.Error, fixture.HighwayRegistryError),
            "managed highway registry error fields differ from direct native result");

        var testimony = client.ExecuteBatch<
            EvidenceRecordTestimonyBatch,
            LaplaceEvidenceTestimonyRecord,
            LaplaceEvidenceTestimonyReceipt>(fixture.TestimonyRecords, context);
        Require(testimony.Status == LaplaceIsaStatus.Ok,
            "managed evidence testimony execution failed");
        Require(testimony.OutputCount == (ulong)fixture.TestimonyOutputs.Length,
            "managed evidence testimony output count differs");
        LaplaceEvidenceTestimonyReceipt[] publishedTestimony =
            testimony.Output.AsSpan(0, checked((int)testimony.OutputCount)).ToArray();
        Require(RawEqual(publishedTestimony, fixture.TestimonyOutputs),
            $"managed evidence testimony output differs from direct native output: " +
            $"actual={RawHex(publishedTestimony)} expected={RawHex(fixture.TestimonyOutputs)}");
        Require(RawEqual(testimony.Receipt, fixture.TestimonyReceipt),
            "managed evidence testimony ISA receipt differs from direct native receipt");
        Require(RawEqual(testimony.Error, fixture.TestimonyError),
            "managed evidence testimony error fields differ from direct native result");

        var sourceProfile = client.ExecuteBatch<
            SourceProfileValidateBatch,
            LaplaceSourceProfileManifest,
            LaplaceSourceProfileReceipt>(fixture.SourceProfiles, context);
        Require(sourceProfile.Status == LaplaceIsaStatus.Ok,
            "managed source-profile execution failed");
        Require(sourceProfile.OutputCount == (ulong)fixture.SourceProfileOutputs.Length,
            "managed source-profile output count differs");
        LaplaceSourceProfileReceipt[] publishedSourceProfiles =
            sourceProfile.Output.AsSpan(0, checked((int)sourceProfile.OutputCount)).ToArray();
        Require(RawEqual(publishedSourceProfiles, fixture.SourceProfileOutputs),
            "managed source-profile output differs from direct native output");
        Require(RawEqual(sourceProfile.Receipt, fixture.SourceProfileReceipt),
            "managed source-profile ISA receipt differs from direct native receipt");
        Require(RawEqual(sourceProfile.Error, fixture.SourceProfileError),
            "managed source-profile error fields differ from direct native result");

        var worldAdmission = client.ExecuteBatch<
            WorldAdmissionCloseBatch,
            LaplaceWorldAdmissionRecord,
            LaplaceWorldAdmissionReceipt>(fixture.WorldAdmissions, context);
        Require(worldAdmission.Status == LaplaceIsaStatus.Ok,
            "managed world-admission execution failed");
        Require(worldAdmission.OutputCount == (ulong)fixture.WorldAdmissionOutputs.Length,
            "managed world-admission output count differs");
        LaplaceWorldAdmissionReceipt[] publishedWorldAdmissions =
            worldAdmission.Output.AsSpan(
                0, checked((int)worldAdmission.OutputCount)).ToArray();
        Require(RawEqual(publishedWorldAdmissions, fixture.WorldAdmissionOutputs),
            "managed world-admission output differs from direct native output");
        Require(RawEqual(worldAdmission.Receipt, fixture.WorldAdmissionReceipt),
            "managed world-admission ISA receipt differs from direct native receipt");
        Require(RawEqual(worldAdmission.Error, fixture.WorldAdmissionError),
            "managed world-admission error fields differ from direct native result");

        var referenceTopology = client.ExecuteBatch<
            ReferenceTopologyResolveBatch,
            LaplaceReferenceCandidate,
            LaplaceReferenceRecord>(fixture.ReferenceCandidates, context);
        Require(referenceTopology.Status == LaplaceIsaStatus.Ok,
            "managed reference-topology execution failed");
        Require(referenceTopology.OutputCount == (ulong)fixture.ReferenceRecords.Length,
            "managed reference-topology output count differs");
        LaplaceReferenceRecord[] publishedReferences =
            referenceTopology.Output.AsSpan(
                0, checked((int)referenceTopology.OutputCount)).ToArray();
        Require(RawEqual(publishedReferences, fixture.ReferenceRecords),
            "managed reference-topology output differs from direct native output");
        Require(RawEqual(referenceTopology.Receipt, fixture.ReferenceReceipt),
            "managed reference-topology ISA receipt differs from direct native receipt");
        Require(RawEqual(referenceTopology.Error, fixture.ReferenceError),
            "managed reference-topology error fields differ from direct native result");

        var referenceMapping = client.ExecuteBatch<
            ReferenceMappingResolveBatch,
            LaplaceReferenceMappingCandidate,
            LaplaceReferenceMappingRecord>(fixture.ReferenceMappingCandidates, context);
        Require(referenceMapping.Status == LaplaceIsaStatus.Ok,
            "managed reference-mapping execution failed");
        Require(referenceMapping.OutputCount ==
            (ulong)fixture.ReferenceMappingRecords.Length,
            "managed reference-mapping output count differs");
        LaplaceReferenceMappingRecord[] publishedMappings =
            referenceMapping.Output.AsSpan(
                0, checked((int)referenceMapping.OutputCount)).ToArray();
        Require(RawEqual(publishedMappings, fixture.ReferenceMappingRecords),
            "managed reference-mapping output differs from direct native output");
        Require(RawEqual(referenceMapping.Receipt, fixture.ReferenceMappingReceipt),
            "managed reference-mapping ISA receipt differs from direct native receipt");
        Require(RawEqual(referenceMapping.Error, fixture.ReferenceMappingError),
            "managed reference-mapping error fields differ from direct native result");
    }

    private static void VerifyScalarLowering(Fixture fixture)
    {
        var context = new LaplaceExecutionContext(fixture.Context);
        using var transport = new NativeIsaTransport();
        var client = new LaplaceIsaClient(transport);
        uint input = fixture.Positions[1];
        uint[] oneElementVector = [input];

        var vector = client.ExecuteBatch<IdentityCodepointBatch, uint, LaplaceId128>(
            oneElementVector,
            context);
        var scalar = client.ExecuteScalar<IdentityCodepointBatch, uint, LaplaceId128>(
            input,
            context);
        Require(vector.Status == LaplaceIsaStatus.Ok && scalar.Status == LaplaceIsaStatus.Ok,
            "one-element execution failed");
        Require(RawEqual(vector.Output, scalar.Output),
            "scalar output differs from one-element vector execution");
        Require(RawEqual(vector.Receipt, scalar.Receipt),
            "scalar receipt proves a different execution path");
    }

    private static void VerifyPreflightRejection(Fixture fixture)
    {
        var context = new LaplaceExecutionContext(fixture.Context);
        using var transport = new NativeIsaTransport();
        var client = new LaplaceIsaClient(transport);
        uint[] input = [0x41u];

        var version = client.ExecuteBatch<WrongVersion, uint, LaplaceId128>(input, context);
        VerifyRejected(version, LaplaceIsaStatus.UnsupportedInstructionVersion);

        var type = client.ExecuteBatch<WrongOutputType, uint, LaplaceCompositionOccurrence>(
            input,
            context);
        VerifyRejected(type, LaplaceIsaStatus.ValueTypeMismatch);

        var opcode = client.ExecuteBatch<UnknownOperation, uint, LaplaceId128>(input, context);
        VerifyRejected(opcode, LaplaceIsaStatus.UnknownOpcode);
    }

    private static void VerifyTransportLifetime(Fixture fixture)
    {
        var context = new LaplaceExecutionContext(fixture.Context);
        using var transport = new NativeIsaTransport();
        var client = new LaplaceIsaClient(transport);
        for (int iteration = 0; iteration < 32; ++iteration)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            var result = client.ExecuteScalar<IdentityCodepointBatch, uint, LaplaceId128>(
                fixture.Positions[iteration % fixture.Positions.Length],
                context);
            Require(result.Status == LaplaceIsaStatus.Ok,
                "call-scoped pin lifetime failed under relocation pressure");
        }
        transport.Dispose();
        bool rejected = false;
        try
        {
            _ = client.ExecuteScalar<IdentityCodepointBatch, uint, LaplaceId128>(
                fixture.Positions[0],
                context);
        }
        catch (ObjectDisposedException)
        {
            rejected = true;
        }
        Require(rejected, "disposed transport accepted another native execution");
    }

    private static void VerifyRejected<TOutput>(
        LaplaceIsaExecution<TOutput> execution,
        LaplaceIsaStatus expected)
        where TOutput : unmanaged
    {
        Require(execution.Status == expected, $"expected {expected}, got {execution.Status}");
        Require(execution.Error.Status == expected, "native error status differs");
        Require(execution.Receipt.Status == expected, "native receipt status differs");
        Require(execution.Receipt.ExecutedInstructionCount == 0,
            "rejected program executed an instruction");
        Require(execution.OutputCount == 0, "rejected program published output");
        Require(MemoryMarshal.AsBytes(execution.Output.AsSpan()).IndexOfAnyExcept((byte)0) < 0,
            "rejected program mutated output capacity");
        Require(RawEqual(execution.Receipt.ReceiptId, default(LaplaceDigest256)),
            "rejected program published a receipt identity");
    }

    private static bool RawEqual<T>(T left, T right) where T : unmanaged
    {
        ReadOnlySpan<T> leftItems = MemoryMarshal.CreateReadOnlySpan(ref left, 1);
        ReadOnlySpan<T> rightItems = MemoryMarshal.CreateReadOnlySpan(ref right, 1);
        return MemoryMarshal.AsBytes(leftItems).SequenceEqual(MemoryMarshal.AsBytes(rightItems));
    }

    private static bool RawEqual<T>(T[] left, T[] right) where T : unmanaged =>
        MemoryMarshal.AsBytes(left.AsSpan()).SequenceEqual(MemoryMarshal.AsBytes(right.AsSpan()));

    private static string RawHex<T>(T[] values) where T : unmanaged =>
        Convert.ToHexString(MemoryMarshal.AsBytes(values.AsSpan()));

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private readonly struct WrongVersion : ILaplaceOperation<uint, LaplaceId128>
    {
        public static LaplaceOperationDescriptor Descriptor =>
            IdentityCodepointBatch.Descriptor with { InstructionVersion = 2 };
    }

    private readonly struct WrongOutputType :
        ILaplaceOperation<uint, LaplaceCompositionOccurrence>
    {
        public static LaplaceOperationDescriptor Descriptor =>
            IdentityCodepointBatch.Descriptor with { OutputType = 4 };
    }

    private readonly struct UnknownOperation : ILaplaceOperation<uint, LaplaceId128>
    {
        public static LaplaceOperationDescriptor Descriptor =>
            IdentityCodepointBatch.Descriptor with { Opcode = uint.MaxValue };
    }
}

internal sealed record Fixture(
    uint[] Layout,
    byte[] Context,
    uint[] Positions,
    LaplaceId128[] Identities,
    LaplaceIsaReceipt IdentityReceipt,
    LaplaceIsaError IdentityError,
    LaplaceTrajectoryCarrier[] Carriers,
    LaplaceCompositionOccurrence[] Occurrences,
    LaplaceIsaReceipt TrajectoryReceipt,
    LaplaceIsaError TrajectoryError,
    LaplaceHighwayKey[] HighwayKeys,
    LaplaceHighwayCoordinate[] HighwayCoordinates,
    LaplaceIsaReceipt HighwayReceipt,
    LaplaceIsaError HighwayError,
    uint[] HighwayRegistryVersions,
    LaplaceHighwayRegistryReceipt[] HighwayRegistryOutputs,
    LaplaceIsaReceipt HighwayRegistryReceipt,
    LaplaceIsaError HighwayRegistryError,
    LaplaceEvidenceTestimonyRecord[] TestimonyRecords,
    LaplaceEvidenceTestimonyReceipt[] TestimonyOutputs,
    LaplaceIsaReceipt TestimonyReceipt,
    LaplaceIsaError TestimonyError,
    LaplaceSourceProfileManifest[] SourceProfiles,
    LaplaceSourceProfileReceipt[] SourceProfileOutputs,
    LaplaceIsaReceipt SourceProfileReceipt,
    LaplaceIsaError SourceProfileError,
    LaplaceWorldAdmissionRecord[] WorldAdmissions,
    LaplaceWorldAdmissionReceipt[] WorldAdmissionOutputs,
    LaplaceIsaReceipt WorldAdmissionReceipt,
    LaplaceIsaError WorldAdmissionError,
    LaplaceReferenceCandidate[] ReferenceCandidates,
    LaplaceReferenceRecord[] ReferenceRecords,
    LaplaceIsaReceipt ReferenceReceipt,
    LaplaceIsaError ReferenceError,
    LaplaceReferenceMappingCandidate[] ReferenceMappingCandidates,
    LaplaceReferenceMappingRecord[] ReferenceMappingRecords,
    LaplaceIsaReceipt ReferenceMappingReceipt,
    LaplaceIsaError ReferenceMappingError)
{
    private static readonly byte[] Magic = [
        0x4c, 0x50, 0x44, 0x4e, 0x45, 0x54, 0x31, 0x00,
    ];

    internal static Fixture Read(string path)
    {
        using var stream = File.OpenRead(path);
        using var input = new BinaryReader(stream);
        if (!input.ReadBytes(Magic.Length).AsSpan().SequenceEqual(Magic))
        {
            throw new InvalidDataException("direct-native fixture magic differs");
        }
        uint version = input.ReadUInt32();
        uint layoutCount = input.ReadUInt32();
        uint identityCount = input.ReadUInt32();
        uint trajectoryCount = input.ReadUInt32();
        uint highwayCount = input.ReadUInt32();
        uint highwayRegistryCount = input.ReadUInt32();
        uint testimonyCount = input.ReadUInt32();
        uint sourceProfileCount = input.ReadUInt32();
        uint worldAdmissionCount = input.ReadUInt32();
        uint referenceCount = input.ReadUInt32();
        uint mappingCount = input.ReadUInt32();
        if (version != 8 || layoutCount > 1024 || identityCount > 1024 ||
            trajectoryCount > 1024 || highwayCount > 1024 ||
            highwayRegistryCount > 1024 || testimonyCount > 1024 ||
            sourceProfileCount > 1024 || worldAdmissionCount > 1024 ||
            referenceCount > 1024 || mappingCount > 1024)
        {
            throw new InvalidDataException("direct-native fixture header is invalid");
        }

        uint[] layout = ReadArray<uint>(input, layoutCount);
        byte[] context = input.ReadBytes(LaplaceExecutionContext.NativeAbiSize);
        if (context.Length != LaplaceExecutionContext.NativeAbiSize)
        {
            throw new EndOfStreamException();
        }
        uint[] positions = ReadArray<uint>(input, identityCount);
        LaplaceId128[] identities = ReadArray<LaplaceId128>(input, identityCount);
        LaplaceIsaReceipt identityReceipt = ReadOne<LaplaceIsaReceipt>(input);
        LaplaceIsaError identityError = ReadOne<LaplaceIsaError>(input);
        LaplaceTrajectoryCarrier[] carriers = ReadArray<LaplaceTrajectoryCarrier>(
            input,
            trajectoryCount);
        LaplaceCompositionOccurrence[] occurrences =
            ReadArray<LaplaceCompositionOccurrence>(input, trajectoryCount);
        LaplaceIsaReceipt trajectoryReceipt = ReadOne<LaplaceIsaReceipt>(input);
        LaplaceIsaError trajectoryError = ReadOne<LaplaceIsaError>(input);
        LaplaceHighwayKey[] highwayKeys = ReadArray<LaplaceHighwayKey>(input, highwayCount);
        LaplaceHighwayCoordinate[] highwayCoordinates =
            ReadArray<LaplaceHighwayCoordinate>(input, highwayCount);
        LaplaceIsaReceipt highwayReceipt = ReadOne<LaplaceIsaReceipt>(input);
        LaplaceIsaError highwayError = ReadOne<LaplaceIsaError>(input);
        uint[] highwayRegistryVersions = ReadArray<uint>(input, highwayRegistryCount);
        LaplaceHighwayRegistryReceipt[] highwayRegistryOutputs =
            ReadArray<LaplaceHighwayRegistryReceipt>(input, highwayRegistryCount);
        LaplaceIsaReceipt highwayRegistryReceipt = ReadOne<LaplaceIsaReceipt>(input);
        LaplaceIsaError highwayRegistryError = ReadOne<LaplaceIsaError>(input);
        LaplaceEvidenceTestimonyRecord[] testimonyRecords =
            ReadArray<LaplaceEvidenceTestimonyRecord>(input, testimonyCount);
        LaplaceEvidenceTestimonyReceipt[] testimonyOutputs =
            ReadArray<LaplaceEvidenceTestimonyReceipt>(input, 1);
        LaplaceIsaReceipt testimonyReceipt = ReadOne<LaplaceIsaReceipt>(input);
        LaplaceIsaError testimonyError = ReadOne<LaplaceIsaError>(input);
        LaplaceSourceProfileManifest[] sourceProfiles =
            ReadArray<LaplaceSourceProfileManifest>(input, sourceProfileCount);
        LaplaceSourceProfileReceipt[] sourceProfileOutputs =
            ReadArray<LaplaceSourceProfileReceipt>(input, 1);
        LaplaceIsaReceipt sourceProfileReceipt = ReadOne<LaplaceIsaReceipt>(input);
        LaplaceIsaError sourceProfileError = ReadOne<LaplaceIsaError>(input);
        LaplaceWorldAdmissionRecord[] worldAdmissions =
            ReadArray<LaplaceWorldAdmissionRecord>(input, worldAdmissionCount);
        LaplaceWorldAdmissionReceipt[] worldAdmissionOutputs =
            ReadArray<LaplaceWorldAdmissionReceipt>(input, 1);
        LaplaceIsaReceipt worldAdmissionReceipt = ReadOne<LaplaceIsaReceipt>(input);
        LaplaceIsaError worldAdmissionError = ReadOne<LaplaceIsaError>(input);
        LaplaceReferenceCandidate[] referenceCandidates =
            ReadArray<LaplaceReferenceCandidate>(input, referenceCount);
        LaplaceReferenceRecord[] referenceRecords =
            ReadArray<LaplaceReferenceRecord>(input, referenceCount);
        LaplaceIsaReceipt referenceReceipt = ReadOne<LaplaceIsaReceipt>(input);
        LaplaceIsaError referenceError = ReadOne<LaplaceIsaError>(input);
        LaplaceReferenceMappingCandidate[] mappingCandidates =
            ReadArray<LaplaceReferenceMappingCandidate>(input, mappingCount);
        LaplaceReferenceMappingRecord[] mappingRecords =
            ReadArray<LaplaceReferenceMappingRecord>(input, mappingCount);
        LaplaceIsaReceipt mappingReceipt = ReadOne<LaplaceIsaReceipt>(input);
        LaplaceIsaError mappingError = ReadOne<LaplaceIsaError>(input);
        if (stream.Position != stream.Length)
        {
            throw new InvalidDataException("direct-native fixture has trailing bytes");
        }
        return new Fixture(
            layout,
            context,
            positions,
            identities,
            identityReceipt,
            identityError,
            carriers,
            occurrences,
            trajectoryReceipt,
            trajectoryError,
            highwayKeys,
            highwayCoordinates,
            highwayReceipt,
            highwayError,
            highwayRegistryVersions,
            highwayRegistryOutputs,
            highwayRegistryReceipt,
            highwayRegistryError,
            testimonyRecords,
            testimonyOutputs,
            testimonyReceipt,
            testimonyError,
            sourceProfiles,
            sourceProfileOutputs,
            sourceProfileReceipt,
            sourceProfileError,
            worldAdmissions,
            worldAdmissionOutputs,
            worldAdmissionReceipt,
            worldAdmissionError,
            referenceCandidates,
            referenceRecords,
            referenceReceipt,
            referenceError,
            mappingCandidates,
            mappingRecords,
            mappingReceipt,
            mappingError);
    }

    private static T ReadOne<T>(BinaryReader input) where T : unmanaged =>
        ReadArray<T>(input, 1)[0];

    private static T[] ReadArray<T>(BinaryReader input, uint count) where T : unmanaged
    {
        int byteCount = checked((int)count * Marshal.SizeOf<T>());
        byte[] bytes = input.ReadBytes(byteCount);
        if (bytes.Length != byteCount)
        {
            throw new EndOfStreamException();
        }
        return MemoryMarshal.Cast<byte, T>(bytes).ToArray();
    }
}
