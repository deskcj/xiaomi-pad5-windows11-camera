#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <utility>
#include <chrono>

// Two waiting immutable jobs plus one in-flight publication. No GPU or sensor
// owner here; the caller supplies a bounded operation on its existing handle.
template<class Job> class OrderedPublisher {
public:
    struct Stats {size_t count=0,intervals=0;double convertMs=0,submitMs=0,periodMs=0;};
    using Publish=std::function<bool(const Job&,double&,double&)>;
    explicit OrderedPublisher(Publish callback):publish(std::move(callback)),worker([this]{Run();}){}
    ~OrderedPublisher(){Close();}
    OrderedPublisher(const OrderedPublisher&)=delete;
    OrderedPublisher& operator=(const OrderedPublisher&)=delete;
    bool Enqueue(Job job){
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock,[this]{return closing||failed||pending.size()<2;});
        if(closing||failed)return false;
        pending.push_back(std::move(job));if(pending.size()>highWater)highWater=pending.size();
        changed.notify_all();return true;
    }
    bool Drain(){
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock,[this]{return failed||(!busy&&pending.empty());});
        return !failed;
    }
    void Close(){
        {std::lock_guard<std::mutex> lock(mutex);closing=true;changed.notify_all();}
        if(worker.joinable())worker.join();
    }
    bool Failed(){std::lock_guard<std::mutex> lock(mutex);return failed;}
    size_t Completed(){std::lock_guard<std::mutex> lock(mutex);return completed;}
    size_t HighWater(){std::lock_guard<std::mutex> lock(mutex);return highWater;}
    Stats TakeStats(){std::lock_guard<std::mutex> lock(mutex);Stats value=stats;stats=Stats{};return value;}
private:
    Publish publish;std::mutex mutex;std::condition_variable changed;
    std::deque<Job> pending;bool closing=false,busy=false,failed=false;
    size_t completed=0,highWater=0;Stats stats;std::thread worker;
    void Run(){
        std::chrono::steady_clock::time_point previous;
        bool havePrevious=false;
        for(;;){
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock,[this]{return closing||!pending.empty();});
                if(pending.empty())return;
                job=std::move(pending.front());pending.pop_front();busy=true;changed.notify_all();
            }
            double convert=0,submit=0;bool ok=false;
            try{ok=publish(job,convert,submit);}catch(...){ok=false;}
            const auto finished=std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(mutex);busy=false;
                if(ok){
                    ++completed;++stats.count;stats.convertMs+=convert;stats.submitMs+=submit;
                    if(havePrevious){++stats.intervals;stats.periodMs+=std::chrono::duration<double,std::milli>(finished-previous).count();}
                    previous=finished;havePrevious=true;
                }
                else{failed=true;closing=true;pending.clear();}
                changed.notify_all();if(failed)return;
            }
        }
    }
};
