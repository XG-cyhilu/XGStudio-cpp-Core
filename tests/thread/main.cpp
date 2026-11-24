// Copyright 2026 XGstudio
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <xgthread/xgthread.h>

#include "../test_util.h"

#include <atomic>

int main() {
    XGT_CHECK(xgthread::HardwareThreads() >= 1);

    {
        xgthread::Pool pool(4);
        XGT_CHECK(pool.Threads() == 4);

        auto fut = pool.Submit([] { return 42; });
        XGT_CHECK(fut.get() == 42);

        std::atomic<int> n{ 0 };
        for (int i = 0; i < 200; ++i) pool.Post([&] { ++n; });
        XGT_CHECK(pool.WaitIdle(5000));
        XGT_CHECK(n.load() == 200);

        pool.Shutdown(true);
        XGT_CHECK(!pool.Post([] {}));   // closed
    }

    // CancelToken: copy shares state, child follows parent only one way
    {
        xgthread::CancelToken tok;
        XGT_CHECK(!tok.Cancelled());
        auto copy = tok;
        tok.Cancel();
        XGT_CHECK(copy.Cancelled());

        xgthread::CancelToken parent;
        auto child = xgthread::CancelToken::Child(parent);
        XGT_CHECK(!child.Cancelled());
        parent.Cancel();
        XGT_CHECK(child.Cancelled());

        xgthread::CancelToken standalone;
        xgthread::CancelToken child2 = xgthread::CancelToken::Child(parent);   // parent already cancelled
        XGT_CHECK(child2.Cancelled());
    }

    // MainQueue: posted tasks run only when the owner calls RunPending
    {
        xgthread::MainQueue q;
        int ran = 0;
        q.Post([&] { ++ran; });
        q.Post([&] { ++ran; });
        q.Post([&] { ran += 8; });
        XGT_CHECK(ran == 0);
        XGT_CHECK(q.Size() == 3);
        XGT_CHECK(q.RunPending(1) == 1 && ran == 1);
        XGT_CHECK(q.RunPending() == 2 && ran == 10);
        XGT_CHECK(q.Size() == 0);
    }

    XGT_FINISH("thread");
}
