/*
VITA_GXM.C

The GXM renderer behind the Vita's Direct3D device (port/vita/include/
vita_gxm.h). Built with VitaSDK's GCC: the Sce structures depend on its ABI.

- Display: two 960x544 buffers in CDRAM, flipped through the display queue.
- Memory: the contiguous window (the game's "physical" memory) is mapped for
  the GPU as it is; three per-frame rings (uncached) hold what draws copy;
  a texture pool in CDRAM holds decoded textures.
- Programs: the ones the levels make ship compiled in the VPK
  (app0:shaders.pak); any other Cg is compiled on the device by SceShaccCg
  (libshacccg.suprx, which Vita3K and every Vita that runs Xita have), on a
  thread of its own, and the result is kept on the memory card
  (ux0:data/haloce-vita/shaders) for the next start.
- Scenes: one per run of draws into the same targets; depth-stencil surfaces
  are loaded and stored at every scene, so a target keeps its depth across
  switches, as the game expects.

Frame ring reuse is fenced by the GPU's fragment notification at the end of
each frame's presentation.
*/

#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/shacccg.h>

#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vita_gxm.h"
#include "vita_host.h"
#include "overlay_font.h"

#define DISPLAY_WIDTH 960
#define DISPLAY_HEIGHT 544
#define DISPLAY_STRIDE 960
#define DISPLAY_BUFFER_COUNT 2
#define RING_COUNT 4
#define RING_SIZE (6 * 1024 * 1024)
#define WORKER_RING_SIZE (2 * 1024 * 1024)
#define POOL_SIZE (56 * 1024 * 1024)
#define PATCHER_BUFFER_SIZE (6 * 1024 * 1024)
#define PATCHER_USSE_SIZE (4 * 1024 * 1024)
#define SHADER_DIRECTORY "ux0:data/haloce-vita/shaders"
/* the GPU's cores, each counting a visibility test's samples into its own
part of the buffer (sceGxmSetVisibilityBuffer's stride per core) */
#define VISIBILITY_CORES 4
#define VISIBILITY_CORE_STRIDE (VGXM_VISIBILITY_SLOTS * 4)

#define ALIGN(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))

/* Direct3D values the draws carry (port/include/xdk) */
#define D3DCLEAR_ZBUFFER 0x1
#define D3DCLEAR_STENCIL 0x2
#define D3DCLEAR_TARGET_R 0x10
#define D3DCLEAR_TARGET_G 0x20
#define D3DCLEAR_TARGET_B 0x40
#define D3DCLEAR_TARGET_A 0x80
#define D3DCULL_CW 2304
#define D3DCULL_CCW 2305
#define D3DPT_POINTLIST 1
#define D3DPT_LINELIST 2
#define D3DPT_TRIANGLELIST 5
#define D3DPT_TRIANGLESTRIP 6
#define D3DPT_TRIANGLEFAN 7

static void log_line(const char *format, ...)
{
	char line[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	vita_host_log(line);
}

/* ---------- memory blocks */

struct block
{
	SceUID uid;
	void *base;
	unsigned int size;
};

static void *block_allocate(struct block *block, SceKernelMemBlockType type, unsigned int size, int map,
	const char *name)
{
	size = ALIGN(size, type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW ? 256 * 1024 : 4096);
	block->uid = sceKernelAllocMemBlock(name, type, size, NULL);
	if (block->uid < 0)
	{
		log_line("gxm: cannot allocate %s (%u bytes): 0x%08x", name, size, (unsigned)block->uid);
		block->base = NULL;
		return NULL;
	}
	sceKernelGetMemBlockBase(block->uid, &block->base);
	block->size = size;
	if (map)
	{
		int result = sceGxmMapMemory(block->base, size, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);

		if (result < 0)
		{
			log_line("gxm: cannot map %s: 0x%08x", name, (unsigned)result);
			sceKernelFreeMemBlock(block->uid);
			block->base = NULL;
			return NULL;
		}
	}
	return block->base;
}

/* ---------- state */

struct display_data
{
	void *address;
};

struct shader
{
	uint64_t hash;
	SceGxmShaderPatcherId id;
	const SceGxmProgram *program;
	int fragment;
	/* parameter resource indices: vertex inputs v<n>_in, samplers tex<n> */
	int input_index[16];
	int sampler_index[4];
};

struct target
{
	int depth;
	unsigned int width, height, stride;
	/* the screen-sized targets' render scale (HALO_RENDER_SCALE): the target is
	that fraction of the size asked for, and viewports and clips into it are
	scaled to match; 0 for 1 */
	float scale;
	struct block memory;
	SceGxmColorSurface color;
	SceGxmDepthStencilSurface depth_stencil;
	SceGxmRenderTarget *render_target;
};

#define MAXIMUM_SHADERS 8192
#define MAXIMUM_TARGETS 128

static unsigned int gxm_scene_count, gxm_scene_splits;
static unsigned int scene_histogram[128];
/* CPU time in scene begins and ends: [0] the first (main) target, [1] the rest */
static unsigned long long scene_switch_us[2];

static struct
{
	unsigned int ring_offset_peak;
	SceGxmContext *context;
	SceGxmShaderPatcher *patcher;
	struct block vdm_ring, vertex_ring, fragment_ring, fragment_usse_ring;
	unsigned int fragment_usse_offset;
	struct block patcher_buffer, patcher_vertex_usse, patcher_fragment_usse;
	unsigned int patcher_vertex_usse_offset, patcher_fragment_usse_offset;
	unsigned char context_host_memory[SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE];

	struct block display_memory[DISPLAY_BUFFER_COUNT];
	SceGxmColorSurface display_surface[DISPLAY_BUFFER_COUNT];
	SceGxmSyncObject *display_sync[DISPLAY_BUFFER_COUNT];
	SceGxmRenderTarget *display_render_target;
	unsigned int back_buffer, front_buffer;

	struct block rings[RING_COUNT];
	unsigned int ring_offset;
	unsigned int ring_index;
	/* the worker's own per-frame rings (its draws' fragment uniforms, the
	clears, blits and overlay), rotated at its present */
	struct block worker_rings[RING_COUNT];
	unsigned int worker_ring_offset;
	unsigned int worker_ring_index;
	struct block pool;
	unsigned int pool_offset;

	/* visibility tests: a buffer per worker ring (a frame's tests count
	into the buffer of the ring the worker executes it in); the frame
	number whose notification completes it (0 while it is recorded), the
	render scale its tests were drawn at, its highest slot used, and the
	ring whose buffer the context has */
	struct block visibility;
	volatile unsigned int visibility_frame[RING_COUNT];
	volatile unsigned long visibility_game_frame[RING_COUNT];
	unsigned long visibility_next_game_frame;
	float visibility_scale[RING_COUNT];
	unsigned int visibility_slots_used[RING_COUNT];
	int visibility_bound;

	volatile unsigned int *notification;
	unsigned int frame;

	struct shader shaders[MAXIMUM_SHADERS];
	unsigned int shader_count;
	struct target targets[MAXIMUM_TARGETS];
	unsigned int target_count;

	/* the scene being recorded */
	int in_scene;
	unsigned int scene_draws;
	unsigned long scene_color, scene_depth;
	unsigned long wanted_color, wanted_depth;

	/* built-in programs */
	unsigned long clear_vertex, clear_fragment, blit_vertex, blit_fragment;
	unsigned long overlay_vertex, overlay_fragment;
	int overlay_enabled, overlay_programs;
	float overlay_fps, overlay_tick_ms, overlay_render_ms;
	/* the settings panel's text, written by the game's thread and drawn by
	the worker: two copies, the index flips when one is complete */
	char menu_text[2][2048];
	volatile int menu_index, menu_visible, menu_selected;
	int shacccg_ready;
	int ready;
} gxm;

static void shader_precompile(void);

/* ---------- start-up */

static void display_callback(const void *callback_data)
{
	const struct display_data *data = callback_data;
	SceDisplayFrameBuf frame_buffer;
	static int raised;

	if (!raised)
	{
		/* this runs on GXM's display queue thread. With the game, the
		render worker and the tick each pinned to a core and busy, it got
		no core at the default priority, and sceGxmDisplayQueueAddEntry
		waited 30-50 ms a frame for it (measured as "GPU time"; 0.2 ms
		with the tick off). The highest user priority: it does little. */
		raised = 1;
		{
			SceUID self = sceKernelGetThreadId();
			int result = sceKernelChangeThreadPriority(self, 64);
			SceKernelThreadInfo info;

			memset(&info, 0, sizeof(info));
			info.size = sizeof(info);
			sceKernelGetThreadInfo(self, &info);
			log_line("gxm: display queue thread priority -> 64: 0x%08x (now %d, affinity 0x%x)", (unsigned)result,
				info.currentPriority, (unsigned)info.currentCpuAffinityMask);
		}
	}
	memset(&frame_buffer, 0, sizeof(frame_buffer));
	frame_buffer.size = sizeof(frame_buffer);
	frame_buffer.base = data->address;
	frame_buffer.pitch = DISPLAY_STRIDE;
	frame_buffer.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	frame_buffer.width = DISPLAY_WIDTH;
	frame_buffer.height = DISPLAY_HEIGHT;
	sceDisplaySetFrameBuf(&frame_buffer, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

static void *patcher_host_alloc(void *user, unsigned int size) { (void)user; return malloc(size); }
static void patcher_host_free(void *user, void *memory) { (void)user; free(memory); }

/* a render target object of its own for every target: each may have up to
scenesPerFrame scenes in flight, and the small ones are rendered many times
a frame */
static SceGxmRenderTarget *render_target_for(unsigned int width, unsigned int height)
{
	SceGxmRenderTargetParams parameters;
	SceGxmRenderTarget *target = NULL;
	int result;

	memset(&parameters, 0, sizeof(parameters));
	parameters.width = width;
	parameters.height = height;
	parameters.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
	parameters.driverMemBlock = -1;
	/* the game draws into a target several times a frame (the targets of a
	size share this object) */
	parameters.scenesPerFrame = 8; /* SCE_GXM_MAX_SCENES_PER_RENDERTARGET */
	result = sceGxmCreateRenderTarget(&parameters, &target);
	if (result < 0)
	{
		log_line("gxm: cannot create a %ux%u render target: 0x%08x", width, height, (unsigned)result);
		return NULL;
	}
	return target;
}

static const char clear_vertex_source[] =
	"void main(float3 position, out float4 out_position : POSITION)\n"
	"{\n"
	"	out_position = float4(position, 1.0);\n"
	"}\n";

static const char clear_fragment_source[] =
	"float4 main(uniform float4 color[1] : BUFFER[0]) : COLOR\n"
	"{\n"
	"	return color[0];\n"
	"}\n";

static const char overlay_vertex_source[] =
	"void main(float2 position, float4 color, out float4 out_position : POSITION,\n"
	"	out float4 out_color : COLOR0)\n"
	"{\n"
	"	out_position = float4(position, 0.5, 1.0);\n"
	"	out_color = color;\n"
	"}\n";

static const char overlay_fragment_source[] =
	"float4 main(float4 color : COLOR0) : COLOR\n"
	"{\n"
	"	return color;\n"
	"}\n";

static const char blit_vertex_source[] =
	"void main(float2 position, float2 texcoord, out float4 out_position : POSITION,\n"
	"	out float2 out_texcoord : TEXCOORD0)\n"
	"{\n"
	"	out_position = float4(position, 0.5, 1.0);\n"
	"	out_texcoord = texcoord;\n"
	"}\n";

static const char blit_fragment_source[] =
	"float4 main(float2 texcoord : TEXCOORD0, uniform sampler2D source) : COLOR\n"
	"{\n"
	"	return float4(tex2D(source, texcoord).rgb, 1.0);\n"
	"}\n";

int vgxm_initialize(void *arena, unsigned long arena_size)
{
	SceGxmInitializeParams initialize;
	SceGxmContextParams context;
	SceGxmShaderPatcherParams patcher;
	unsigned int index;
	int result;

	memset(&initialize, 0, sizeof(initialize));
	initialize.flags = 0;
	initialize.displayQueueMaxPendingCount = DISPLAY_BUFFER_COUNT - 1;
	initialize.displayQueueCallback = display_callback;
	initialize.displayQueueCallbackDataSize = sizeof(struct display_data);
	{
		/* HALO_GXM_PARAMETER_MB: the parameter buffer (tiled primitives per
		scene). The 16 MB default overflows at 848x480 and the GPU falls
		into partial renders: ~75 ms/frame in a fight, 1-4 ms at 640x480 */
		const char *setting = getenv("HALO_GXM_PARAMETER_MB");
		unsigned int megabytes = setting ? (unsigned int)atoi(setting) : 40;

		if (megabytes < 8 || megabytes > 64)
			megabytes = 40;
		initialize.parameterBufferSize = megabytes * 1024 * 1024;
	}
	result = sceGxmInitialize(&initialize);
	if (result < 0)
	{
		log_line("gxm: sceGxmInitialize failed: 0x%08x", (unsigned)result);
		return -1;
	}
	gxm.notification = sceGxmGetNotificationRegion();
	*gxm.notification = 0;

	if (!block_allocate(&gxm.vdm_ring, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE, 1, "vdm ring") ||
		!block_allocate(&gxm.vertex_ring, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE, 1, "vertex ring") ||
		!block_allocate(&gxm.fragment_ring, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE, 1, "fragment ring") ||
		!block_allocate(&gxm.fragment_usse_ring, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE, 0, "fragment usse ring"))
		return -1;
	sceGxmMapFragmentUsseMemory(gxm.fragment_usse_ring.base, gxm.fragment_usse_ring.size, &gxm.fragment_usse_offset);

	memset(&context, 0, sizeof(context));
	context.hostMem = gxm.context_host_memory;
	context.hostMemSize = sizeof(gxm.context_host_memory);
	context.vdmRingBufferMem = gxm.vdm_ring.base;
	context.vdmRingBufferMemSize = gxm.vdm_ring.size;
	context.vertexRingBufferMem = gxm.vertex_ring.base;
	context.vertexRingBufferMemSize = gxm.vertex_ring.size;
	context.fragmentRingBufferMem = gxm.fragment_ring.base;
	context.fragmentRingBufferMemSize = gxm.fragment_ring.size;
	context.fragmentUsseRingBufferMem = gxm.fragment_usse_ring.base;
	context.fragmentUsseRingBufferMemSize = gxm.fragment_usse_ring.size;
	context.fragmentUsseRingBufferOffset = gxm.fragment_usse_offset;
	result = sceGxmCreateContext(&context, &gxm.context);
	if (result < 0)
	{
		log_line("gxm: sceGxmCreateContext failed: 0x%08x", (unsigned)result);
		return -1;
	}

	/* the display */
	gxm.display_render_target = render_target_for(DISPLAY_WIDTH, DISPLAY_HEIGHT);
	for (index = 0; index < DISPLAY_BUFFER_COUNT; index++)
	{
		void *memory = block_allocate(&gxm.display_memory[index], SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
			4 * DISPLAY_STRIDE * DISPLAY_HEIGHT, 1, "display");

		if (!memory)
			return -1;
		memset(memory, 0, 4 * DISPLAY_STRIDE * DISPLAY_HEIGHT);
		sceGxmColorSurfaceInit(&gxm.display_surface[index], SCE_GXM_COLOR_FORMAT_A8B8G8R8,
			SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
			DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE, memory);
		sceGxmSyncObjectCreate(&gxm.display_sync[index]);
	}
	gxm.back_buffer = 0;
	gxm.front_buffer = DISPLAY_BUFFER_COUNT - 1;

	/* the shader patcher */
	if (!block_allocate(&gxm.patcher_buffer, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, PATCHER_BUFFER_SIZE, 1, "patcher") ||
		!block_allocate(&gxm.patcher_vertex_usse, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, PATCHER_USSE_SIZE, 0, "patcher vertex usse") ||
		!block_allocate(&gxm.patcher_fragment_usse, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, PATCHER_USSE_SIZE, 0, "patcher fragment usse"))
		return -1;
	sceGxmMapVertexUsseMemory(gxm.patcher_vertex_usse.base, gxm.patcher_vertex_usse.size, &gxm.patcher_vertex_usse_offset);
	sceGxmMapFragmentUsseMemory(gxm.patcher_fragment_usse.base, gxm.patcher_fragment_usse.size, &gxm.patcher_fragment_usse_offset);
	memset(&patcher, 0, sizeof(patcher));
	patcher.hostAllocCallback = patcher_host_alloc;
	patcher.hostFreeCallback = patcher_host_free;
	patcher.bufferMem = gxm.patcher_buffer.base;
	patcher.bufferMemSize = gxm.patcher_buffer.size;
	patcher.vertexUsseMem = gxm.patcher_vertex_usse.base;
	patcher.vertexUsseMemSize = gxm.patcher_vertex_usse.size;
	patcher.vertexUsseOffset = gxm.patcher_vertex_usse_offset;
	patcher.fragmentUsseMem = gxm.patcher_fragment_usse.base;
	patcher.fragmentUsseMemSize = gxm.patcher_fragment_usse.size;
	patcher.fragmentUsseOffset = gxm.patcher_fragment_usse_offset;
	result = sceGxmShaderPatcherCreate(&patcher, &gxm.patcher);
	if (result < 0)
	{
		log_line("gxm: sceGxmShaderPatcherCreate failed: 0x%08x", (unsigned)result);
		return -1;
	}

	/* the game's memory, the rings and the texture pool */
	result = sceGxmMapMemory(arena, arena_size, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
	if (result < 0)
	{
		log_line("gxm: cannot map the memory window: 0x%08x", (unsigned)result);
		return -1;
	}
	for (index = 0; index < RING_COUNT; index++)
	{
		if (!block_allocate(&gxm.rings[index], SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, RING_SIZE, 1, "frame ring"))
			return -1;
		if (!block_allocate(&gxm.worker_rings[index], SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, WORKER_RING_SIZE, 1, "worker ring"))
			return -1;
	}
	if (!block_allocate(&gxm.pool, SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, POOL_SIZE, 1, "texture pool"))
		return -1;
	if (block_allocate(&gxm.visibility, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		RING_COUNT * VISIBILITY_CORES * VISIBILITY_CORE_STRIDE, 1, "visibility"))
		memset(gxm.visibility.base, 0, gxm.visibility.size);
	gxm.visibility_bound = -1;

	sceIoMkdir(SHADER_DIRECTORY, 0777);
	gxm.clear_vertex = vgxm_shader_get(clear_vertex_source, 0);
	gxm.clear_fragment = vgxm_shader_get(clear_fragment_source, 1);
	gxm.blit_vertex = vgxm_shader_get(blit_vertex_source, 0);
	gxm.blit_fragment = vgxm_shader_get(blit_fragment_source, 1);
	gxm.overlay_vertex = vgxm_shader_get(overlay_vertex_source, 0);
	gxm.overlay_fragment = vgxm_shader_get(overlay_fragment_source, 1);
	{
		const char *setting = getenv("XV_FPS");

		gxm.overlay_programs = gxm.overlay_vertex && gxm.overlay_fragment;
		gxm.overlay_enabled = setting && atoi(setting) != 0 && gxm.overlay_programs;
	}
	if (!gxm.clear_vertex || !gxm.clear_fragment || !gxm.blit_vertex || !gxm.blit_fragment)
	{
		log_line("gxm: the built-in programs do not compile");
		return -1;
	}
	shader_precompile();
	gxm.ready = 1;
	vita_host_log_memory("with the renderer up");
	log_line("gxm: ready: %ux%u display, %u MB rings, %u MB texture pool, window %p (%lu MB) mapped",
		DISPLAY_WIDTH, DISPLAY_HEIGHT, RING_COUNT * RING_SIZE >> 20, POOL_SIZE >> 20, arena, arena_size >> 20);
	return 0;
}

/* ---------- memory */

void *vgxm_ring_alloc(unsigned long size, unsigned long alignment)
{
	/* (two threads allocate: the game's records and the worker's own) */
	unsigned int reserved = __atomic_fetch_add(&gxm.ring_offset, (unsigned int)(size + alignment), __ATOMIC_RELAXED);
	unsigned int offset = ALIGN(reserved, (unsigned int)alignment);

	if (offset + size > RING_SIZE)
	{
		static unsigned int reported;

		if (reported++ < 8)
			log_line("gxm: the frame ring is full (%u bytes)", RING_SIZE);
		return NULL;
	}
	return (unsigned char *)gxm.rings[gxm.ring_index].base + offset;
}

/* the worker thread's GPU-visible bytes for the frame it is executing (one
thread: no atomics); the ring is reused four presents later, when the GPU,
at most two frames behind, is done with it */
void *vgxm_worker_alloc(unsigned long size, unsigned long alignment)
{
	unsigned int offset = ALIGN(gxm.worker_ring_offset, (unsigned int)alignment);

	if (offset + size > WORKER_RING_SIZE)
	{
		static unsigned int reported;

		if (reported++ < 8)
			log_line("gxm: the worker ring is full (%u bytes)", WORKER_RING_SIZE);
		return NULL;
	}
	gxm.worker_ring_offset = offset + (unsigned int)size;
	return (unsigned char *)gxm.worker_rings[gxm.worker_ring_index].base + offset;
}

void vgxm_ring_next(unsigned long frame)
{
	if (gxm.ring_offset > gxm.ring_offset_peak)
		gxm.ring_offset_peak = gxm.ring_offset;
	gxm.ring_index = (unsigned int)(frame % RING_COUNT);
	__atomic_store_n(&gxm.ring_offset, 0u, __ATOMIC_RELEASE);
}

void *vgxm_pool_alloc(unsigned long size, unsigned long alignment)
{
	unsigned int offset = ALIGN(gxm.pool_offset, (unsigned int)alignment);

	if (offset + size > POOL_SIZE)
		return NULL;
	gxm.pool_offset = offset + (unsigned int)size;
	return (unsigned char *)gxm.pool.base + offset;
}

/* the sequential indices live at the pool's start and survive resets */
static unsigned int pool_floor;

void vgxm_pool_reset(void)
{
	if (gxm.in_scene)
	{
		sceGxmEndScene(gxm.context, NULL, NULL);
		gxm.in_scene = 0;
	}
	sceGxmFinish(gxm.context);
	if (!pool_floor)
		pool_floor = 65536 * 2;
	gxm.pool_offset = pool_floor;
}

unsigned long vgxm_pool_used(void)
{
	return gxm.pool_offset;
}

/* ---------- shaders */

static SceShaccCgSourceFile shacccg_source;

static SceShaccCgSourceFile *shacccg_open(const char *name, const SceShaccCgSourceLocation *included_from,
	const SceShaccCgCompileOptions *options, const char **error)
{
	(void)name;
	(void)included_from;
	(void)options;
	(void)error;
	return &shacccg_source;
}

static SceShaccCgCallbackList shacccg_callbacks;

static int shacccg_start(void)
{
	static const char *const paths[] = {
		"ur0:data/libshacccg.suprx", "ux0:data/libshacccg.suprx", "ux0:data/xita/libshacccg.suprx",
	};
	unsigned int index;

	if (gxm.shacccg_ready)
		return gxm.shacccg_ready > 0;
	gxm.shacccg_ready = -1;
	for (index = 0; index < sizeof(paths) / sizeof(paths[0]); index++)
	{
		SceIoStat stat;

		if (sceIoGetstat(paths[index], &stat) < 0)
			continue;
		if (sceKernelLoadStartModule(paths[index], 0, NULL, 0, NULL, NULL) >= 0)
		{
			sceShaccCgSetDefaultAllocator(malloc, free);
			sceShaccCgInitializeCallbackList(&shacccg_callbacks, SCE_SHACCCG_TRIVIAL);
			shacccg_callbacks.openFile = shacccg_open;
			gxm.shacccg_ready = 1;
			log_line("gxm: shader compiler %s", sceShaccCgGetVersionString());
			return 1;
		}
	}
	log_line("gxm: no libshacccg.suprx (ur0:data/): shaders cannot be compiled");
	return 0;
}

/* a malloc'd GXP program for the source, or NULL */
static SceGxmProgram *compile(const char *source, int fragment)
{
	SceShaccCgCompileOptions options;
	const SceShaccCgCompileOutput *output;
	SceGxmProgram *program = NULL;
	int index;

	if (!shacccg_start())
		return NULL;
	sceShaccCgInitializeCompileOptions(&options);
	options.mainSourceFile = "halo.cg";
	options.targetProfile = fragment ? SCE_SHACCCG_PROFILE_FP : SCE_SHACCCG_PROFILE_VP;
	options.entryFunctionName = "main";
	options.locale = SCE_SHACCCG_ENGLISH;
	options.optimizationLevel = 3;
	options.warningLevel = 1;
	shacccg_source.fileName = "halo.cg";
	shacccg_source.text = source;
	shacccg_source.size = strlen(source);
	output = sceShaccCgCompileProgram(&options, &shacccg_callbacks, 0);
	if (!output)
		return NULL;
	if (output->programData && output->programSize)
	{
		program = malloc(output->programSize);
		memcpy(program, output->programData, output->programSize);
	}
	else
	{
		{
			/* (the heap, which the compiler allocates from: a compile
			that fails for want of memory says "fatal internal error") */
			struct mallinfo heap = mallinfo();

			log_line("gxm: a %s program does not compile (heap: %d KB in use, %d KB free of %d KB)",
				fragment ? "fragment" : "vertex", heap.uordblks / 1024, heap.fordblks / 1024, heap.arena / 1024);
		}
		for (index = 0; index < output->diagnosticCount; index++)
		{
			const SceShaccCgDiagnosticMessage *message = &output->diagnostics[index];

			if (message->level >= SCE_SHACCCG_DIAGNOSTIC_LEVEL_ERROR)
				log_line("gxm: shader line %d: %s", message->location ? (int)message->location->lineNumber : -1,
					message->message ? message->message : "");
		}
	}
	sceShaccCgDestroyCompileOutput(output);
	/* (WIP, untested) HALO_SHADER_RELEASE=1: the compiler's memory handed
	back after each compile - in Vita3K the heap grew ~100 KB a compile
	until compiles failed ("fatal internal error") after 50-110 of them */
	if (getenv("HALO_SHADER_RELEASE") && atoi(getenv("HALO_SHADER_RELEASE")))
		sceShaccCgReleaseCompiler();
	return program;
}

static uint64_t source_hash(const char *source, int fragment)
{
	uint64_t hash = 14695981039346656037ULL ^ (uint64_t)fragment;

	while (*source)
		hash = (hash ^ (unsigned char)*source++) * 1099511628211ULL;
	return hash;
}

static SceGxmProgram *cache_read(uint64_t hash)
{
	char path[128];
	SceUID file;
	SceIoStat stat;
	SceGxmProgram *program;

	snprintf(path, sizeof(path), SHADER_DIRECTORY "/%016llx.gxp", (unsigned long long)hash);
	if (sceIoGetstat(path, &stat) < 0 || stat.st_size <= 0)
		return NULL;
	program = malloc((size_t)stat.st_size);
	file = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (file < 0 || sceIoRead(file, program, (SceSize)stat.st_size) != (int)stat.st_size)
	{
		if (file >= 0)
			sceIoClose(file);
		free(program);
		return NULL;
	}
	sceIoClose(file);
	/* a file cut short (the process killed while writing it) or otherwise
	not a program: registered, it hangs the patcher or the GPU at the
	next map load, every run - so it is dropped and compiled again */
	if (sceGxmProgramCheck(program) < 0 || sceGxmProgramGetSize(program) != (unsigned int)stat.st_size)
	{
		log_line("gxm: shader cache %016llx invalid (%ld bytes), removed", (unsigned long long)hash, (long)stat.st_size);
		free(program);
		sceIoRemove(path);
		return NULL;
	}
	return program;
}

volatile unsigned long long vgxm_cache_write_us;

static void cache_write_file(uint64_t hash, const SceGxmProgram *program);

/* (timed for the hitch log: the memory card's writes, file creation and
rename, can be slow) */
static void cache_write(uint64_t hash, const SceGxmProgram *program)
{
	unsigned long long before = sceKernelGetProcessTimeWide();

	cache_write_file(hash, program);
	vgxm_cache_write_us += sceKernelGetProcessTimeWide() - before;
}

static void cache_write_file(uint64_t hash, const SceGxmProgram *program)
{
	char path[128], temporary[128];
	SceUID file;

	/* written under another name and renamed into place, so a reader
	never sees a partial file */
	snprintf(path, sizeof(path), SHADER_DIRECTORY "/%016llx.gxp", (unsigned long long)hash);
	snprintf(temporary, sizeof(temporary), SHADER_DIRECTORY "/%016llx.tmp", (unsigned long long)hash);
	file = sceIoOpen(temporary, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (file < 0)
		return;
	if (sceIoWrite(file, program, sceGxmProgramGetSize(program)) != (int)sceGxmProgramGetSize(program))
	{
		sceIoClose(file);
		sceIoRemove(temporary);
		return;
	}
	sceIoClose(file);
	sceIoRemove(path);
	sceIoRename(temporary, path);
}

/* (the hitch log, d3d8_gxm.c) shaders compiled on the device since it last
looked, and the rest of what the worker spent on programs: loading them
(the shipped pack, the memory card's cache, writing the cache, registering)
and linking them. A compile in the background (HALO_SHADER_ASYNC) is
counted in vgxm_compiles_background, not in the worker's time. */
volatile unsigned long long vgxm_compile_us, vgxm_shader_load_us, vgxm_link_us;
volatile unsigned long vgxm_compiles, vgxm_shader_loads, vgxm_links, vgxm_compiles_background;

/* ---------- the shipped programs

The programs the campaign and the multiplayer maps make, compiled ahead by
the same SceShaccCg (tools/vita_shader_pack.py, from the sources the Linux
gxm-null harness collects with HALO_SHADER_COLLECT), so a first visit to an
area compiles nothing on the device. One file in the VPK, read whole at
start-up (it is small): "HCEVSHP1", the count, then per program its source
hash, offset and size, sorted by hash, then the programs (16-byte aligned).
HALO_SHADER_PACK=0 leaves it unread. */
#define SHADER_PACK_PATH "app0:shaders.pak"

struct shader_pack_entry
{
	uint64_t hash;
	uint32_t offset, size;
};

static struct
{
	unsigned char *data;
	const struct shader_pack_entry *entries;
	unsigned int count;
	unsigned long size;
	int opened;
} shader_pack;

static void shader_pack_open(void)
{
	SceUID file;
	SceIoStat stat;
	const char *setting = getenv("HALO_SHADER_PACK");
	unsigned int index;

	if (shader_pack.opened)
		return;
	shader_pack.opened = 1;
	if (setting && atoi(setting) == 0)
		return;
	if (sceIoGetstat(SHADER_PACK_PATH, &stat) < 0 || stat.st_size < 16)
	{
		log_line("gxm: no shipped shaders (%s)", SHADER_PACK_PATH);
		return;
	}
	shader_pack.data = memalign(16, (size_t)stat.st_size);
	if (!shader_pack.data)
		return;
	file = sceIoOpen(SHADER_PACK_PATH, SCE_O_RDONLY, 0);
	if (file < 0 || sceIoRead(file, shader_pack.data, (SceSize)stat.st_size) != (int)stat.st_size ||
		memcmp(shader_pack.data, "HCEVSHP1", 8))
	{
		if (file >= 0)
			sceIoClose(file);
		log_line("gxm: the shipped shaders (%s) are unreadable", SHADER_PACK_PATH);
		free(shader_pack.data);
		shader_pack.data = NULL;
		return;
	}
	sceIoClose(file);
	shader_pack.size = (unsigned long)stat.st_size;
	memcpy(&shader_pack.count, shader_pack.data + 8, 4);
	if (16 + (unsigned long long)shader_pack.count * sizeof(struct shader_pack_entry) > (unsigned long long)stat.st_size)
		shader_pack.count = 0;
	shader_pack.entries = (const struct shader_pack_entry *)(shader_pack.data + 16);
	for (index = 0; index < shader_pack.count; index++)
	{
		if ((unsigned long long)shader_pack.entries[index].offset + shader_pack.entries[index].size >
			(unsigned long long)stat.st_size)
		{
			shader_pack.count = index;
			break;
		}
	}
	log_line("gxm: %u shipped shaders (%ld KB)", shader_pack.count, (long)(stat.st_size / 1024));
}

/* the shipped program for the hash (in the pack's memory, kept for good), or NULL */
static SceGxmProgram *shader_pack_find(uint64_t hash)
{
	unsigned int low = 0, high = shader_pack.count;

	while (low < high)
	{
		unsigned int middle = low + (high - low) / 2;
		const struct shader_pack_entry *entry = &shader_pack.entries[middle];

		if (entry->hash < hash)
			low = middle + 1;
		else if (entry->hash > hash)
			high = middle;
		else
		{
			SceGxmProgram *program = (SceGxmProgram *)(shader_pack.data + entry->offset);

			if (sceGxmProgramCheck(program) < 0 || sceGxmProgramGetSize(program) != entry->size)
				return NULL;
			return program;
		}
	}
	return NULL;
}

static int shader_pack_owns(const SceGxmProgram *program)
{
	return shader_pack.data && (const unsigned char *)program >= shader_pack.data &&
		(const unsigned char *)program < shader_pack.data + shader_pack.size;
}

/* ---------- collecting the sources (for the pack)

HALO_SHADER_COLLECT=<directory>: each program's Cg is written there as
<hash>.vp.cg or <hash>.fp.cg, its exact bytes (the hash is of them), for
tools/vita_shader_pack.py. HALO_SHADER_PRECOMPILE=<directory>: at start-up
every such file there is compiled into the memory card's cache (on Vita3K,
whose SceShaccCg is the device's own), which the pack is then made from. */
static void shader_collect(uint64_t hash, const char *source, int fragment)
{
	static int checked;
	static const char *directory;
	char path[256];
	SceUID file;
	SceIoStat stat;

	if (!checked)
	{
		checked = 1;
		directory = getenv("HALO_SHADER_COLLECT");
		if (directory && !*directory)
			directory = NULL;
		if (directory)
			sceIoMkdir(directory, 0777);
	}
	if (!directory)
		return;
	snprintf(path, sizeof(path), "%s/%016llx.%s.cg", directory, (unsigned long long)hash, fragment ? "fp" : "vp");
	if (sceIoGetstat(path, &stat) >= 0)
		return;
	file = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (file < 0)
		return;
	sceIoWrite(file, source, strlen(source));
	sceIoClose(file);
}

/* ---------- registered programs */

static unsigned long shader_find(uint64_t hash)
{
	unsigned int index;

	for (index = 0; index < gxm.shader_count; index++)
	{
		if (gxm.shaders[index].hash == hash)
			return index + 1;
	}
	return 0;
}

/* registers a loaded or compiled program (malloc'd, or the pack's) */
static unsigned long shader_register(uint64_t hash, int fragment, SceGxmProgram *program)
{
	struct shader *shader;
	unsigned int index;
	int result;

	if (gxm.shader_count >= MAXIMUM_SHADERS)
	{
		if (!shader_pack_owns(program))
			free(program);
		return 0;
	}
	shader = &gxm.shaders[gxm.shader_count];
	memset(shader, 0, sizeof(*shader));
	result = sceGxmShaderPatcherRegisterProgram(gxm.patcher, program, &shader->id);
	if (result < 0)
	{
		log_line("gxm: cannot register a program: 0x%08x", (unsigned)result);
		if (!shader_pack_owns(program))
			free(program);
		return 0;
	}
	shader->hash = hash;
	shader->fragment = fragment;
	shader->program = sceGxmShaderPatcherGetProgramFromId(shader->id);
	for (index = 0; index < 16; index++)
	{
		char name[16];
		const SceGxmProgramParameter *parameter;

		snprintf(name, sizeof(name), "v%u_in", index);
		parameter = sceGxmProgramFindParameterByName(shader->program, name);
		shader->input_index[index] = parameter ? (int)sceGxmProgramParameterGetResourceIndex(parameter) : -1;
	}
	for (index = 0; index < 4; index++)
	{
		char name[16];
		const SceGxmProgramParameter *parameter;

		snprintf(name, sizeof(name), "tex%u", index);
		parameter = sceGxmProgramFindParameterByName(shader->program, name);
		shader->sampler_index[index] = parameter ? (int)sceGxmProgramParameterGetResourceIndex(parameter) : -1;
	}
	return ++gxm.shader_count;
}

unsigned long vgxm_shader_get(const char *source, int fragment)
{
	uint64_t hash = source_hash(source, fragment);
	SceGxmProgram *program;
	unsigned long id;
	unsigned long long before;

	if ((id = shader_find(hash)) != 0)
		return id;
	if (gxm.shader_count >= MAXIMUM_SHADERS)
		return 0;
	if (getenv("HALO_TRACE_FILES"))
		log_line("trace: shader %016llx (%s), %u registered", (unsigned long long)hash, fragment ? "fragment" : "vertex",
			gxm.shader_count);
	shader_collect(hash, source, fragment);
	before = sceKernelGetProcessTimeWide();
	shader_pack_open();
	program = shader_pack_find(hash);
	if (!program)
		program = cache_read(hash);
	if (!program)
	{
		unsigned long long compile_from = sceKernelGetProcessTimeWide();

		vgxm_shader_load_us += compile_from - before;
		program = compile(source, fragment);
		before = sceKernelGetProcessTimeWide();
		vgxm_compile_us += before - compile_from;
		vgxm_compiles++;
		if (!program)
			return 0;
		cache_write(hash, program);
	}
	id = shader_register(hash, fragment, program);
	vgxm_shader_load_us += sceKernelGetProcessTimeWide() - before;
	vgxm_shader_loads++;
	return id;
}

/* ---------- compiling in the background (HALO_SHADER_ASYNC)

A program neither shipped nor cached is compiled on a thread of its own, at
a low priority, and read from the memory card's cache there too: the worker
never waits for the compiler (0.6-1.5 s a program on the hardware) nor for
the memory card. Until it is ready the draws that need it are skipped
(VGXM_SHADER_PENDING), so a program the pack misses costs a surface missing
for a second instead of a frozen frame. Only the worker asks; only the
compiler thread compiles once it is running (SceShaccCg and the source
callback are used from one thread). Registering with the patcher stays on
the worker. */
struct shader_job
{
	struct shader_job *next;
	uint64_t hash;
	int fragment;
	char *source;
	SceGxmProgram *program;
	/* 0 queued, 1 being compiled, 2 done (program NULL: it does not compile) */
	volatile int state;
	int from_cache;
	unsigned long long queued_us, done_us, compile_us;
};

static struct
{
	SceUID lock, ready;
	struct shader_job *jobs;
	int started, enabled;
	unsigned long queued;
} shader_async;

static void shader_compiler_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		struct shader_job *job;
		SceGxmProgram *program;
		unsigned long long from;

		sceKernelWaitSema(shader_async.ready, 1, NULL);
		sceKernelWaitSema(shader_async.lock, 1, NULL);
		for (job = shader_async.jobs; job; job = job->next)
		{
			if (job->state == 0)
			{
				job->state = 1;
				break;
			}
		}
		sceKernelSignalSema(shader_async.lock, 1);
		if (!job)
			continue;
		from = sceKernelGetProcessTimeWide();
		program = cache_read(job->hash);
		job->from_cache = program != NULL;
		if (!program)
		{
			program = compile(job->source, job->fragment);
			job->compile_us = sceKernelGetProcessTimeWide() - from;
			if (program)
				cache_write_file(job->hash, program);
		}
		free(job->source);
		job->source = NULL;
		job->program = program;
		job->done_us = sceKernelGetProcessTimeWide();
		__atomic_store_n(&job->state, 2, __ATOMIC_RELEASE);
	}
}

static int shader_async_enabled(void)
{
	if (!shader_async.started)
	{
		const char *setting = getenv("HALO_SHADER_ASYNC");

		shader_async.started = 1;
		shader_async.enabled = !setting || atoi(setting) != 0;
		if (shader_async.enabled)
		{
			shader_async.lock = sceKernelCreateSema("shader jobs", 0, 1, 1, NULL);
			shader_async.ready = sceKernelCreateSema("shader jobs ready", 0, 0, 0x7fffffff, NULL);
			/* (any core, below the game's threads: it takes the time they leave) */
			if (shader_async.lock < 0 || shader_async.ready < 0 ||
				vita_host_thread_start_priority("shader compiler", shader_compiler_thread, NULL, -1, 180) != 0)
			{
				log_line("gxm: cannot start the shader compiler thread: shaders are compiled as the worker needs them");
				shader_async.enabled = 0;
			}
		}
	}
	return shader_async.enabled;
}

unsigned long vgxm_shader_request(const char *source, int fragment)
{
	uint64_t hash;
	struct shader_job *job, **link;
	SceGxmProgram *program;
	unsigned long id;
	unsigned long long before;

	if (!shader_async_enabled())
		return vgxm_shader_get(source, fragment);
	hash = source_hash(source, fragment);
	if ((id = shader_find(hash)) != 0)
		return id;
	if (gxm.shader_count >= MAXIMUM_SHADERS)
		return 0;
	sceKernelWaitSema(shader_async.lock, 1, NULL);
	for (link = &shader_async.jobs; (job = *link) != NULL; link = &job->next)
	{
		if (job->hash == hash)
			break;
	}
	if (job)
	{
		if (__atomic_load_n(&job->state, __ATOMIC_ACQUIRE) != 2)
		{
			sceKernelSignalSema(shader_async.lock, 1);
			return VGXM_SHADER_PENDING;
		}
		*link = job->next;
		sceKernelSignalSema(shader_async.lock, 1);
		before = sceKernelGetProcessTimeWide();
		program = job->program;
		if (!job->from_cache)
		{
			vgxm_compiles_background++;
			log_line("gxm: shader %016llx compiled in the background in %.1f ms (ready %.1f ms after it was first drawn)",
				(unsigned long long)hash, job->compile_us / 1000.0, (job->done_us - job->queued_us) / 1000.0);
		}
		free(job);
		id = program ? shader_register(hash, fragment, program) : 0;
		vgxm_shader_load_us += sceKernelGetProcessTimeWide() - before;
		vgxm_shader_loads++;
		return id;
	}
	sceKernelSignalSema(shader_async.lock, 1);
	shader_collect(hash, source, fragment);
	/* shipped: registered now, no waiting */
	before = sceKernelGetProcessTimeWide();
	shader_pack_open();
	if ((program = shader_pack_find(hash)) != NULL)
	{
		id = shader_register(hash, fragment, program);
		vgxm_shader_load_us += sceKernelGetProcessTimeWide() - before;
		vgxm_shader_loads++;
		return id;
	}
	job = calloc(1, sizeof(*job));
	if (!job || !(job->source = strdup(source)))
	{
		/* (asked again at the next draw; never compiled here, beside the
		compiler thread) */
		free(job);
		return VGXM_SHADER_PENDING;
	}
	job->hash = hash;
	job->fragment = fragment;
	job->queued_us = sceKernelGetProcessTimeWide();
	sceKernelWaitSema(shader_async.lock, 1, NULL);
	job->next = shader_async.jobs;
	shader_async.jobs = job;
	shader_async.queued++;
	sceKernelSignalSema(shader_async.lock, 1);
	sceKernelSignalSema(shader_async.ready, 1);
	return VGXM_SHADER_PENDING;
}

/* HALO_SHADER_PRECOMPILE=<directory> (start-up): every <hash>.vp.cg and
<hash>.fp.cg there into the memory card's cache, unless it is there */
static void shader_precompile(void)
{
	const char *directory = getenv("HALO_SHADER_PRECOMPILE");
	SceUID listing;
	SceIoDirent entry;
	unsigned long compiled = 0, cached = 0, failed = 0, mismatched = 0;
	unsigned long long from = sceKernelGetProcessTimeWide();

	if (!directory || !*directory)
		return;
	listing = sceIoDopen(directory);
	if (listing < 0)
	{
		log_line("shader precompile: cannot list %s", directory);
		return;
	}
	while (sceIoDread(listing, &entry) > 0)
	{
		char path[320];
		unsigned long long hash;
		int fragment;
		size_t length = strlen(entry.d_name);
		SceUID file;
		char *source;
		SceGxmProgram *program;
		SceIoStat stat;

		if (length != 22 || strcmp(entry.d_name + 19, ".cg") || entry.d_name[16] != '.' ||
			(strncmp(entry.d_name + 17, "fp", 2) && strncmp(entry.d_name + 17, "vp", 2)))
			continue;
		fragment = entry.d_name[17] == 'f';
		hash = strtoull(entry.d_name, NULL, 16);
		snprintf(path, sizeof(path), "%s/%s", directory, entry.d_name);
		if (sceIoGetstat(path, &stat) < 0 || stat.st_size <= 0)
			continue;
		source = malloc((size_t)stat.st_size + 1);
		file = sceIoOpen(path, SCE_O_RDONLY, 0);
		if (!source || file < 0 || sceIoRead(file, source, (SceSize)stat.st_size) != (int)stat.st_size)
		{
			if (file >= 0)
				sceIoClose(file);
			free(source);
			continue;
		}
		sceIoClose(file);
		source[stat.st_size] = 0;
		if (source_hash(source, fragment) != hash)
		{
			mismatched++;
			free(source);
			continue;
		}
		if ((program = cache_read(hash)) != NULL)
		{
			cached++;
			free(program);
		}
		else if ((program = compile(source, fragment)) != NULL)
		{
			cache_write(hash, program);
			compiled++;
			free(program);
		}
		else
		{
			log_line("shader precompile: %s does not compile", entry.d_name);
			failed++;
		}
		free(source);
	}
	sceIoDclose(listing);
	log_line("shader precompile: %lu compiled, %lu already cached, %lu failed, %lu with a wrong name, in %.1f s",
		compiled, cached, failed, mismatched, (sceKernelGetProcessTimeWide() - from) / 1e6);
}

/* ---------- linked programs */

struct vertex_program_key
{
	unsigned long shader;
	unsigned long attribute_count;
	struct vgxm_attribute attributes[VGXM_ATTRIBUTE_COUNT];
	unsigned long stream_count;
	unsigned long strides[VGXM_STREAM_COUNT];
};

struct vertex_program_entry
{
	struct vertex_program_entry *next;
	struct vertex_program_key key;
	SceGxmVertexProgram *program;
};

struct fragment_program_entry
{
	struct fragment_program_entry *next;
	unsigned long shader, vertex_shader;
	uint32_t blend;
	SceGxmFragmentProgram *program;
};

#define PROGRAM_BUCKETS 2048
static unsigned int vertex_program_count, fragment_program_count;

static struct vertex_program_entry *vertex_programs[PROGRAM_BUCKETS];
static struct fragment_program_entry *fragment_programs[PROGRAM_BUCKETS];

/* (a word at a time: a vertex program key is 204 bytes, hashed on every
draw that changes the program, and a byte at a time was 204 dependent
multiplies; the hash only picks the bucket, the key is compared in full) */
static uint32_t hash_words(const void *data, unsigned int size)
{
	const uint32_t *words = data;
	uint32_t hash = 2166136261U;

	for (size /= 4; size; size--)
		hash = (hash ^ *words++) * 16777619U;
	return hash;
}
typedef char vertex_program_key_size_assert[sizeof(struct vertex_program_key) % 4 == 0 ? 1 : -1];

static SceGxmAttributeFormat attribute_format(unsigned int format)
{
	switch (format)
	{
	case _vgxm_attribute_u8n: return SCE_GXM_ATTRIBUTE_FORMAT_U8N;
	case _vgxm_attribute_u8: return SCE_GXM_ATTRIBUTE_FORMAT_U8;
	case _vgxm_attribute_s16: return SCE_GXM_ATTRIBUTE_FORMAT_S16;
	case _vgxm_attribute_s16n: return SCE_GXM_ATTRIBUTE_FORMAT_S16N;
	default: return SCE_GXM_ATTRIBUTE_FORMAT_F32;
	}
}

static SceGxmVertexProgram *vertex_program_get(const struct vertex_program_key *key)
{
	static struct vertex_program_entry *last;
	uint32_t hash;
	struct vertex_program_entry **bucket;
	struct vertex_program_entry *entry;
	const struct shader *shader = &gxm.shaders[key->shader - 1];
	SceGxmVertexAttribute attributes[VGXM_ATTRIBUTE_COUNT];
	SceGxmVertexStream streams[VGXM_STREAM_COUNT];
	unsigned int index, count = 0;
	int result;

	if (last && !memcmp(&last->key, key, sizeof(*key)))
		return last->program;
	hash = hash_words(key, sizeof(*key));
	bucket = &vertex_programs[hash % PROGRAM_BUCKETS];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (!memcmp(&entry->key, key, sizeof(*key)))
		{
			last = entry;
			return entry->program;
		}
	}
	entry = calloc(1, sizeof(*entry));
	entry->key = *key;
	for (index = 0; index < key->attribute_count; index++)
	{
		const struct vgxm_attribute *attribute = &key->attributes[index];
		int input = attribute->reg < 16 ? shader->input_index[attribute->reg] : -1;

		if (input < 0)
			continue;
		attributes[count].streamIndex = attribute->stream;
		attributes[count].offset = attribute->offset;
		attributes[count].format = attribute_format(attribute->format);
		attributes[count].componentCount = attribute->components;
		attributes[count].regIndex = (uint16_t)input;
		count++;
	}
	for (index = 0; index < key->stream_count; index++)
	{
		streams[index].stride = (uint16_t)key->strides[index];
		streams[index].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
	}
	{
		unsigned long long before = sceKernelGetProcessTimeWide();

		result = sceGxmShaderPatcherCreateVertexProgram(gxm.patcher, shader->id, attributes, count, streams,
			key->stream_count, &entry->program);
		vgxm_link_us += sceKernelGetProcessTimeWide() - before;
		vgxm_links++;
	}
	if (result < 0)
	{
		log_line("gxm: cannot link a vertex program: 0x%08x", (unsigned)result);
		entry->program = NULL;
	}
	else
		vertex_program_count++;
	entry->next = *bucket;
	*bucket = entry;
	last = entry;
	return entry->program;
}

static SceGxmFragmentProgram *fragment_program_get(unsigned long shader, unsigned long vertex_shader,
	const SceGxmBlendInfo *blend)
{
	uint32_t blend_word = 0;
	uint32_t hash;
	struct fragment_program_entry **bucket, *entry;
	int result;

	if (blend)
		memcpy(&blend_word, blend, sizeof(*blend) < 4 ? sizeof(*blend) : 4);
	else
		blend_word = 0xffffffffU;
	{
		static struct fragment_program_entry *last;

		if (last && last->shader == shader && last->vertex_shader == vertex_shader && last->blend == blend_word)
			return last->program;
		hash = (uint32_t)shader * 2654435761U ^ (uint32_t)vertex_shader * 40503U ^ blend_word;
		bucket = &fragment_programs[hash % PROGRAM_BUCKETS];
		for (entry = *bucket; entry; entry = entry->next)
		{
			if (entry->shader == shader && entry->vertex_shader == vertex_shader && entry->blend == blend_word)
			{
				last = entry;
				return entry->program;
			}
		}
	}
	entry = calloc(1, sizeof(*entry));
	entry->shader = shader;
	entry->vertex_shader = vertex_shader;
	entry->blend = blend_word;
	{
		unsigned long long before = sceKernelGetProcessTimeWide();

		result = sceGxmShaderPatcherCreateFragmentProgram(gxm.patcher, gxm.shaders[shader - 1].id,
			SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, blend,
			gxm.shaders[vertex_shader - 1].program, &entry->program);
		vgxm_link_us += sceKernelGetProcessTimeWide() - before;
		vgxm_links++;
	}
	if (result < 0)
	{
		log_line("gxm: cannot link a fragment program: 0x%08x", (unsigned)result);
		entry->program = NULL;
	}
	else
		fragment_program_count++;
	entry->next = *bucket;
	*bucket = entry;
	return entry->program;
}

const char *vgxm_counts(void)
{
	static char line[160];

	snprintf(line, sizeof(line), "shaders %u, programs %u+%u, targets %u, patcher %u KB/%u KB",
		gxm.shader_count, vertex_program_count, fragment_program_count, gxm.target_count,
		(unsigned)(sceGxmShaderPatcherGetBufferMemAllocated(gxm.patcher) / 1024),
		(unsigned)(sceGxmShaderPatcherGetFragmentUsseMemAllocated(gxm.patcher) / 1024));
	return line;
}

/* ---------- textures */

int vgxm_texture_initialize(struct vgxm_texture *texture, const void *data, unsigned long format,
	unsigned long layout, unsigned long width, unsigned long height, unsigned long levels)
{
	SceGxmTexture *gxm_texture = (SceGxmTexture *)texture;
	SceGxmTextureFormat texture_format;
	int result;

	switch (format)
	{
	case _vgxm_texture_dxt1: texture_format = SCE_GXM_TEXTURE_FORMAT_UBC1_ABGR; break;
	case _vgxm_texture_dxt3: texture_format = SCE_GXM_TEXTURE_FORMAT_UBC2_ABGR; break;
	case _vgxm_texture_dxt5: texture_format = SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR; break;
	default: texture_format = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB; break;
	}
	switch (layout)
	{
	case _vgxm_texture_swizzled:
		result = sceGxmTextureInitSwizzled(gxm_texture, data, texture_format, width, height, levels);
		break;
	case _vgxm_texture_cube:
		result = sceGxmTextureInitCube(gxm_texture, data, texture_format, width, height, levels);
		break;
	default:
		result = sceGxmTextureInitLinear(gxm_texture, data, texture_format, width, height, levels);
		break;
	}
	if (result < 0)
	{
		log_line("gxm: texture %lux%lu format %lu layout %lu levels %lu: 0x%08x", width, height, format, layout,
			levels, (unsigned)result);
		return -1;
	}
	return 0;
}

void vgxm_texture_set_sampler(struct vgxm_texture *texture, unsigned long min_filter, unsigned long mag_filter,
	unsigned long mip_filter, unsigned long address_u, unsigned long address_v, float lod_bias)
{
	SceGxmTexture *gxm_texture = (SceGxmTexture *)texture;
	static const SceGxmTextureAddrMode modes[] = {
		SCE_GXM_TEXTURE_ADDR_REPEAT,       /* 0 */
		SCE_GXM_TEXTURE_ADDR_REPEAT,       /* D3DTADDRESS_WRAP */
		SCE_GXM_TEXTURE_ADDR_MIRROR,       /* D3DTADDRESS_MIRROR */
		SCE_GXM_TEXTURE_ADDR_CLAMP,        /* D3DTADDRESS_CLAMP */
		SCE_GXM_TEXTURE_ADDR_CLAMP,        /* D3DTADDRESS_BORDER */
		SCE_GXM_TEXTURE_ADDR_CLAMP,        /* D3DTADDRESS_CLAMPTOEDGE */
	};

	(void)lod_bias;
	sceGxmTextureSetMinFilter(gxm_texture, min_filter == 1 ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR);
	sceGxmTextureSetMagFilter(gxm_texture, mag_filter == 1 ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR);
	sceGxmTextureSetMipFilter(gxm_texture, mip_filter ? SCE_GXM_TEXTURE_MIP_FILTER_ENABLED : SCE_GXM_TEXTURE_MIP_FILTER_DISABLED);
	/* cube maps address as they must */
	if (sceGxmTextureGetType(gxm_texture) != SCE_GXM_TEXTURE_CUBE)
	{
		sceGxmTextureSetUAddrMode(gxm_texture, modes[address_u < 6 ? address_u : 0]);
		sceGxmTextureSetVAddrMode(gxm_texture, modes[address_v < 6 ? address_v : 0]);
	}
}

/* ---------- render targets */

/* CDRAM for a small colour target (the glow's and the shadows' 128x128s,
mip chains): CDRAM blocks come in 256 KB steps, and a block each held a
64 KB 128x128 target in 256 KB, so two dozen of them took 6 MB of the
12 MB left and the next failed (b30, "cannot allocate colour target").
Small targets are carved from shared 256 KB blocks instead (targets are
never freed). NULL when there is no memory. */
#define SMALL_TARGET_BLOCK (256 * 1024)

static void *small_target_memory(unsigned int size)
{
	static struct block current;
	static unsigned int used;
	struct block block;

	size = ALIGN(size, 4096);
	if (size > SMALL_TARGET_BLOCK / 2)
		return NULL;
	if (!current.base || used + size > current.size)
	{
		if (!block_allocate(&block, SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, SMALL_TARGET_BLOCK, 1, "colour targets"))
			return NULL;
		current = block;
		used = 0;
	}
	used += size;
	return (unsigned char *)current.base + used - size;
}

/* CDRAM for a colour target: a small one's share of a block, else a block
of its own */
static void *colour_target_memory(struct block *memory, unsigned int size, const char *name)
{
	void *base = small_target_memory(size);

	if (base)
	{
		memory->uid = -1;
		memory->base = base;
		memory->size = size;
		return base;
	}
	return block_allocate(memory, SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, 1, name);
}

unsigned long vgxm_target_create(unsigned long width, unsigned long height, int depth, struct vgxm_texture *texture)
{
	struct target *target;
	int result;

	{
		/* (debug) HALO_TARGET_LIMIT=n: fewer targets, to exercise the
		recycling of unused ones (d3d8_gxm.c render_target_recycle) */
		static int limit = -1;

		if (limit < 0)
		{
			const char *setting = getenv("HALO_TARGET_LIMIT");

			limit = setting && atoi(setting) > 0 && atoi(setting) < MAXIMUM_TARGETS ? atoi(setting) : MAXIMUM_TARGETS;
		}
		if (!gxm.ready || gxm.target_count >= (unsigned)limit || !width || !height)
			return 0;
	}
	if (getenv("HALO_TRACE_FILES"))
		log_line("trace: target %lux%lu depth %d (%u made)", width, height, depth, gxm.target_count);
	target = &gxm.targets[gxm.target_count];
	memset(target, 0, sizeof(*target));
	target->depth = depth;
	{
		/* HALO_RENDER_SCALE=<0.5..1>: the screen-sized targets (480 lines:
		the back buffer, its depth, the screen effects' copies) are made at
		that fraction of the size, for a GPU that cannot fill 848x480 in a
		frame; the blit to the display scales the picture up */
		static float render_scale = -1.0f;

		if (render_scale < 0.0f)
		{
			const char *setting = getenv("HALO_RENDER_SCALE");

			render_scale = setting ? (float)atof(setting) : 1.0f;
			if (render_scale < 0.5f || render_scale > 1.0f)
				render_scale = 1.0f;
			if (render_scale < 1.0f)
				log_line("gxm: screen-sized targets at %.0f%% (HALO_RENDER_SCALE)", render_scale * 100.0f);
		}
		if (render_scale < 1.0f && height == 480 && width >= 640)
		{
			width = (unsigned long)(width * render_scale + 0.5f) & ~1UL;
			height = (unsigned long)(height * render_scale + 0.5f) & ~1UL;
			target->scale = render_scale;
		}
	}
	target->width = (unsigned int)width;
	target->height = (unsigned int)height;
	target->render_target = render_target_for(target->width, target->height);
	if (!target->render_target)
		return 0;
	if (depth)
	{
		unsigned int aligned_width = ALIGN(target->width, SCE_GXM_TILE_SIZEX);
		unsigned int aligned_height = ALIGN(target->height, SCE_GXM_TILE_SIZEY);

		if (!block_allocate(&target->memory, SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 4 * aligned_width * aligned_height, 1,
			"depth target"))
		{
			/* (the render target object is not kept for a retry: each
			failed attempt, every 30 frames, leaked one, and the driver's
			memory for them ran out - "cannot create a 128x128 render
			target: 0x805b0027") */
			sceGxmDestroyRenderTarget(target->render_target);
			return 0;
		}
		result = sceGxmDepthStencilSurfaceInit(&target->depth_stencil, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
			SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, aligned_width, target->memory.base, NULL);
		if (result < 0)
		{
			log_line("gxm: depth surface %lux%lu: 0x%08x", width, height, (unsigned)result);
			return 0;
		}
		/* keep the depth across scenes */
		sceGxmDepthStencilSurfaceSetForceLoadMode(&target->depth_stencil, SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_ENABLED);
		sceGxmDepthStencilSurfaceSetForceStoreMode(&target->depth_stencil, SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED);
	}
	else
	{
		target->stride = ALIGN(target->width, 32);
		if (!colour_target_memory(&target->memory, 4 * target->stride * target->height, "colour target"))
		{
			sceGxmDestroyRenderTarget(target->render_target);
			return 0;
		}
		memset(target->memory.base, 0, 4 * target->stride * target->height);
		result = sceGxmColorSurfaceInit(&target->color, SCE_GXM_COLOR_FORMAT_A8R8G8B8, SCE_GXM_COLOR_SURFACE_LINEAR,
			SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, target->width, target->height,
			target->stride, target->memory.base);
		if (result < 0)
		{
			log_line("gxm: colour surface %lux%lu: 0x%08x", width, height, (unsigned)result);
			return 0;
		}
		if (texture)
		{
			result = sceGxmTextureInitLinearStrided((SceGxmTexture *)texture, target->memory.base,
				SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB, target->width, target->height, target->stride * 4);
			if (result < 0)
				log_line("gxm: target texture %lux%lu: 0x%08x", width, height, (unsigned)result);
		}
	}
	return ++gxm.target_count;
}

int vgxm_target_create_chain(unsigned long width, unsigned long height, unsigned long levels,
	unsigned long *ids, struct vgxm_texture *texture)
{
	struct block chain;
	unsigned int size = 0, offset = 0, level;
	int result;

	if (!gxm.ready || !levels || gxm.target_count + levels > MAXIMUM_TARGETS)
		return -1;
	for (level = 0; level < levels; level++)
	{
		unsigned int level_width = width >> level ? width >> level : 1, level_height = height >> level ? height >> level : 1;

		size += 4 * ALIGN(level_width, 8) * level_height;
	}
	if (!colour_target_memory(&chain, size, "colour target chain"))
		return -1;
	memset(chain.base, 0, size);
	for (level = 0; level < levels; level++)
	{
		unsigned int level_width = width >> level ? width >> level : 1, level_height = height >> level ? height >> level : 1;
		struct target *target = &gxm.targets[gxm.target_count];

		memset(target, 0, sizeof(*target));
		target->width = level_width;
		target->height = level_height;
		target->stride = ALIGN(level_width, 8);
		target->render_target = render_target_for(target->width, target->height);
		/* (the chain's block is the first level's) */
		target->memory.base = (unsigned char *)chain.base + offset;
		target->memory.size = 4 * target->stride * level_height;
		if (!level)
			target->memory.uid = chain.uid;
		if (!target->render_target)
			return -1;
		result = sceGxmColorSurfaceInit(&target->color, SCE_GXM_COLOR_FORMAT_A8R8G8B8, SCE_GXM_COLOR_SURFACE_LINEAR,
			SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, target->width, target->height,
			target->stride, target->memory.base);
		if (result < 0)
		{
			log_line("gxm: chained colour surface %ux%u (level %u): 0x%08x", level_width, level_height, level, (unsigned)result);
			return -1;
		}
		ids[level] = ++gxm.target_count;
		offset += 4 * target->stride * level_height;
	}
	{
		/* (debug) HALO_CHAIN_BASE_LEVEL=n: the texture starts at level n
		(to tell aliasing from a missing mip chain) */
		const char *setting = getenv("HALO_CHAIN_BASE_LEVEL");
		unsigned int base_level = setting ? (unsigned int)atoi(setting) : 0, skip = 0;

		if (base_level >= levels)
			base_level = 0;
		for (level = 0; level < base_level; level++)
			skip += 4 * ALIGN(width >> level, 8) * (height >> level);
		result = sceGxmTextureInitLinear((SceGxmTexture *)texture, (unsigned char *)chain.base + skip,
			SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB, width >> base_level, height >> base_level, levels - base_level);
	}
	if (result < 0)
	{
		log_line("gxm: chained target texture %lux%lu, %lu levels: 0x%08x", width, height, levels, (unsigned)result);
		return -1;
	}
	log_line("gxm: a %lux%lu colour target with %lu levels (%u KB)", width, height, levels, size / 1024);
	return 0;
}

void vgxm_set_targets(unsigned long color, unsigned long depth)
{
	gxm.wanted_color = color;
	gxm.wanted_depth = depth;
}

static void shadow_invalidate(void);

/* begins a scene for the wanted targets if the current one is not for them */
static unsigned int *visibility_buffer(unsigned int ring)
{
	return (unsigned int *)((unsigned char *)gxm.visibility.base + ring * VISIBILITY_CORES * VISIBILITY_CORE_STRIDE);
}

static int scene_ensure(void)
{
	struct target *color, *depth;
	unsigned int width, height;
	int result;

	if (gxm.in_scene && gxm.scene_color == gxm.wanted_color && gxm.scene_depth == gxm.wanted_depth)
	{
		/* HALO_GXM_SCENE_DRAWS (default 300): a scene with this many draws
		is ended and begun again on the same targets, so its primitives fit
		the parameter buffer; an overflowing scene falls into partial
		renders (~75 ms/frame in a fight at 848x480, 1-4 ms when it fits).
		The depth is kept across scenes (force load/store), the colour is */
		static int scene_draw_limit = -1;

		if (scene_draw_limit < 0)
		{
			const char *setting = getenv("HALO_GXM_SCENE_DRAWS");
			scene_draw_limit = setting ? atoi(setting) : 300;
		}
		if (scene_draw_limit <= 0 || gxm.scene_draws < (unsigned int)scene_draw_limit)
			return 1;
		gxm_scene_splits++;
	}
	if (gxm.in_scene)
	{
		unsigned long long before = sceKernelGetProcessTimeWide();

		sceGxmEndScene(gxm.context, NULL, NULL);
		scene_switch_us[gxm.scene_color == 1 ? 0 : 1] += sceKernelGetProcessTimeWide() - before;
		gxm.in_scene = 0;
	}
	color = gxm.wanted_color ? &gxm.targets[gxm.wanted_color - 1] : NULL;
	depth = gxm.wanted_depth ? &gxm.targets[gxm.wanted_depth - 1] : NULL;
	if (!color && !depth)
		return 0;
	width = color ? color->width : depth->width;
	height = color ? color->height : depth->height;
	/* a depth target smaller than the colour target cannot serve it */
	if (color && depth && (depth->width < color->width || depth->height < color->height))
		depth = NULL;
	if (gxm.visibility.base && gxm.visibility_bound != (int)gxm.worker_ring_index)
	{
		/* (set between scenes: the frame's tests count into its ring's
		buffer) */
		gxm.visibility_bound = (int)gxm.worker_ring_index;
		sceGxmSetVisibilityBuffer(gxm.context, visibility_buffer(gxm.worker_ring_index), VISIBILITY_CORE_STRIDE);
	}
	{
		unsigned long long before = sceKernelGetProcessTimeWide();

		result = sceGxmBeginScene(gxm.context, 0, color ? color->render_target : depth->render_target, NULL, NULL, NULL,
			color ? &color->color : NULL, depth ? &depth->depth_stencil : NULL);
		scene_switch_us[gxm.wanted_color == 1 ? 0 : 1] += sceKernelGetProcessTimeWide() - before;
	}
	if (result < 0)
	{
		static unsigned int reported;

		if (reported++ < 16)
			log_line("gxm: cannot begin a %ux%u scene: 0x%08x", width, height, (unsigned)result);
		return 0;
	}
	gxm.in_scene = 1;
	gxm.scene_draws = 0;
	gxm_scene_count++;
	{
		/* which targets the scenes are for (the report at present) */
		unsigned int slot = (unsigned int)(gxm.wanted_color ? gxm.wanted_color : gxm.wanted_depth + 64) % 128;

		scene_histogram[slot]++;
	}
	shadow_invalidate();
	gxm.scene_color = gxm.wanted_color;
	gxm.scene_depth = gxm.wanted_depth;
	sceGxmSetViewportEnable(gxm.context, SCE_GXM_VIEWPORT_ENABLED);
	return 1;
}

/* ---------- state */

static SceGxmBlendFactor blend_factor(unsigned long factor)
{
	/* HALO_DSTCOLOR_AS_ONE=1: the colour factors (DST_COLOR, SRC_COLOR)
	stand in as ONE, to measure whether the factor itself is what the
	multiplicative passes cost the GPU (the image is wrong) */
	static int dstcolor_as_one = -1;

	if (dstcolor_as_one < 0)
	{
		const char *setting = getenv("HALO_DSTCOLOR_AS_ONE");
		dstcolor_as_one = setting && atoi(setting) != 0;
	}
	if (dstcolor_as_one && (factor == 774 || factor == 768 || factor == 769))
		return SCE_GXM_BLEND_FACTOR_ONE;
	switch (factor)
	{
	case 0: return SCE_GXM_BLEND_FACTOR_ZERO;
	case 1: return SCE_GXM_BLEND_FACTOR_ONE;
	case 768: return SCE_GXM_BLEND_FACTOR_SRC_COLOR;
	case 769: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	case 770: return SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
	case 771: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case 772: return SCE_GXM_BLEND_FACTOR_DST_ALPHA;
	case 773: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	case 774: return SCE_GXM_BLEND_FACTOR_DST_COLOR;
	case 775: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	case 776: return SCE_GXM_BLEND_FACTOR_SRC_ALPHA_SATURATE;
	/* GXM has no constant colour factors */
	default: return SCE_GXM_BLEND_FACTOR_ONE;
	}
}

static SceGxmBlendFunc blend_function(unsigned long operation)
{
	switch (operation)
	{
	case 32778: return SCE_GXM_BLEND_FUNC_SUBTRACT;
	case 32779: case 61445: return SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT;
	case 32775: return SCE_GXM_BLEND_FUNC_MIN;
	case 32776: return SCE_GXM_BLEND_FUNC_MAX;
	default: return SCE_GXM_BLEND_FUNC_ADD;
	}
}

static uint8_t color_mask(unsigned long write)
{
	return (uint8_t)(((write & (1UL << 16)) ? SCE_GXM_COLOR_MASK_R : 0) | ((write & (1UL << 8)) ? SCE_GXM_COLOR_MASK_G : 0) |
		((write & 1UL) ? SCE_GXM_COLOR_MASK_B : 0) | ((write & (1UL << 24)) ? SCE_GXM_COLOR_MASK_A : 0));
}

/* a Direct3D comparison (D3DCMP_*, 0x200 + GL's order) as GXM's */
static unsigned int comparison(unsigned long function, unsigned int shift)
{
	return ((unsigned int)(function ? function : 0x200) & 7) << shift;
}

static SceGxmStencilOp stencil_operation(unsigned long operation)
{
	switch (operation)
	{
	case 0: return SCE_GXM_STENCIL_OP_ZERO;
	case 7681: return SCE_GXM_STENCIL_OP_REPLACE;
	case 7682: return SCE_GXM_STENCIL_OP_INCR;
	case 7683: return SCE_GXM_STENCIL_OP_DECR;
	case 5386: return SCE_GXM_STENCIL_OP_INVERT;
	case 34055: return SCE_GXM_STENCIL_OP_INCR_WRAP;
	case 34056: return SCE_GXM_STENCIL_OP_DECR_WRAP;
	default: return SCE_GXM_STENCIL_OP_KEEP;
	}
}

static SceGxmPrimitiveType primitive_type(unsigned long primitive)
{
	switch (primitive)
	{
	case D3DPT_POINTLIST: return SCE_GXM_PRIMITIVE_POINTS;
	case D3DPT_LINELIST: return SCE_GXM_PRIMITIVE_LINES;
	case D3DPT_TRIANGLESTRIP: return SCE_GXM_PRIMITIVE_TRIANGLE_STRIP;
	case D3DPT_TRIANGLEFAN: return SCE_GXM_PRIMITIVE_TRIANGLE_FAN;
	default: return SCE_GXM_PRIMITIVE_TRIANGLES;
	}
}

/* the render scale of the scene's target (1 for most) */
static float scene_scale(void)
{
	const struct target *target = gxm.scene_color ? &gxm.targets[gxm.scene_color - 1] :
		gxm.scene_depth ? &gxm.targets[gxm.scene_depth - 1] : NULL;

	return target && target->scale > 0.0f ? target->scale : 1.0f;
}

static void set_clip(const long unscaled[4])
{
	float scale = scene_scale();
	long clip[4];
	long x0, y0, x1, y1;

	if (scale != 1.0f)
	{
		clip[0] = (long)(unscaled[0] * scale);
		clip[1] = (long)(unscaled[1] * scale);
		clip[2] = (long)(unscaled[2] * scale + 0.999f);
		clip[3] = (long)(unscaled[3] * scale + 0.999f);
	}
	else
		memcpy(clip, unscaled, sizeof(clip));
	x0 = clip[0] < 0 ? 0 : clip[0];
	y0 = clip[1] < 0 ? 0 : clip[1];
	x1 = clip[2];
	y1 = clip[3];

	if (x1 <= x0 || y1 <= y0)
		sceGxmSetRegionClip(gxm.context, SCE_GXM_REGION_CLIP_ALL, 0, 0, 0, 0);
	else
		sceGxmSetRegionClip(gxm.context, SCE_GXM_REGION_CLIP_OUTSIDE, (unsigned int)x0, (unsigned int)y0,
			(unsigned int)x1 - 1, (unsigned int)y1 - 1);
}

/* ---------- the context's state, set only when it changes */

static struct
{
	const SceGxmVertexProgram *vertex_program;
	const SceGxmFragmentProgram *fragment_program;
	const void *streams[VGXM_STREAM_COUNT];
	const void *vertex_chunks[6], *vertex_uniforms, *fragment_uniforms[2];
	unsigned long textures[4][4];
	int texture_set[4];
	SceGxmDepthFunc depth_function;
	int depth_write;
	unsigned long stencil[7];
	int cull;
	int bias[2];
	float viewport[6];
	long clip[4];
	float clip_scale;
	int valid;
} shadow;

static void shadow_invalidate(void)
{
	memset(&shadow, 0, sizeof(shadow));
	shadow.valid = 1;
	shadow.depth_function = (SceGxmDepthFunc)0xffffffffu;
	shadow.depth_write = -1;
	shadow.cull = -1;
	shadow.bias[0] = shadow.bias[1] = 0x7fffffff;
	memset(shadow.stencil, 0xff, sizeof(shadow.stencil));
	memset(shadow.viewport, 0xff, sizeof(shadow.viewport));
	memset(shadow.clip, 0xff, sizeof(shadow.clip));
}

void vgxm_draw(const struct vgxm_draw *draw)
{
	struct vertex_program_key key;
	SceGxmVertexProgram *vertex_program;
	SceGxmFragmentProgram *fragment_program;
	SceGxmBlendInfo blend;
	const SceGxmBlendInfo *blend_info = NULL;
	const struct shader *fragment_shader;
	unsigned int index;

	if (!gxm.ready || !draw->index_count || !scene_ensure())
		return;
	memset(&key, 0, sizeof(key));
	key.shader = draw->vertex_shader;
	key.attribute_count = draw->attribute_count;
	memcpy(key.attributes, draw->attributes, sizeof(key.attributes[0]) * draw->attribute_count);
	key.stream_count = draw->stream_count;
	memcpy(key.strides, draw->strides, sizeof(key.strides[0]) * draw->stream_count);
	vertex_program = vertex_program_get(&key);
	if (draw->blend || color_mask(draw->color_write) != SCE_GXM_COLOR_MASK_ALL)
	{
		memset(&blend, 0, sizeof(blend));
		blend.colorMask = color_mask(draw->color_write);
		if (draw->blend)
		{
			blend.colorFunc = blend.alphaFunc = blend_function(draw->blend_operation);
			blend.colorSrc = blend.alphaSrc = blend_factor(draw->blend_source);
			blend.colorDst = blend.alphaDst = blend_factor(draw->blend_destination);
		}
		else
		{
			blend.colorFunc = blend.alphaFunc = SCE_GXM_BLEND_FUNC_NONE;
			blend.colorSrc = blend.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE;
			blend.colorDst = blend.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;
		}
		blend_info = &blend;
	}
	fragment_program = fragment_program_get(draw->fragment_shader, draw->vertex_shader, blend_info);
	if (!vertex_program || !fragment_program)
		return;
	if (!shadow.valid)
		shadow_invalidate();
	if (shadow.vertex_program != vertex_program)
	{
		shadow.vertex_program = vertex_program;
		sceGxmSetVertexProgram(gxm.context, vertex_program);
	}
	if (shadow.fragment_program != fragment_program)
	{
		shadow.fragment_program = fragment_program;
		sceGxmSetFragmentProgram(gxm.context, fragment_program);
	}
	for (index = 0; index < draw->stream_count; index++)
	{
		if (shadow.streams[index] != draw->streams[index])
		{
			shadow.streams[index] = draw->streams[index];
			sceGxmSetVertexStream(gxm.context, index, draw->streams[index]);
		}
	}
	{
		/* the constant chunks' BUFFER indices (vita_xgpu.h VITA_VC_BUFFER) */
		static const unsigned int chunk_buffer[6] = { 0, 2, 3, 4, 5, 6 };
		unsigned int chunk;

		for (chunk = 0; chunk < 6; chunk++)
		{
			if (draw->vertex_chunks[chunk] && shadow.vertex_chunks[chunk] != draw->vertex_chunks[chunk])
			{
				shadow.vertex_chunks[chunk] = draw->vertex_chunks[chunk];
				sceGxmSetVertexUniformBuffer(gxm.context, chunk_buffer[chunk], draw->vertex_chunks[chunk]);
			}
		}
	}
	if (shadow.vertex_uniforms != draw->vertex_uniforms)
	{
		shadow.vertex_uniforms = draw->vertex_uniforms;
		sceGxmSetVertexUniformBuffer(gxm.context, 1, draw->vertex_uniforms);
	}
	for (index = 0; index < 2; index++)
	{
		if (shadow.fragment_uniforms[index] != draw->fragment_uniforms[index])
		{
			shadow.fragment_uniforms[index] = draw->fragment_uniforms[index];
			sceGxmSetFragmentUniformBuffer(gxm.context, index, draw->fragment_uniforms[index]);
		}
	}
	fragment_shader = &gxm.shaders[draw->fragment_shader - 1];
	for (index = 0; index < 4; index++)
	{
		int slot = fragment_shader->sampler_index[index];

		if (slot < 0 || !draw->textures[index])
			continue;
		/* (the device declares a sampler only for a stage with a texture) */
		if (!shadow.texture_set[slot] || memcmp(shadow.textures[slot], draw->textures[index]->control, 16))
		{
			memcpy(shadow.textures[slot], draw->textures[index]->control, 16);
			shadow.texture_set[slot] = 1;
			sceGxmSetFragmentTexture(gxm.context, (unsigned int)slot, (const SceGxmTexture *)draw->textures[index]);
		}
	}

	{
		SceGxmDepthFunc depth_function = draw->depth_test ? (SceGxmDepthFunc)comparison(draw->depth_function, 22) :
			SCE_GXM_DEPTH_FUNC_ALWAYS;
		int depth_write = draw->depth_write ? 1 : 0;
		unsigned long stencil[7];
		int cull = draw->cull == D3DCULL_CW ? SCE_GXM_CULL_CW : draw->cull == D3DCULL_CCW ? SCE_GXM_CULL_CCW : SCE_GXM_CULL_NONE;
		int bias[2];

		if (shadow.depth_function != depth_function)
		{
			shadow.depth_function = depth_function;
			sceGxmSetFrontDepthFunc(gxm.context, depth_function);
		}
		if (shadow.depth_write != depth_write)
		{
			shadow.depth_write = depth_write;
			sceGxmSetFrontDepthWriteEnable(gxm.context, depth_write ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
		}
		if (draw->stencil_test)
		{
			stencil[0] = comparison(draw->stencil_function, 25);
			stencil[1] = stencil_operation(draw->stencil_fail);
			stencil[2] = stencil_operation(draw->stencil_depth_fail);
			stencil[3] = stencil_operation(draw->stencil_pass);
			stencil[4] = draw->stencil_read_mask & 0xff;
			stencil[5] = draw->stencil_write_mask & 0xff;
			stencil[6] = draw->stencil_reference & 0xff;
		}
		else
		{
			stencil[0] = SCE_GXM_STENCIL_FUNC_ALWAYS;
			stencil[1] = stencil[2] = stencil[3] = SCE_GXM_STENCIL_OP_KEEP;
			stencil[4] = stencil[5] = stencil[6] = 0;
		}
		if (memcmp(shadow.stencil, stencil, sizeof(stencil)))
		{
			memcpy(shadow.stencil, stencil, sizeof(stencil));
			sceGxmSetFrontStencilFunc(gxm.context, (SceGxmStencilFunc)stencil[0], (SceGxmStencilOp)stencil[1],
				(SceGxmStencilOp)stencil[2], (SceGxmStencilOp)stencil[3], (unsigned char)stencil[4], (unsigned char)stencil[5]);
			sceGxmSetFrontStencilRef(gxm.context, (unsigned int)stencil[6]);
		}
		if (shadow.cull != cull)
		{
			shadow.cull = cull;
			sceGxmSetCullMode(gxm.context, (SceGxmCullMode)cull);
		}
		bias[0] = (int)draw->depth_bias_slope;
		bias[1] = (int)draw->depth_bias_units;
		if (shadow.bias[0] != bias[0] || shadow.bias[1] != bias[1])
		{
			shadow.bias[0] = bias[0];
			shadow.bias[1] = bias[1];
			sceGxmSetFrontDepthBias(gxm.context, bias[0], bias[1]);
		}
		{
			/* (the viewport in the target's pixels: scaled with it) */
			float scale = scene_scale();
			float viewport[6];

			viewport[0] = draw->viewport_offset[0] * scale;
			viewport[1] = draw->viewport_offset[1] * scale;
			viewport[2] = draw->viewport_offset[2];
			viewport[3] = draw->viewport_scale[0] * scale;
			viewport[4] = draw->viewport_scale[1] * scale;
			viewport[5] = draw->viewport_scale[2];
			if (memcmp(shadow.viewport, viewport, sizeof(viewport)))
			{
				memcpy(shadow.viewport, viewport, sizeof(viewport));
				sceGxmSetViewport(gxm.context, viewport[0], viewport[3], viewport[1], viewport[4], viewport[2], viewport[5]);
			}
			if (memcmp(shadow.clip, draw->clip, sizeof(shadow.clip)) || shadow.clip_scale != scale)
			{
				memcpy(shadow.clip, draw->clip, sizeof(shadow.clip));
				shadow.clip_scale = scale;
				set_clip(draw->clip);
			}
		}
	}
	if (draw->visibility_index && draw->visibility_index < VGXM_VISIBILITY_SLOTS && gxm.visibility.base)
	{
		/* a visibility test's draw (a lens flare's occlusion quad): its
		samples that pass count into its slot */
		unsigned int ring = gxm.worker_ring_index;

		sceGxmSetFrontVisibilityTestIndex(gxm.context, (unsigned int)draw->visibility_index);
		sceGxmSetBackVisibilityTestIndex(gxm.context, (unsigned int)draw->visibility_index);
		sceGxmSetFrontVisibilityTestOp(gxm.context, SCE_GXM_VISIBILITY_TEST_OP_INCREMENT);
		sceGxmSetBackVisibilityTestOp(gxm.context, SCE_GXM_VISIBILITY_TEST_OP_INCREMENT);
		sceGxmSetFrontVisibilityTestEnable(gxm.context, SCE_GXM_VISIBILITY_TEST_ENABLED);
		sceGxmSetBackVisibilityTestEnable(gxm.context, SCE_GXM_VISIBILITY_TEST_ENABLED);
		sceGxmDraw(gxm.context, primitive_type(draw->primitive), SCE_GXM_INDEX_FORMAT_U16, draw->indices,
			(unsigned int)draw->index_count);
		/* (off again at once: no other draw, clear or blit counts) */
		sceGxmSetFrontVisibilityTestEnable(gxm.context, SCE_GXM_VISIBILITY_TEST_DISABLED);
		sceGxmSetBackVisibilityTestEnable(gxm.context, SCE_GXM_VISIBILITY_TEST_DISABLED);
		gxm.visibility_scale[ring] = scene_scale();
		if (draw->visibility_index > gxm.visibility_slots_used[ring])
			gxm.visibility_slots_used[ring] = (unsigned int)draw->visibility_index;
		gxm.scene_draws++;
		return;
	}
	sceGxmDraw(gxm.context, primitive_type(draw->primitive), SCE_GXM_INDEX_FORMAT_U16, draw->indices,
		(unsigned int)draw->index_count);
	gxm.scene_draws++;
}

void vgxm_clear(unsigned long flags, unsigned long color, float depth, unsigned long stencil, const long clip[4])
{
	struct vertex_program_key key;
	SceGxmVertexProgram *vertex_program;
	SceGxmFragmentProgram *fragment_program;
	SceGxmBlendInfo blend;
	struct target *target;
	float *vertices, *uniforms;
	unsigned short *indices;
	unsigned int width, height;
	uint8_t mask = 0;

	if (!gxm.ready || !scene_ensure())
		return;
	target = gxm.scene_color ? &gxm.targets[gxm.scene_color - 1] : &gxm.targets[gxm.scene_depth - 1];
	width = target->width;
	height = target->height;
	if (flags & D3DCLEAR_TARGET_R) mask |= SCE_GXM_COLOR_MASK_R;
	if (flags & D3DCLEAR_TARGET_G) mask |= SCE_GXM_COLOR_MASK_G;
	if (flags & D3DCLEAR_TARGET_B) mask |= SCE_GXM_COLOR_MASK_B;
	if (flags & D3DCLEAR_TARGET_A) mask |= SCE_GXM_COLOR_MASK_A;
	if (!gxm.scene_color)
		mask = 0;
	if (!mask && !(flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)))
		return;
	vertices = vgxm_worker_alloc(4 * 3 * sizeof(float), 16);
	uniforms = vgxm_worker_alloc(4 * sizeof(float), 16);
	indices = vgxm_worker_alloc(4 * sizeof(unsigned short), 16);
	if (!vertices || !uniforms || !indices)
		return;
	{
		float x0 = 2.0f * (float)clip[0] / (float)width - 1.0f, x1 = 2.0f * (float)clip[2] / (float)width - 1.0f;
		float y0 = 1.0f - 2.0f * (float)clip[1] / (float)height, y1 = 1.0f - 2.0f * (float)clip[3] / (float)height;
		float corners[4][3] = { { x0, y0, depth }, { x1, y0, depth }, { x0, y1, depth }, { x1, y1, depth } };

		memcpy(vertices, corners, sizeof(corners));
	}
	uniforms[0] = ((color >> 16) & 0xff) / 255.0f;
	uniforms[1] = ((color >> 8) & 0xff) / 255.0f;
	uniforms[2] = (color & 0xff) / 255.0f;
	uniforms[3] = ((color >> 24) & 0xff) / 255.0f;
	indices[0] = 0; indices[1] = 1; indices[2] = 2; indices[3] = 3;

	memset(&key, 0, sizeof(key));
	key.shader = gxm.clear_vertex;
	key.attribute_count = 1;
	key.attributes[0].reg = 0xff;
	key.stream_count = 1;
	key.strides[0] = 3 * sizeof(float);
	{
		/* the clear program's input is named position */
		static SceGxmVertexProgram *program;

		if (!program)
		{
			const struct shader *shader = &gxm.shaders[gxm.clear_vertex - 1];
			const SceGxmProgramParameter *parameter = sceGxmProgramFindParameterByName(shader->program, "position");
			SceGxmVertexAttribute attribute;
			SceGxmVertexStream stream;

			attribute.streamIndex = 0;
			attribute.offset = 0;
			attribute.format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
			attribute.componentCount = 3;
			attribute.regIndex = (uint16_t)sceGxmProgramParameterGetResourceIndex(parameter);
			stream.stride = 3 * sizeof(float);
			stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
			sceGxmShaderPatcherCreateVertexProgram(gxm.patcher, shader->id, &attribute, 1, &stream, 1, &program);
		}
		vertex_program = program;
	}
	(void)key;
	memset(&blend, 0, sizeof(blend));
	blend.colorMask = mask;
	blend.colorFunc = blend.alphaFunc = SCE_GXM_BLEND_FUNC_NONE;
	blend.colorSrc = blend.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE;
	blend.colorDst = blend.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;
	fragment_program = fragment_program_get(gxm.clear_fragment, gxm.clear_vertex, &blend);
	if (!vertex_program || !fragment_program)
		return;
	shadow.valid = 0;
	sceGxmSetVertexProgram(gxm.context, vertex_program);
	sceGxmSetFragmentProgram(gxm.context, fragment_program);
	sceGxmSetVertexStream(gxm.context, 0, vertices);
	sceGxmSetFragmentUniformBuffer(gxm.context, 0, uniforms);
	sceGxmSetFrontDepthFunc(gxm.context, SCE_GXM_DEPTH_FUNC_ALWAYS);
	sceGxmSetFrontDepthWriteEnable(gxm.context, (flags & D3DCLEAR_ZBUFFER) ? SCE_GXM_DEPTH_WRITE_ENABLED :
		SCE_GXM_DEPTH_WRITE_DISABLED);
	if (flags & D3DCLEAR_STENCIL)
	{
		sceGxmSetFrontStencilFunc(gxm.context, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_REPLACE,
			SCE_GXM_STENCIL_OP_REPLACE, SCE_GXM_STENCIL_OP_REPLACE, 0xff, 0xff);
		sceGxmSetFrontStencilRef(gxm.context, stencil & 0xff);
	}
	else
	{
		sceGxmSetFrontStencilFunc(gxm.context, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP,
			SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0, 0);
	}
	sceGxmSetCullMode(gxm.context, SCE_GXM_CULL_NONE);
	sceGxmSetFrontDepthBias(gxm.context, 0, 0);
	sceGxmSetViewport(gxm.context, width * 0.5f, width * 0.5f, height * 0.5f, -(float)height * 0.5f, 0.0f, 1.0f);
	set_clip(clip);
	sceGxmDraw(gxm.context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, indices, 4);
	gxm.scene_draws++;
}

void vgxm_visibility_frame(unsigned long frame)
{
	gxm.visibility_next_game_frame = frame;
}

/* (the game's thread) the newest buffer whose frame the GPU has finished:
the game asks at the start of the next frame, when that frame is still
ahead of the GPU, and takes a result a frame or two old rather than wait */
int vgxm_visibility_newest(unsigned long *frame)
{
	unsigned int ring, newest_frame = 0, completed = *gxm.notification;
	int newest = -1;

	if (!gxm.visibility.base)
		return -1;
	for (ring = 0; ring < RING_COUNT; ring++)
	{
		unsigned int gpu_frame = gxm.visibility_frame[ring];

		if (gpu_frame && (int)(completed - gpu_frame) >= 0 && (newest < 0 || (int)(gpu_frame - newest_frame) > 0))
		{
			newest = (int)ring;
			newest_frame = gpu_frame;
		}
	}
	if (newest >= 0)
		*frame = gxm.visibility_game_frame[newest];
	return newest;
}

unsigned long vgxm_visibility_count(int buffer, unsigned long slot)
{
	const unsigned int *counts;
	unsigned long samples = 0;
	unsigned int core;
	float scale;

	if (!gxm.visibility.base || buffer < 0 || buffer >= RING_COUNT || slot >= VGXM_VISIBILITY_SLOTS)
		return 0;
	counts = visibility_buffer((unsigned int)buffer);
	for (core = 0; core < VISIBILITY_CORES; core++)
		samples += counts[core * (VISIBILITY_CORE_STRIDE / 4) + slot];
	/* (samples of a scaled target: the game counts its own pixels) */
	scale = gxm.visibility_scale[buffer];
	if (scale > 0.0f && scale < 1.0f)
		samples = (unsigned long)(samples / (scale * scale) + 0.5f);
	return samples;
}

/* ---------- frames */

static void blit(struct target *source)
{
	static SceGxmVertexProgram *vertex_program;
	SceGxmFragmentProgram *fragment_program;
	SceGxmTexture texture;
	float *vertices;
	unsigned short *indices;
	float width, height, x0, x1;

	if (!vertex_program)
	{
		const struct shader *shader = &gxm.shaders[gxm.blit_vertex - 1];
		SceGxmVertexAttribute attributes[2];
		SceGxmVertexStream stream;

		attributes[0].streamIndex = 0;
		attributes[0].offset = 0;
		attributes[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		attributes[0].componentCount = 2;
		attributes[0].regIndex = (uint16_t)sceGxmProgramParameterGetResourceIndex(
			sceGxmProgramFindParameterByName(shader->program, "position"));
		attributes[1].streamIndex = 0;
		attributes[1].offset = 2 * sizeof(float);
		attributes[1].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		attributes[1].componentCount = 2;
		attributes[1].regIndex = (uint16_t)sceGxmProgramParameterGetResourceIndex(
			sceGxmProgramFindParameterByName(shader->program, "texcoord"));
		stream.stride = 4 * sizeof(float);
		stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
		if (sceGxmShaderPatcherCreateVertexProgram(gxm.patcher, shader->id, attributes, 2, &stream, 1, &vertex_program) < 0)
			return;
	}
	fragment_program = fragment_program_get(gxm.blit_fragment, gxm.blit_vertex, NULL);
	vertices = vgxm_worker_alloc(4 * 4 * sizeof(float), 16);
	indices = vgxm_worker_alloc(4 * sizeof(unsigned short), 16);
	if (!fragment_program || !vertices || !indices)
		return;
	/* the picture at the display's height, its shape kept */
	height = (float)DISPLAY_HEIGHT;
	width = height * (float)source->width / (float)source->height;
	if (width > DISPLAY_WIDTH)
		width = DISPLAY_WIDTH;
	x0 = -width / DISPLAY_WIDTH;
	x1 = width / DISPLAY_WIDTH;
	{
		float quad[4][4] = {
			{ x0, 1.0f, 0.0f, 0.0f }, { x1, 1.0f, 1.0f, 0.0f },
			{ x0, -1.0f, 0.0f, 1.0f }, { x1, -1.0f, 1.0f, 1.0f },
		};

		memcpy(vertices, quad, sizeof(quad));
	}
	indices[0] = 0; indices[1] = 1; indices[2] = 2; indices[3] = 3;
	sceGxmTextureInitLinearStrided(&texture, source->memory.base, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB, source->width,
		source->height, source->stride * 4);
	sceGxmTextureSetMinFilter(&texture, SCE_GXM_TEXTURE_FILTER_LINEAR);
	sceGxmTextureSetMagFilter(&texture, SCE_GXM_TEXTURE_FILTER_LINEAR);
	shadow.valid = 0;
	sceGxmSetVertexProgram(gxm.context, vertex_program);
	sceGxmSetFragmentProgram(gxm.context, fragment_program);
	sceGxmSetVertexStream(gxm.context, 0, vertices);
	sceGxmSetFragmentTexture(gxm.context, 0, &texture);
	sceGxmSetFrontDepthFunc(gxm.context, SCE_GXM_DEPTH_FUNC_ALWAYS);
	sceGxmSetFrontDepthWriteEnable(gxm.context, SCE_GXM_DEPTH_WRITE_DISABLED);
	sceGxmSetFrontStencilFunc(gxm.context, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP,
		SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0, 0);
	sceGxmSetCullMode(gxm.context, SCE_GXM_CULL_NONE);
	sceGxmSetFrontDepthBias(gxm.context, 0, 0);
	sceGxmSetViewportEnable(gxm.context, SCE_GXM_VIEWPORT_ENABLED);
	sceGxmSetViewport(gxm.context, DISPLAY_WIDTH * 0.5f, DISPLAY_WIDTH * 0.5f, DISPLAY_HEIGHT * 0.5f,
		-DISPLAY_HEIGHT * 0.5f, 0.0f, 1.0f);
	sceGxmSetRegionClip(gxm.context, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
	sceGxmDraw(gxm.context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, indices, 4);
	gxm.scene_draws++;
}

/* ---------- the overlay (Xita's panel: fps, game and render ms, cores) */

struct overlay_vertex
{
	float x, y;
	unsigned char color[4];
};

static unsigned int overlay_rect(struct overlay_vertex *vertices, unsigned int count, float x, float y, float width,
	float height, uint32_t abgr)
{
	struct overlay_vertex *v = vertices + count * 6;
	float x0 = 2.0f * x / DISPLAY_WIDTH - 1.0f, x1 = 2.0f * (x + width) / DISPLAY_WIDTH - 1.0f;
	float y0 = 1.0f - 2.0f * y / DISPLAY_HEIGHT, y1 = 1.0f - 2.0f * (y + height) / DISPLAY_HEIGHT;
	int index;

	v[0].x = x0; v[0].y = y0;
	v[1].x = x1; v[1].y = y0;
	v[2].x = x0; v[2].y = y1;
	v[3].x = x1; v[3].y = y0;
	v[4].x = x1; v[4].y = y1;
	v[5].x = x0; v[5].y = y1;
	for (index = 0; index < 6; index++)
		memcpy(v[index].color, &abgr, 4);
	return count + 1;
}

/* text in the 8x8 font at scale, one rectangle per lit pixel */
static unsigned int overlay_text(struct overlay_vertex *vertices, unsigned int count, unsigned int limit, float x,
	float y, float scale, uint32_t color, const char *text)
{
	for (; *text; text++, x += 8.0f * scale)
	{
		unsigned char c = (unsigned char)*text;
		int row, column;

		if (c >= 'a' && c <= 'z')
			c = (unsigned char)(c - 'a' + 'A');
		if (c < 32 || c >= 128)
			continue;
		for (row = 0; row < 8; row++)
		{
			unsigned char bits = font[c - 32][row];

			/* (a run of lit pixels is one rectangle) */
			for (column = 0; column < 8; column++)
			{
				int run = 0;

				while (column + run < 8 && (bits & (0x80 >> (column + run))))
					run++;
				if (run && count < limit)
					count = overlay_rect(vertices, count, x + column * scale, y + row * scale, scale * run, scale, color);
				column += run;
			}
		}
	}
	return count;
}

void vgxm_overlay_enable(int enabled)
{
	gxm.overlay_enabled = enabled && gxm.overlay_programs;
}

void vgxm_menu_set(const char *text, int selected)
{
	if (!text)
	{
		gxm.menu_visible = 0;
		return;
	}
	{
		int next = !gxm.menu_index;

		strncpy(gxm.menu_text[next], text, sizeof(gxm.menu_text[next]) - 1);
		gxm.menu_text[next][sizeof(gxm.menu_text[next]) - 1] = 0;
		gxm.menu_selected = selected;
		__atomic_store_n(&gxm.menu_index, next, __ATOMIC_RELEASE);
		gxm.menu_visible = 1;
	}
}

/* the settings panel, centred: title, a row per line (the selected one on
a bar), the hint at the bottom */
static unsigned int menu_build(struct overlay_vertex *vertices, unsigned int count, unsigned int limit)
{
	const char *text = gxm.menu_text[__atomic_load_n(&gxm.menu_index, __ATOMIC_ACQUIRE)];
	const char *lines[24];
	int line_count = 0, index;
	const float width = 600.0f, row_height = 24.0f;
	float height, left, top;
	char copy[2048];
	char *cursor;

	strncpy(copy, text, sizeof(copy) - 1);
	copy[sizeof(copy) - 1] = 0;
	for (cursor = copy; cursor && line_count < 24; )
	{
		char *newline = strchr(cursor, '\n');

		lines[line_count++] = cursor;
		if (newline)
			*newline = 0;
		cursor = newline ? newline + 1 : NULL;
	}
	if (line_count < 2)
		return count;
	height = 16.0f + row_height * line_count + 8.0f;
	left = (DISPLAY_WIDTH - width) / 2.0f;
	top = (DISPLAY_HEIGHT - height) / 2.0f;
	count = overlay_rect(vertices, count, left, top, width, height, 0xE0101010u);
	count = overlay_rect(vertices, count, left, top, width, 2.0f, 0xFF40FF40u);
	for (index = 0; index < line_count; index++)
	{
		float y = top + 12.0f + row_height * index;
		uint32_t color = index == 0 ? 0xFF40FF40u : index == line_count - 1 ? 0xFFA0A0A0u : 0xFFE0E0E0u;

		if (index == gxm.menu_selected)
		{
			count = overlay_rect(vertices, count, left + 6.0f, y - 4.0f, width - 12.0f, row_height, 0xFF305030u);
			color = 0xFFFFFFFFu;
		}
		count = overlay_text(vertices, count, limit, left + 16.0f, y, index == line_count - 1 ? 1.5f : 2.0f, color,
			lines[index]);
	}
	return count;
}

void vgxm_overlay_set(float fps, float tick_ms, float render_ms)
{
	gxm.overlay_fps = fps;
	gxm.overlay_tick_ms = tick_ms;
	gxm.overlay_render_ms = render_ms;
}

static void overlay_draw(void)
{
	static SceGxmVertexProgram *vertex_program;
	SceGxmFragmentProgram *fragment_program;
	SceGxmBlendInfo blend;
	/* (built in a cached array, then copied to the worker's ring as large
	as it came out) */
	static struct overlay_vertex *built;
	struct overlay_vertex *vertices;
	unsigned short *indices;
	const unsigned int limit = 8192;
	unsigned int count = 0, index;
	unsigned char busy[3];
	char text[32];
	const float scale = 2.0f;
	const float left = DISPLAY_WIDTH - 190.0f;

	if (!gxm.overlay_programs || (!gxm.overlay_enabled && !gxm.menu_visible))
		return;
	if (!built)
		built = malloc(limit * 6 * sizeof(*built));
	if (!built)
		return;
	if (!vertex_program)
	{
		const struct shader *shader = &gxm.shaders[gxm.overlay_vertex - 1];
		SceGxmVertexAttribute attributes[2];
		SceGxmVertexStream stream;

		attributes[0].streamIndex = 0;
		attributes[0].offset = 0;
		attributes[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		attributes[0].componentCount = 2;
		attributes[0].regIndex = (uint16_t)sceGxmProgramParameterGetResourceIndex(
			sceGxmProgramFindParameterByName(shader->program, "position"));
		attributes[1].streamIndex = 0;
		attributes[1].offset = 2 * sizeof(float);
		attributes[1].format = SCE_GXM_ATTRIBUTE_FORMAT_U8N;
		attributes[1].componentCount = 4;
		attributes[1].regIndex = (uint16_t)sceGxmProgramParameterGetResourceIndex(
			sceGxmProgramFindParameterByName(shader->program, "color"));
		stream.stride = sizeof(struct overlay_vertex);
		stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
		if (sceGxmShaderPatcherCreateVertexProgram(gxm.patcher, shader->id, attributes, 2, &stream, 1, &vertex_program) < 0)
			return;
	}
	memset(&blend, 0, sizeof(blend));
	blend.colorMask = SCE_GXM_COLOR_MASK_ALL;
	blend.colorFunc = blend.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
	blend.colorSrc = blend.alphaSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
	blend.colorDst = blend.alphaDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	fragment_program = fragment_program_get(gxm.overlay_fragment, gxm.overlay_vertex, &blend);
	if (!fragment_program)
		return;
	vertices = built;
	if (gxm.overlay_enabled)
	{
		vita_host_cpu_usage(busy);
		count = overlay_rect(vertices, count, left, 6.0f, 184.0f, 132.0f, 0xA0000000u);
		snprintf(text, sizeof(text), "FPS %3.0f", (double)gxm.overlay_fps);
		count = overlay_text(vertices, count, limit, left + 6.0f, 11.0f, scale, 0xFF40FF40u, text);
		snprintf(text, sizeof(text), "GAME %3.0f MS", (double)gxm.overlay_tick_ms);
		count = overlay_text(vertices, count, limit, left + 6.0f, 31.0f, scale, 0xFF40D0FFu, text);
		snprintf(text, sizeof(text), "REND %3.0f MS", (double)gxm.overlay_render_ms);
		count = overlay_text(vertices, count, limit, left + 6.0f, 51.0f, scale, 0xFFFFC040u, text);
		for (index = 0; index < 3; index++)
		{
			float y = 75.0f + 19.0f * index;
			uint32_t color = busy[index] == 255 ? 0xFF808080u : busy[index] > 85 ? 0xFF4040FFu : 0xFFE0E0E0u;

			if (busy[index] == 255)
				snprintf(text, sizeof(text), "C%u  N/A", index);
			else
				snprintf(text, sizeof(text), "C%u %3u%%", index, busy[index]);
			count = overlay_text(vertices, count, limit, left + 6.0f, y, scale, color, text);
			/* a bar to the right of the label */
			count = overlay_rect(vertices, count, left + 108.0f, y + 2.0f, 70.0f, 12.0f, 0xFF303030u);
			if (busy[index] != 255)
				count = overlay_rect(vertices, count, left + 108.0f, y + 2.0f, busy[index] * 0.7f, 12.0f, color);
		}
	}
	if (gxm.menu_visible)
		count = menu_build(vertices, count, limit);
	if (!count)
		return;
	vertices = vgxm_worker_alloc(count * 6 * sizeof(*vertices), 16);
	indices = vgxm_worker_alloc(count * 6 * sizeof(*indices), 16);
	if (!vertices || !indices)
		return;
	memcpy(vertices, built, count * 6 * sizeof(*vertices));
	for (index = 0; index < count * 6; index++)
		indices[index] = (unsigned short)index;
	shadow.valid = 0;
	sceGxmSetVertexProgram(gxm.context, vertex_program);
	sceGxmSetFragmentProgram(gxm.context, fragment_program);
	sceGxmSetVertexStream(gxm.context, 0, vertices);
	sceGxmSetFrontDepthFunc(gxm.context, SCE_GXM_DEPTH_FUNC_ALWAYS);
	sceGxmSetFrontDepthWriteEnable(gxm.context, SCE_GXM_DEPTH_WRITE_DISABLED);
	sceGxmSetFrontStencilFunc(gxm.context, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP,
		SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0, 0);
	sceGxmSetCullMode(gxm.context, SCE_GXM_CULL_NONE);
	sceGxmSetRegionClip(gxm.context, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
	sceGxmDraw(gxm.context, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, indices, count * 6);
	gxm.scene_draws++;
}

/* where vgxm_present's time goes: 0 ending the main scene, 1 beginning
the display scene, 2 the blit and its scene end, 3 the display queue */
static unsigned long long present_mark, present_step_us[4];
static void present_step(int step)
{
	unsigned long long now = sceKernelGetProcessTimeWide();

	present_step_us[step] += now - present_mark;
	present_mark = now;
}

void vgxm_present(unsigned long color_target, unsigned long width, unsigned long height)
{
	struct display_data data;
	SceGxmNotification notification;
	unsigned int wait_for;

	(void)width;
	(void)height;
	if (!gxm.ready)
		return;
	present_mark = sceKernelGetProcessTimeWide();
	if (gxm.in_scene)
	{
		sceGxmEndScene(gxm.context, NULL, NULL);
		gxm.in_scene = 0;
	}
	present_step(0);
	/* the frame on the display, in a scene of its own */
	sceGxmBeginScene(gxm.context, 0, gxm.display_render_target, NULL, NULL, gxm.display_sync[gxm.back_buffer],
		&gxm.display_surface[gxm.back_buffer], NULL);
	present_step(1);
	/* (the letterbox stays as the buffers were cleared at start-up) */
	if (color_target && color_target <= gxm.target_count && !gxm.targets[color_target - 1].depth)
		blit(&gxm.targets[color_target - 1]);
	overlay_draw();
	notification.address = gxm.notification;
	notification.value = ++gxm.frame;
	sceGxmEndScene(gxm.context, NULL, &notification);
	present_step(2);
	data.address = gxm.display_memory[gxm.back_buffer].base;
	sceGxmDisplayQueueAddEntry(gxm.display_sync[gxm.front_buffer], gxm.display_sync[gxm.back_buffer], &data);
	present_step(3);
	gxm.front_buffer = gxm.back_buffer;
	gxm.back_buffer = (gxm.back_buffer + 1) % DISPLAY_BUFFER_COUNT;

	/* the GPU may run up to two frames behind (the recorder is a frame
	ahead of this thread, and a ring is reused only once its frame's GPU
	work is done: vgxm_ring_next) */
	wait_for = gxm.frame >= 2 ? gxm.frame - 2 : 0;
	{
		/* how long the CPU waits for the GPU (it is the GPU's frame that is
		too long when this grows), and how many scenes a frame has */
		static unsigned long long waited, started;
		static unsigned int frames;
		unsigned long long before = sceKernelGetProcessTimeWide();

		while ((int)(*gxm.notification - wait_for) < 0)
			sceKernelDelayThread(100);
		waited += sceKernelGetProcessTimeWide() - before;
		if (!started)
			started = before;
		if (++frames == 300)
		{
			unsigned long long elapsed = sceKernelGetProcessTimeWide() - started;

			log_line("gxm: %u frames in %llu ms: %.2f ms/frame waiting for the GPU, %.1f scenes/frame (%.1f splits), ring %u KB; present: end-scene %.2f begin-display %.2f blit+end %.2f queue %.2f ms/frame",
				frames, elapsed / 1000, waited / 1000.0 / frames, (double)gxm_scene_count / frames, (double)gxm_scene_splits / frames, gxm.ring_offset_peak / 1024,
				present_step_us[0] / 1000.0 / frames, present_step_us[1] / 1000.0 / frames, present_step_us[2] / 1000.0 / frames, present_step_us[3] / 1000.0 / frames);
			memset(present_step_us, 0, sizeof(present_step_us));
			gxm_scene_splits = 0;
			{
				char line[400];
				int length = 0;
				unsigned int slot;

				for (slot = 0; slot < 128 && length < 300; slot++)
				{
					if (!scene_histogram[slot])
						continue;
					if (slot < 64 && slot <= gxm.target_count)
						length += snprintf(line + length, sizeof(line) - length, " %ux%u:%.1f",
							gxm.targets[slot - 1].width, gxm.targets[slot - 1].height, (double)scene_histogram[slot] / frames);
					else if (slot >= 64 && slot - 64 <= gxm.target_count && slot > 64)
						length += snprintf(line + length, sizeof(line) - length, " depth%ux%u:%.1f",
							gxm.targets[slot - 65].width, gxm.targets[slot - 65].height, (double)scene_histogram[slot] / frames);
					scene_histogram[slot] = 0;
				}
				log_line("gxm: scenes/frame by target:%s; begin/end CPU: main %.2f ms/frame, others %.2f ms/frame", line,
					scene_switch_us[0] / 1000.0 / frames, scene_switch_us[1] / 1000.0 / frames);
				scene_switch_us[0] = scene_switch_us[1] = 0;
			}
			waited = 0;
			frames = 0;
			started = 0;
			gxm_scene_count = 0;
			gxm.ring_offset_peak = 0;
		}
	}
	/* this frame's visibility counts are known once its notification is */
	gxm.visibility_game_frame[gxm.worker_ring_index] = gxm.visibility_next_game_frame;
	__atomic_store_n(&gxm.visibility_frame[gxm.worker_ring_index], gxm.frame, __ATOMIC_RELEASE);
	/* the worker's next frame goes to its next ring */
	gxm.worker_ring_index = (gxm.worker_ring_index + 1) % RING_COUNT;
	gxm.worker_ring_offset = 0;
	if (gxm.visibility.base)
	{
		/* its visibility buffer, last counted into four frames ago (the GPU
		is done with it: above), starts at zero again - the slots used */
		unsigned int ring = gxm.worker_ring_index, used = gxm.visibility_slots_used[ring], core;

		gxm.visibility_frame[ring] = 0;
		if (used)
			for (core = 0; core < VISIBILITY_CORES; core++)
				memset(visibility_buffer(ring) + core * (VISIBILITY_CORE_STRIDE / 4), 0, (used + 1) * 4);
		gxm.visibility_slots_used[ring] = 0;
	}
}

const void *vgxm_target_pixels(unsigned long color_target, unsigned long *pitch, unsigned long *width,
	unsigned long *height)
{
	struct target *target;

	if (!gxm.ready || !color_target || color_target > gxm.target_count)
		return NULL;
	target = &gxm.targets[color_target - 1];
	if (target->depth)
		return NULL;
	/* (a scaled target, HALO_RENDER_SCALE: its own, smaller size) */
	*width = target->width;
	*height = target->height;
	if (gxm.in_scene)
	{
		sceGxmEndScene(gxm.context, NULL, NULL);
		gxm.in_scene = 0;
	}
	sceGxmFinish(gxm.context);
	*pitch = target->stride * 4;
	return target->memory.base;
}
