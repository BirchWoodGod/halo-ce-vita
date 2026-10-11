/*
DYNAMIC_RESOLUTION_TEST.C

A desktop test of the dynamic resolution's controller
(port/vita/platform/dynamic_resolution.c, included whole): a simulated GPU
whose frame time grows with the pixels (a + b x scale^2, a little noise),
measured two frames late as on the Vita, through a GPU-bound fight (the
Pillar of Autumn's: 48 ms at 100%), a CPU-bound scene (the beach's: the
GPU idle most of the frame), a fight starting and ending, a worker-bound
frame (the GPU's tail short), hitches, the floor, the ceiling, and 60 FPS.

Run port/vita/tests/run_dynamic_resolution_test.sh.
*/

#include "../platform/dynamic_resolution.c"

#include <stdio.h>
#include <stdlib.h>

static int failures, checks;

static void check(int condition, const char *what)
{
	checks++;
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

/* a deterministic noise in [-1, 1] */
static unsigned int noise_state = 12345;

static float noise(void)
{
	noise_state = noise_state * 1103515245u + 12345u;
	return ((noise_state >> 8) & 0xffff) / 32767.5f - 1.0f;
}

struct run
{
	/* the levels the frames were drawn at (the GPU two frames behind) */
	int drawn[3];
	int changes, minimum, maximum;
	float level_sum, gpu_sum, over_budget;
	int frames;
};

/* frames of a scene whose GPU takes fixed + pixels x scale^2 ms (noise as
a fraction), its tail tail_part of it, the frame the longer of the GPU and
cpu_ms */
static void simulate(struct dynres_controller *controller, struct run *run, int frames, float fixed, float pixels,
	float noise_part, float tail_part, float cpu_ms)
{
	int frame;

	for (frame = 0; frame < frames; frame++)
	{
		int level = run->drawn[0];
		float scale = (float)level / DYNRES_UNITS;
		float gpu = (fixed + pixels * scale * scale) * (1.0f + noise_part * noise());
		float interval = gpu > cpu_ms ? gpu : cpu_ms;
		int before = controller->level, after;

		after = dynres_controller_frame(controller, gpu, gpu * tail_part, level, interval);
		if (after != before)
			run->changes++;
		run->drawn[0] = run->drawn[1];
		run->drawn[1] = run->drawn[2];
		run->drawn[2] = after;
		if (after < run->minimum)
			run->minimum = after;
		if (after > run->maximum)
			run->maximum = after;
		run->level_sum += (float)level;
		run->gpu_sum += gpu;
		if (gpu > controller->budget_ms)
			run->over_budget += 1.0f;
		run->frames++;
	}
}

static void run_reset(struct run *run, const struct dynres_controller *controller)
{
	run->drawn[0] = run->drawn[1] = run->drawn[2] = controller->level;
	run->changes = 0;
	run->minimum = run->maximum = controller->level;
	run->level_sum = run->gpu_sum = run->over_budget = 0.0f;
	run->frames = 0;
}

int main(void)
{
	struct dynres_controller controller;
	struct run run;
	const float budget = 1000.0f / 30.0f;
	int level_settled, frames_to_settle, frame;

	/* ---------- a GPU-bound fight: 48 ms at 100%, 12 at 50% */
	dynres_controller_init(&controller, 16, 32, budget);
	check(controller.level == 32, "starts at the ceiling");
	run_reset(&run, &controller);
	frames_to_settle = -1;
	for (frame = 0; frame < 60 && frames_to_settle < 0; frame++)
	{
		simulate(&controller, &run, 1, 0.0f, 48.0f, 0.0f, 0.8f, 20.0f);
		if (controller.level <= 26)
			frames_to_settle = frame + 1;
	}
	printf("  48 ms fight: at %d/32 after %d frames\n", controller.level, frames_to_settle);
	check(frames_to_settle > 0 && frames_to_settle <= 12, "a 48 ms fight: down to 26/32 or less within 12 frames");
	run_reset(&run, &controller);
	simulate(&controller, &run, 30 * 60, 0.0f, 48.0f, 0.10f, 0.8f, 20.0f);
	printf("  48 ms fight, 60 s with 10%% noise: levels %d..%d, %d changes, mean level %.1f, GPU mean %.1f ms, %.1f%% "
		"frames over the budget\n", run.minimum, run.maximum, run.changes, run.level_sum / run.frames,
		run.gpu_sum / run.frames, 100.0f * run.over_budget / run.frames);
	check(run.changes <= 6, "60 s of the same fight with noise: no oscillation (6 changes at most)");
	check(run.gpu_sum / run.frames < budget * DYNRES_HIGH && run.gpu_sum / run.frames > budget * 0.6f,
		"the fight's GPU time is kept under the high mark, and not far under");
	check(run.minimum >= 20 && run.maximum <= 27, "the fight's level stays between 63% and 84%");
	level_settled = controller.level;

	/* ---------- the fight ends: a light scene, 16 ms at 100% */
	run_reset(&run, &controller);
	simulate(&controller, &run, 30 * 2, 0.0f, 16.0f, 0.05f, 0.8f, 20.0f);
	check(controller.level <= level_settled + 6, "up more slowly than down: at most 6 steps in 2 s");
	simulate(&controller, &run, 30 * 6, 0.0f, 16.0f, 0.05f, 0.8f, 20.0f);
	printf("  light scene: %d/32 after 8 s\n", controller.level);
	check(controller.level == 32, "the light scene climbs back to the ceiling within 8 s");

	/* ---------- a spiky scene at the floor (the Vita, Oct 6: 17.7 ms at 50%
	with a spike about once a second, a step up a minute) */
	dynres_controller_init(&controller, 16, 32, budget);
	controller.level = 16;
	run_reset(&run, &controller);
	for (frame = 0; frame < 30 * 15; frame++)
	{
		int level = run.drawn[0];
		float scale = (float)level / DYNRES_UNITS;
		float gpu = (4.0f + 50.0f * scale * scale) * (frame % 30 == 0 ? 2.5f : 1.0f);
		int after = dynres_controller_frame(&controller, gpu, gpu * 0.8f, level, gpu > 20.0f ? gpu : 20.0f);

		run.drawn[0] = run.drawn[1];
		run.drawn[1] = run.drawn[2];
		run.drawn[2] = after;
	}
	printf("  spiky scene from the floor: %d/32 after 15 s\n", controller.level);
	check(controller.level >= 19, "a spike a second does not keep the scale at the floor");

	/* ---------- CPU-bound: the GPU idle most of a 55 ms frame */
	dynres_controller_init(&controller, 16, 32, budget);
	run_reset(&run, &controller);
	simulate(&controller, &run, 30 * 30, 2.0f, 8.0f, 0.10f, 0.3f, 55.0f);
	check(controller.level == 32 && run.changes == 0, "CPU-bound: the full resolution, never lowered");

	/* ---------- worker-bound: long GPU times from a slow submission, a
	short tail */
	dynres_controller_init(&controller, 16, 32, budget);
	run_reset(&run, &controller);
	simulate(&controller, &run, 30 * 10, 0.0f, 40.0f, 0.05f, 0.1f, 40.0f);
	check(controller.level == 32, "worker-bound (the GPU's tail short): not lowered");

	/* ---------- hitches: single slow frames */
	dynres_controller_init(&controller, 16, 32, budget);
	run_reset(&run, &controller);
	for (frame = 0; frame < 30 * 10; frame++)
		simulate(&controller, &run, 1, 0.0f, frame % 45 == 0 ? 90.0f : 20.0f, 0.0f, 0.8f, 20.0f);
	check(controller.level == 32, "a slow frame now and then: not lowered (two in a row needed)");

	/* ---------- the floor */
	dynres_controller_init(&controller, 20, 32, budget);
	run_reset(&run, &controller);
	simulate(&controller, &run, 30 * 10, 0.0f, 150.0f, 0.0f, 0.8f, 20.0f);
	check(controller.level == 20, "an impossible scene: held at the floor (63%)");

	/* ---------- a lower ceiling (the targets made smaller: CDRAM) */
	dynres_controller_init(&controller, 16, 24, budget);
	check(controller.level == 24, "starts at a lower ceiling");
	run_reset(&run, &controller);
	simulate(&controller, &run, 30 * 20, 0.0f, 10.0f, 0.0f, 0.8f, 20.0f);
	check(controller.level == 24, "never over the ceiling");
	dynres_controller_limits(&controller, 16, 32, budget);
	check(controller.level == 24, "a new ceiling keeps the level");
	dynres_controller_limits(&controller, 26, 32, budget);
	check(controller.level == 26, "a new floor above the level lifts it");

	/* ---------- switched on in a game drawn at a fixed 50% (the Vita, Oct 7:
	from the ceiling, 50% to 100% at once in a fight of 34 ms at 50%, and
	seconds over the budget) */
	dynres_controller_start(&controller, 16, 32, budget, 16);
	check(controller.level == 16, "switched on at a fixed 50%: starts at 50%, not the ceiling");
	run_reset(&run, &controller);
	simulate(&controller, &run, 25, 0.0f, 16.0f, 0.0f, 0.8f, budget);
	check(controller.level == 16, "switched on: no step up before the step above has fitted for a second");
	simulate(&controller, &run, 30 * 6, 0.0f, 16.0f, 0.0f, 0.8f, budget);
	printf("  switched on at 50%%, light scene: %d/32 after 7 s, %.0f frames over the budget\n", controller.level,
		run.over_budget);
	check(controller.level == 32 && run.over_budget == 0.0f, "switched on, a light scene: climbs to the ceiling, never over the budget");
	dynres_controller_start(&controller, 16, 32, budget, 16);
	run_reset(&run, &controller);
	simulate(&controller, &run, 30 * 10, 0.0f, 120.0f, 0.0f, 0.8f, 20.0f);
	check(controller.level == 16 && run.changes == 0, "switched on in a fight too heavy for more: stays at 50%");
	dynres_controller_start(&controller, 20, 24, budget, 16);
	check(controller.level == 20, "switched on below the floor: starts at the floor");
	dynres_controller_start(&controller, 16, 24, budget, 28);
	check(controller.level == 24, "switched on above the ceiling: starts at the ceiling");

	/* ---------- frames drawn at another level are not counted */
	dynres_controller_init(&controller, 16, 32, budget);
	for (frame = 0; frame < 10; frame++)
		dynres_controller_frame(&controller, 60.0f, 50.0f, 28, 33.3f);
	check(controller.level == 32, "frames drawn at another level: not counted");

	/* ---------- 60 FPS: a 16.7 ms budget */
	dynres_controller_init(&controller, 16, 32, 1000.0f / 60.0f);
	run_reset(&run, &controller);
	simulate(&controller, &run, 60 * 20, 1.0f, 24.0f, 0.05f, 0.8f, 10.0f);
	printf("  60 FPS, 25 ms at 100%%: %d/32, GPU mean %.1f ms\n", controller.level, run.gpu_sum / run.frames);
	check(controller.level < 32 && controller.level >= 22, "60 FPS: lowered to fit 16.7 ms");

	/* ---------- a fight that comes and goes every 4 s */
	dynres_controller_init(&controller, 16, 32, budget);
	run_reset(&run, &controller);
	for (frame = 0; frame < 15; frame++)
	{
		simulate(&controller, &run, 120, 0.0f, 48.0f, 0.05f, 0.8f, 20.0f);
		simulate(&controller, &run, 120, 0.0f, 20.0f, 0.05f, 0.8f, 20.0f);
	}
	printf("  a fight every 4 s for 60 s: %d changes, %.1f%% frames over the budget\n", run.changes,
		100.0f * run.over_budget / run.frames);
	check(100.0f * run.over_budget / run.frames < 5.0f, "a fight coming and going: under 5% of frames over the budget");

	printf("-- %d of %d checks failed\n", failures, checks);
	return failures != 0;
}
