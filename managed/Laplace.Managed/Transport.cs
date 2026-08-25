using System.Runtime.InteropServices;

namespace Laplace.Managed;

public interface ILaplaceIsaTransport : IDisposable
{
    LaplaceIsaExecution<TOutput> ExecuteBatch<TOperation, TInput, TOutput>(
        ReadOnlySpan<TInput> input,
        LaplaceExecutionContext context)
        where TOperation : struct, ILaplaceOperation<TInput, TOutput>
        where TInput : unmanaged
        where TOutput : unmanaged;
}

public interface ILaplaceIsaClient
{
    LaplaceIsaExecution<TOutput> ExecuteBatch<TOperation, TInput, TOutput>(
        ReadOnlySpan<TInput> input,
        LaplaceExecutionContext context)
        where TOperation : struct, ILaplaceOperation<TInput, TOutput>
        where TInput : unmanaged
        where TOutput : unmanaged;

    LaplaceIsaExecution<TOutput> ExecuteScalar<TOperation, TInput, TOutput>(
        TInput input,
        LaplaceExecutionContext context)
        where TOperation : struct, ILaplaceOperation<TInput, TOutput>
        where TInput : unmanaged
        where TOutput : unmanaged;
}

public abstract class LaplaceIsaClientBase : ILaplaceIsaClient
{
    private readonly ILaplaceIsaTransport transport;

    protected LaplaceIsaClientBase(ILaplaceIsaTransport transport)
    {
        this.transport = transport ?? throw new ArgumentNullException(nameof(transport));
    }

    public LaplaceIsaExecution<TOutput> ExecuteBatch<TOperation, TInput, TOutput>(
        ReadOnlySpan<TInput> input,
        LaplaceExecutionContext context)
        where TOperation : struct, ILaplaceOperation<TInput, TOutput>
        where TInput : unmanaged
        where TOutput : unmanaged =>
        transport.ExecuteBatch<TOperation, TInput, TOutput>(input, context);

    public LaplaceIsaExecution<TOutput> ExecuteScalar<TOperation, TInput, TOutput>(
        TInput input,
        LaplaceExecutionContext context)
        where TOperation : struct, ILaplaceOperation<TInput, TOutput>
        where TInput : unmanaged
        where TOutput : unmanaged
    {
        TInput[] oneElementVector = [input];
        return ExecuteBatch<TOperation, TInput, TOutput>(oneElementVector, context);
    }
}

public sealed class LaplaceIsaClient(ILaplaceIsaTransport transport) :
    LaplaceIsaClientBase(transport);

public sealed unsafe class NativeIsaTransport : ILaplaceIsaTransport
{
    private int disposed;

    [DllImport(
        LaplaceIsaContract.NativeLibrary,
        EntryPoint = LaplaceIsaContract.ExecuteSymbol,
        CallingConvention = CallingConvention.Cdecl,
        ExactSpelling = true)]
    private static extern LaplaceIsaStatus ExecuteNative(
        NativeAbi.Program* program,
        LaplaceIsaReceipt* receipt,
        LaplaceIsaError* error);

    public LaplaceIsaExecution<TOutput> ExecuteBatch<TOperation, TInput, TOutput>(
        ReadOnlySpan<TInput> input,
        LaplaceExecutionContext context)
        where TOperation : struct, ILaplaceOperation<TInput, TOutput>
        where TInput : unmanaged
        where TOutput : unmanaged
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref disposed) != 0, this);
        ArgumentNullException.ThrowIfNull(context);

        LaplaceOperationDescriptor descriptor = TOperation.Descriptor;
        TOutput[] output = new TOutput[input.Length];
        LaplaceIsaReceipt receipt = default;
        LaplaceIsaError error = default;
        LaplaceIsaStatus status;

        fixed (TInput* inputPointer = input)
        fixed (TOutput* outputPointer = output)
        fixed (byte* contextPointer = context.AbiBytes)
        {
            NativeAbi.ValueView* values = stackalloc NativeAbi.ValueView[2];
            values[0] = new NativeAbi.ValueView
            {
                Data = (nint)inputPointer,
                Count = checked((ulong)input.Length),
                Capacity = checked((ulong)input.Length),
                StrideBytes = checked((uint)sizeof(TInput)),
                Type = descriptor.InputType,
                Flags = LaplaceIsaContract.KnownValueFlags,
            };
            values[1] = new NativeAbi.ValueView
            {
                Data = (nint)outputPointer,
                Count = 0,
                Capacity = checked((ulong)output.Length),
                StrideBytes = checked((uint)sizeof(TOutput)),
                Type = descriptor.OutputType,
                Flags = LaplaceIsaContract.KnownValueFlags,
            };

            NativeAbi.Instruction instruction = new()
            {
                Opcode = descriptor.Opcode,
                InputValue = 0,
                OutputValue = 1,
                Version = descriptor.InstructionVersion,
                Flags = LaplaceIsaContract.KnownInstructionFlags,
            };
            NativeAbi.Program program = new()
            {
                Instructions = &instruction,
                Values = values,
                Context = contextPointer,
                InstructionCount = 1,
                ValueCount = 2,
                Major = LaplaceIsaContract.Major,
                Minor = LaplaceIsaContract.Minor,
                Flags = LaplaceIsaContract.KnownProgramFlags,
                ReceiptDetail = LaplaceIsaContract.ReceiptDetailFull,
            };
            status = ExecuteNative(&program, &receipt, &error);
            if (values[1].Count > checked((ulong)output.Length))
            {
                throw new InvalidDataException("Native ISA published an output count beyond capacity.");
            }

            return new LaplaceIsaExecution<TOutput>(
                output,
                values[1].Count,
                status,
                receipt,
                error);
        }
    }

    public void Dispose() => Interlocked.Exchange(ref disposed, 1);
}
