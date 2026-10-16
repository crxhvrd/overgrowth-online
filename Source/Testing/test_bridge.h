//-----------------------------------------------------------------------------
//           Name: test_bridge.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: Localhost JSON-lines command server for automated tests,
//                 started with --test-bridge <port>. A socket thread only
//                 queues lines; every command runs on the main thread, once
//                 per frame, from the top of the main loop (no script is
//                 running there, so Execute and the online locks are safe).
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

class Engine;

namespace Json {
class Value;
}

class TestBridge {
   public:
    static TestBridge* Instance();

    bool Start(int port);
    void Stop();
    bool Running() const { return running_; }

    // Main thread, once per frame, before Engine::Update
    void Pump(Engine* engine);

    // Level messages seen here (main thread; kept while a test asks for them)
    void NoteLevelMessage(const std::string& msg);
    std::deque<std::string> level_messages_;

   private:
    void ThreadMain();
    std::string Handle(const std::string& line, Engine* engine);
    void Dispatch(const std::string& cmd, const Json::Value& req, Json::Value& out, Engine* engine);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex lock_;
    std::deque<std::string> incoming_;
    std::deque<std::string> outgoing_;
    int port_ = 0;
    uint64_t frame_ = 0;
};
