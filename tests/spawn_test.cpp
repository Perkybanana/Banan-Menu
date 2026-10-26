#include "../src/spawn_core.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main()
{
    // Actor existence must never count as completion; this is the production request reducer.
    spawn::Request request;
    CHECK(!request.Begin(false, 1, 100));
    CHECK(!request.Pending() && request.Ready() == 0);
    CHECK(request.Begin(true, 2, 100));
    CHECK(!request.Begin(true, 1, 101));
    CHECK(!request.Bind(0, {0})); // missing correlation is not actor identity
    CHECK(request.Bind(0, {41}));
    CHECK(!request.Bind(1, {41})); // another slot cannot alias a marker
    CHECK(request.Bind(1, {42}));
    request.Observe(0, {41}, true, false, true);
    request.Observe(1, {42}, true, true, false);
    CHECK(request.Ready() == 0 && request.Pending());
    request.Observe(0, {41}, true, true, true);
    CHECK(request.Ready() == 1 && request.Pending());
    request.Observe(1, {43}, true, true, true); // different request cannot complete this slot
    CHECK(request.Ready() == 1 && !request.Pending());
    CHECK(request.Result() == spawn::Result::Stale);
    CHECK(request.Begin(true, 1, 100));
    request.Poll(true, 100 + spawn::TimeoutMs - 1);
    CHECK(request.Pending() && request.Ready() == 0); // no manager completion is not success
    request.Poll(true, 100 + spawn::TimeoutMs);
    CHECK(request.Result() == spawn::Result::Timeout && request.Ready() == 0);
    CHECK(request.Begin(true, 1, 100));
    request.Poll(false, 101);
    CHECK(request.Result() == spawn::Result::Transition);
    CHECK(request.Begin(true, 1, 100) && request.Bind(0, {41}));
    request.Observe(0, {41}, true, true, true);
    request.Poll(true, 101);
    CHECK(request.Ready() == 1 && request.Result() == spawn::Result::Success && !request.Pending());
    CHECK(request.Begin(true, 1, 100) && request.Bind(0, {41}));
    request.Observe(0, {41}, false, true, true);
    CHECK(request.Result() == spawn::Result::Stale && request.Ready() == 0);
    CHECK(request.Begin(true, 2, 100) && request.Bind(0, {51}) && request.Bind(1, {52}));
    request.Observe(0, {51}, true, true, true);
    request.Observe(0, {51}, true, false, true); // readiness can regress before the batch completes
    CHECK(request.Ready() == 0 && request.Pending());
    request.Fail();
    CHECK(!request.Pending() && request.Result() == spawn::Result::Dispatch);
    CHECK(spawn::ValidRequest(0, 1));
    CHECK(spawn::ValidRequest(4, 5));
    for (int type : {-1, 5, 999}) CHECK(!spawn::ValidRequest(type, 1));
    for (int count : {-1, 0, 6, 999}) CHECK(!spawn::ValidRequest(0, count));
    std::array<spawn::Point, spawn::MaxCount> points{};
    CHECK(spawn::Positions({10, 20, 30}, {1, 0, 0}, 1, points));
    CHECK(points[0].x == 360 && points[0].y == 20 && points[0].z == 30);
    CHECK(spawn::Positions({10, 20, 30}, {4, 0, 3}, 5, points));
    for (int i = 0; i < 5; ++i) {
        CHECK(points[i].x == 360 && points[i].z == 30);
        CHECK(points[i].y == 20 + (i - 2) * 160);
        if (i) CHECK(points[i].y - points[i - 1].y == 160);
    }
    CHECK(spawn::Positions({0, 0, 0}, {0, 2, 0}, 2, points));
    CHECK(points[0].y == 350 && points[1].y == 350 && points[0].x == 80 && points[1].x == -80);
    for (int count : {0, 6}) CHECK(!spawn::Positions({}, {1, 0, 0}, count, points));
    for (auto bad : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        CHECK(!spawn::Positions({bad, 0, 0}, {1, 0, 0}, 1, points));
        CHECK(!spawn::Positions({}, {1, bad, 0}, 1, points));
        CHECK(!spawn::Positions({}, {1, 0, bad}, 1, points));
    }
    CHECK(!spawn::Positions({}, {}, 1, points));
    CHECK(!spawn::Positions({}, {0, 0, 1}, 1, points));
    CHECK(!spawn::Positions({}, {1e-12, 0, 1}, 1, points));
    CHECK(!spawn::Positions({1e100, 0, 0}, {1, 0, 0}, 5, points));
    puts("Spawn request and placement tests passed");
}
