#include "../src/spawn_core.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main()
{
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
