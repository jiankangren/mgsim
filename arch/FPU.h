// -*- c++ -*-
#ifndef FPU_H
#define FPU_H

#include "sim/kernel.h"
#include "sim/buffer.h"
#include "sim/register.h"
#include "sim/inspect.h"
#include "arch/simtypes.h"

#include <deque>
#include <map>

namespace Simulator
{

/**
 * The different kinds of floating point operations that the FPU can perform
 */
enum FPUOperation
{
    FPU_OP_NONE = -1,   ///< Reserved for internal use
    FPU_OP_ADD  =  0,   ///< Addition
    FPU_OP_SUB,         ///< Subtraction
    FPU_OP_MUL,         ///< Multiplication
    FPU_OP_DIV,         ///< Division
    FPU_OP_SQRT,        ///< Square root
    FPU_NUM_OPS         ///< Number of operations
};

/**
 * @brief Floating Point Unit
 *
 * This component accepts floating point operations, executes them asynchronously and writes them
 * back once calculated. It has several pipelines, assuming every operation of equal delay can be pipelined.
 */
class FPU : public Object, public Inspect::Interface<Inspect::Read>
{
    /// Represents an FP operation
    struct Operation;

public:
    /// Represents a client for this FPU
    class IFPUClient
    {
    public:
        virtual const std::string& GetName() const = 0;
        virtual bool CheckFPUOutputAvailability(RegAddr addr) = 0;
        virtual bool WriteFPUResult(RegAddr addr, const RegValue& value) = 0;
        virtual ~IFPUClient();
    };

private:

    /// Represents a source for this FPU
    class Source;

    /// Represents the result of an FP operation
    struct Result
    {
        RegAddr       address;     ///< Address of destination register of result.
        MultiFloat    value;       ///< Resulting value of the operation.
        unsigned int  source;      ///< The source of the operation
        unsigned int  size;        ///< Size of the resulting value.
        unsigned int  state;       ///< Progression through the pipeline.
        unsigned int  index;       ///< Current index of writeback.

        SERIALIZE(a) { a & "fpr" & address & source & state & index & Serialization::multifloat(value, size); }
    };

    /// Represents a pipeline for an FP operation type
    struct Unit
    {
        CycleNo            latency;     ///< The latency of the unit/pipeline
        std::deque<Result> slots;       ///< The pipeline slots
        bool               pipelined;   ///< Is it a pipeline or a single ex. unit?

        Unit() : latency(0), slots(), pipelined(false) {}
        SERIALIZE(a) { a & slots; }
    };

    /**
     * Called when an operation has completed.
     * @param res the result to write back to the register file.
     * @return true if the result could be written back to the register file.
     */
    bool OnCompletion(unsigned int unit, const Result& res) const;

    /**
     * Called in order to compute the result from a queued operation
     * @param op    [in] the operation with source information
     * @param start [in] the cycle number when the FP operation started
     * @return the result of the source operation
     */
    Result CalculateResult(const Operation& op) const;

    StorageTraceSet CreateStoragePermutation(size_t num_sources, std::vector<bool>& visited);

    Register<bool>       m_active;                ///< Process-trigger for FPU
    std::vector<Source*> m_sources;               ///< Data for the sources for this FPU
    std::vector<Unit>    m_units;                 ///< The execution units in the FPU
    std::vector<size_t>  m_mapping[FPU_NUM_OPS];  ///< List of units for each FPU op

    DefineStateVariable(size_t, last_source);
    Simulator::Result DoPipeline();

    void Cleanup();
public:
    /**
     * @brief Constructs the FPU.
     * @param parent     reference to the parent object
     * @param name       name of the FPU, irrelevant to simulation
     * @param num_inputs number of inputs that will be connected to this FPU
     */
    FPU(const std::string& name, Object& parent, Clock& clock, size_t num_inputs);

    /// Destroys the FPU object
    ~FPU();

    /**
     * @brief Registers a source to the FPU
     * @param regfile [in] the register file to use to write back results for this source
     * @param output [in] the storage traces that can be generated when writing the result
     * @return the unique for this source to be passed to QueueOperation
     */
    size_t RegisterSource(IFPUClient& client, const StorageTraceSet& output);

    /**
     * @brief Queues an FP operation.
     * @details This function determines the length of the operation and queues the operation in the corresponding
     *      pipeline. When the operation has completed, the result is written back to the register file.
     * @param source  the source input ID
     * @param op      the FP operation to perform
     * @param size    size of the FP operation (4 or 8)
     * @param Rav     first (or only) operand of the operation
     * @param Rbv     second operand of the operation
     * @param Rc      address of the destination register(s)
     * @return true if the operation could be queued.
     */
    bool QueueOperation(size_t source, FPUOperation op, int size, double Rav, double Rbv, const RegAddr& Rc);

    StorageTraceSet GetSourceTrace(size_t source) const;

    // Processes
    Process p_Pipeline;

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif
