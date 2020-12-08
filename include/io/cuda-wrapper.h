//
// Created by Yuxiang Ma on 10-11-2020.
//
#ifndef JAMSCRIPT_CUDA_H
#define JAMSCRIPT_CUDA_H
#include "concurrency/future.hpp"
#include <initializer_list>
#include <unordered_map>
#include <type_traits>
#include "cuda_runtime.h"

using StreamType = cudaStream_t;
using ErrorType = cudaError_t;

namespace jamc {
namespace cuda {

    class StreamBundle
    {

        static void Callback(StreamType stream, ErrorType error, void* lpStreamBundle)
        {
            StreamBundle* data = static_cast<StreamBundle*>(lpStreamBundle);
            {
                std::unique_lock lk(data->m);
                data->resMap.emplace(stream, error);
                if (data->resMap.size() == data->totalStreamCount)
                {
                    lk.unlock();
                    data->cv.notify_one();
                }
            }
        }

        std::unordered_map<StreamType, ErrorType> resMap;
        SpinMutex m;
        ConditionVariable cv;
        std::size_t totalStreamCount;

    public:

        StreamBundle(std::initializer_list<StreamType> st) : totalStreamCount(st.size())
        {
            for (auto& stream: st)
            {
                auto status = ::cudaStreamAddCallback(stream, StreamBundle::Callback, this, 0);
                if (cudaSuccess != status)
                {
                    std::scoped_lock lk(m);
                    resMap.emplace(stream, status);
                }
            }
        }

        StreamBundle(std::vector<StreamType> st) : totalStreamCount(st.size())
        {
            for (auto& stream: st)
            {
                auto status = cudaStreamAddCallback(stream, StreamBundle::Callback, this, 0);
                if (cudaSuccess != status)
                {
                    std::scoped_lock lk(m);
                    resMap.emplace(stream, status);
                }
            }
        }

        std::unordered_map<StreamType, ErrorType> WaitThenGet()
        {
            std::unique_lock lk(m);
            while (resMap.size() < totalStreamCount) cv.wait(lk);
            return std::move(resMap);
        }

    };

    ErrorType WaitStream(StreamType stream)
    {
        auto r = std::make_unique<jamc::promise<ErrorType>>();
        auto f = r->get_future();
        auto status = ::cudaStreamAddCallback(stream,
            [] (StreamType stream,  ErrorType status, void* lpPromise)
            {
                auto* r = static_cast<jamc::promise<ErrorType>*>(lpPromise);
                r->set_value(status);
            },
            r.get(), 0);
        if (cudaSuccess != status)
        {
            return status;
        }
        return f.get();
    }
    
    std::unordered_map<StreamType, ErrorType> WaitStream(std::vector<StreamType> streams)
    {
        auto r = std::make_unique<StreamBundle>(streams);
        return r->WaitThenGet();
    }

}
}

#endif //JAMSCRIPT_CUDA_H
