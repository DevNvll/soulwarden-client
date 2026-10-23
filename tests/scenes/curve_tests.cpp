// The tests of the scene key curves (stage Q5, AST-240; research/scene-curves.md): the modes of the right key, the Bezier of the worked
// beach camera pair (Camera_Trans X, keys 0 and 0.383333, the value at 0.1916666), the ends, and the step of the DOF focus distance.
#include <cmath>
#include <cstdio>

#include "Scenes/SwcSceneCurve.h"

namespace
{
int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what)
{
	++g_checks;
	if (!ok)
	{
		++g_failures;
	}
	std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
}
} // namespace

int main()
{
	using swc::scene::Curve;
	using swc::scene::Key;
	// The first pair of the beach Camera_Trans X axis (research/scene-curves.md "Worked beach camera pair"): 0.14664550035377033.
	Curve x;
	x.keys = {Key{0.0f, 0.147062f, -0.333333f, 0.0005402f, 0.127778f, -0.0002071f, 3},
	          Key{0.383333f, 0.146116f, -0.127778f, 0.0003575f, 0.472222f, -0.0013211f, 3}};
	check(std::fabs(x.evaluate(0.1916666477918625f) - 0.14664550035377033f) < 2e-6f, "the Bezier of the worked beach pair (0.1466455)");
	check(x.evaluate(-1.0f) == 0.147062f && x.evaluate(5.0f) == 0.146116f, "a Bezier with small handles holds its end values");
	// Mode 1 (Step): the left value until the key (the DOF focus distance 0.6 until 7.3167, then 0.65).
	Curve step;
	step.keys = {Key{0.0f, 0.6f, 0, 0, 0, 0, 3}, Key{7.316699981689453f, 0.65f, 0, 0, 0, 0, 1}};
	check(step.evaluate(7.0f) == 0.6f && step.evaluate(7.316699981689453f) == 0.65f, "a step key holds the left value until its time");
	// Mode 0 and an unknown mode: linear.
	Curve line;
	line.keys = {Key{0.0f, 0.0f, 0, 0, 0, 0, 0}, Key{2.0f, 10.0f, 0, 0, 0, 0, 7}};
	check(std::fabs(line.evaluate(0.5f) - 2.5f) < 1e-6f, "an unknown mode is linear");
	Curve empty;
	check(empty.evaluate(1.0f, 40.0f) == 40.0f, "an empty axis gives the fallback");
	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
