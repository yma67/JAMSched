#include "io/cuda-wrapper.h"

jamc::cuda::CUDAPooler::~CUDAPooler() {
    eventReuse.consume_all([this] (const EventType& ev) {
        cudaEventDestroy(ev);
    });
}

jamc::cuda::CUDAPooler& jamc::cuda::CUDAPooler::GetInstance() {
    static jamc::cuda::CUDAPooler cp;
    return cp;
}

void jamc::cuda::CUDAPooler::EnqueueKernel(const std::function<void()>& f) {
    std::scoped_lock lk(m);
    kernels.push_back(f);
}

ErrorType jamc::cuda::CUDAPooler::WaitStream(StreamType s, size_t spinRound) {
    for (int i = 0; i < spinRound; i++) {
        auto rt = cudaStreamQuery(s);
        if (rt != cudaErrorNotReady) {
            return rt;
        } 
        if (i > 0) jamc::ctask::SleepFor(std::chrono::microseconds(i * 10));
    }
    CUDAPair prWait;
    prWait.ev = s;
    /*if (!eventReuse.pop(prWait.ev)) {
        if (cudaSuccess != cudaEventCreateWithFlags(&prWait.ev, cudaEventDisableTiming)) {
            printf("bad allocation event\n");
            exit(1);
        }
    }*/
    if (!promiseReuse.pop(prWait.pr)) {
        prWait.pr = new jamc::promise<ErrorType>();
    } else {
        *(prWait.pr) = jamc::promise<ErrorType>();
    }
    // cudaEventRecord(prWait.ev, s);
    /*for (int i = 0; i < spinRound; i++) {
        auto rt = cudaEventQuery(prWait.ev);
        if (rt != cudaErrorNotReady) {
            promiseReuse.push(prWait.pr);
            eventReuse.push(prWait.ev);
            return rt;
        } 
        jamc::ctask::SleepFor(std::chrono::microseconds(i * 10));
    }*/
    pendings.push(prWait);
    auto f = prWait.pr->get_future();
    auto ret = f.get();
    promiseReuse.push(prWait.pr);
    // eventReuse.push(prWait.ev);
    return ret;
}

void jamc::cuda::CUDAPooler::IterateOnce() {
    std::vector<CUDAPair> addBacks;
    std::unique_lock lk(m);
    auto krs = std::move(kernels);
    kernels = decltype(kernels)();
    lk.unlock();
    for (auto& kr: krs) kr();  
    pendings.consume_all([this, &addBacks] (const CUDAPair& p) {
        auto ret = cudaStreamQuery(p.ev);
        if (cudaErrorNotReady == ret) {
            addBacks.push_back(p);
            return;
        }
        p.pr->set_value(ret);
    });
    for (auto& addBack: addBacks) {
        pendings.push(addBack);
    }
}

ErrorType jamc::cuda::WaitStream(StreamType stream)
{
    return CUDAPooler::GetInstance().WaitStream(stream, 6);
}

ErrorType jamc::cuda::WaitStream(StreamType stream, size_t spinRound)
{
    return CUDAPooler::GetInstance().WaitStream(stream, spinRound);
}

ErrorType jamc::cuda::WaitStreamWithStreamCallback(StreamType stream)
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
