//
// Created by Yuxiang Ma on 10-11-2020.
//
#ifndef JAMSCRIPT_CUDA_H
#define JAMSCRIPT_CUDA_H
#ifdef JAMSCRIPT_HAS_CUDA
#include "concurrency/future.hpp"
#include <initializer_list>
#include <unordered_map>
#include <type_traits>
#include <boost/lockfree/stack.hpp>
#include <boost/lockfree/queue.hpp>
#include "cuda_runtime.h"
#include "concurrency/waitgroup.hpp"
#include <queue>

using StreamType = cudaStream_t;
using ErrorType = cudaError_t;
using EventType = cudaEvent_t;

namespace jamc {
namespace cuda {
    
    class CUDAPooler {
        struct CUDAPair {
            StreamType ev;
            jamc::promise<ErrorType>* pr;
        };
        jamc::SpinMutex m;
        std::deque<std::function<void()>> kernels;
        boost::lockfree::queue<CUDAPair> pendings;
        boost::lockfree::stack<EventType> eventReuse;
        boost::lockfree::stack<jamc::promise<ErrorType>*> promiseReuse;
    public:
        ~CUDAPooler();
        static CUDAPooler& GetInstance();
        ErrorType WaitStream(StreamType s, size_t spinRound);
        void EnqueueKernel(const std::function<void()>&);
        void IterateOnce();
    };
    
    template <typename... Args>
    class CommandArgs {
        using ArgTupleType = std::tuple<Args...>;
        std::array<void*, std::tuple_size<ArgTupleType>::value> arrayArgs;
        ArgTupleType actArgs;
    public:
        CommandArgs() = default;
        CommandArgs(Args... args) : actArgs(std::forward<Args>(args)...) {
            std::apply([this](auto& ...xs) { arrayArgs = {(&xs)...}; }, actArgs);
        }
        void** GetCudaKernelArgs() { return arrayArgs.data(); }
        ArgTupleType &GetArgTuple() { return actArgs; }
    };

    ErrorType WaitStream(StreamType stream);
    ErrorType WaitStream(StreamType stream, size_t spinRound);
    ErrorType WaitStreamWithStreamCallback(StreamType stream);

}
}
#endif
#endif //JAMSCRIPT_CUDA_H
