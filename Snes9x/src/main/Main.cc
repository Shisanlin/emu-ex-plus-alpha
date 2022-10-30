#define LOGTAG "main"
#include <emuframework/EmuSystemInlines.hh>
#include <emuframework/EmuAppInlines.hh>
#include <imagine/fs/FS.hh>
#include <imagine/fs/ArchiveFS.hh>
#include <imagine/util/format.hh>
#include <imagine/util/string.h>

#include <snes9x.h>
#include <memmap.h>
#include <display.h>
#include <snapshot.h>
#include <cheats.h>
#ifndef SNES9X_VERSION_1_4
#include <apu/bapu/snes/snes.hpp>
#else
#include <soundux.h>
#endif

namespace EmuEx
{

const char *EmuSystem::creditsViewStr = CREDITS_INFO_STRING "(c) 2011-2022\nRobert Broglia\nwww.explusalpha.com\n\nPortions (c) the\nSnes9x Team\nwww.snes9x.com";
#if PIXEL_FORMAT == RGB565
constexpr auto srcPixFmt = IG::PIXEL_FMT_RGB565;
#else
#error "incompatible PIXEL_FORMAT value"
#endif
static EmuSystemTaskContext emuSysTask{};
static EmuVideo *emuVideo{};
constexpr auto SNES_HEIGHT_480i = SNES_HEIGHT * 2;
constexpr auto SNES_HEIGHT_EXTENDED_480i = SNES_HEIGHT_EXTENDED * 2;
bool EmuSystem::hasCheats = true;
bool EmuSystem::hasPALVideoSystem = true;
double EmuSystem::staticFrameTime = 357366. / 21477272.; // ~60.098Hz
double EmuSystem::staticPalFrameTime = 425568. / 21281370.; // ~50.00Hz
bool EmuSystem::hasResetModes = true;
bool EmuSystem::canRenderRGBA8888 = false;
bool EmuApp::needsGlobalInstance = true;

EmuSystem::NameFilterFunc EmuSystem::defaultFsFilter =
	[](std::string_view name)
	{
		return IG::endsWithAnyCaseless(name, ".smc", ".sfc", ".fig", ".mgd", ".bs");
	};
EmuSystem::NameFilterFunc EmuSystem::defaultBenchmarkFsFilter = defaultFsFilter;

const BundledGameInfo &EmuSystem::bundledGameInfo(int idx) const
{
	static constexpr BundledGameInfo info[]
	{
		{"Bio Worm", "Bio Worm.7z"}
	};

	return info[0];
}

static IG::PixmapView snesPixmapView(IG::WP size)
{
	return {{size, srcPixFmt}, GFX.Screen, {(int)GFX.Pitch, PixmapView::Units::BYTE}};
}

const char *EmuSystem::shortSystemName() const
{
	return "SFC-SNES";
}

const char *EmuSystem::systemName() const
{
	return "Super Famicom (SNES)";
}

void Snes9xSystem::renderFramebuffer(EmuVideo &video)
{
	video.startFrameWithFormat({}, snesPixmapView(video.image().size()));
}

void Snes9xSystem::reset(EmuApp &, ResetMode mode)
{
	assert(hasContent());
	if(mode == ResetMode::HARD)
	{
		S9xReset();
	}
	else
	{
		S9xSoftReset();
	}
}

#ifndef SNES9X_VERSION_1_4
#define FREEZE_EXT "frz"
#else
#define FREEZE_EXT "s96"
#endif

FS::FileString Snes9xSystem::stateFilename(int slot, std::string_view name) const
{
	return IG::format<FS::FileString>("{}.0{}." FREEZE_EXT, name, saveSlotCharUpper(slot));
}

std::string_view Snes9xSystem::stateFilenameExt() const { return "." FREEZE_EXT; }

#undef FREEZE_EXT

static FS::PathString sramFilename(EmuApp &app)
{
	return app.contentSaveFilePath(".srm");
}

void Snes9xSystem::saveState(IG::CStringView path)
{
	if(!S9xFreezeGame(path))
		return throwFileWriteError();
}

void Snes9xSystem::loadState(EmuApp &, IG::CStringView path)
{
	if(S9xUnfreezeGame(path))
	{
		IPPU.RenderThisFrame = TRUE;
	}
	else
		return throwFileReadError();
}

void Snes9xSystem::loadBackupMemory(EmuApp &app)
{
	if(!Memory.SRAMSize)
		return;
	logMsg("loading backup memory");
	Memory.LoadSRAM(sramFilename(app).c_str());
}

void Snes9xSystem::onFlushBackupMemory(EmuApp &app, BackupMemoryDirtyFlags)
{
	if(!Memory.SRAMSize)
		return;
	logMsg("saving backup memory");
	Memory.SaveSRAM(sramFilename(app).c_str());
}

IG::Time Snes9xSystem::backupMemoryLastWriteTime(const EmuApp &app) const
{
	return appContext().fileUriLastWriteTime(app.contentSaveFilePath(".srm").c_str());
}

VideoSystem Snes9xSystem::videoSystem() const { return Settings.PAL ? VideoSystem::PAL : VideoSystem::NATIVE_NTSC; }
WP Snes9xSystem::multiresVideoBaseSize() const { return {256, 239}; }

static bool isSufamiTurboCart(const IOBuffer &buff)
{
	return buff.size() >= 0x80000 && buff.size() <= 0x100000 &&
		buff.stringView(0, 14) == "BANDAI SFC-ADX" && buff.stringView(0x10, 14) != "SFC-ADX BACKUP";
}

static bool isSufamiTurboBios(const IOBuffer &buff)
{
	return buff.size() == 0x40000 &&
		buff.stringView(0, 14) == "BANDAI SFC-ADX" && buff.stringView(0x10, 14) == "SFC-ADX BACKUP";
}

bool Snes9xSystem::hasBiosExtension(std::string_view name)
{
	return IG::endsWithAnyCaseless(name, ".bin", ".bios");
}

IOBuffer Snes9xSystem::readSufamiTurboBios() const
{
	if(sufamiBiosPath.empty())
		throw std::runtime_error{"No Sufami Turbo BIOS set"};
	logMsg("loading Sufami Turbo BIOS:%s", sufamiBiosPath.data());
	if(EmuApp::hasArchiveExtension(appCtx.fileUriDisplayName(sufamiBiosPath)))
	{
		for(auto &entry : FS::ArchiveIterator{appCtx.openFileUri(sufamiBiosPath)})
		{
			if(entry.type() == FS::file_type::directory || !hasBiosExtension(entry.name()))
				continue;
			auto buff = entry.moveIO().buffer(IOBufferMode::RELEASE);
			if(!isSufamiTurboBios(buff))
				throw std::runtime_error{"Incompatible Sufami Turbo BIOS"};
			return buff;
		}
		throw std::runtime_error{"Sufami Turbo BIOS not in archive, must end in .bin or .bios"};
	}
	else
	{
		auto buff = appCtx.openFileUri(sufamiBiosPath, IOAccessHint::ALL).releaseBuffer();
		if(!isSufamiTurboBios(buff))
			throw std::runtime_error{"Incompatible Sufami Turbo BIOS"};
		return buff;
	}
}

void Snes9xSystem::loadContent(IO &io, EmuSystemCreateParams, OnLoadProgressDelegate)
{
	auto size = io.size();
	if(size > CMemory::MAX_ROM_SIZE + 512)
	{
		throw std::runtime_error("ROM is too large");
	}
	#ifndef SNES9X_VERSION_1_4
	IG::fill(Memory.NSRTHeader);
	#endif
	Memory.HeaderCount = 0;
	strncpy(Memory.ROMFilename, contentFileName().data(), sizeof(Memory.ROMFilename));
	auto forceVideoSystemSettings = [&]() -> std::pair<bool, bool> // ForceNTSC, ForcePAL
	{
		switch(optionVideoSystem.val)
		{
			case 1: return {true, false};
			case 2: return {false, true};
			case 3: return {true, true};
		}
		return {false, false};
	};
	Settings.ForceNTSC = forceVideoSystemSettings().first;
	Settings.ForcePAL = forceVideoSystemSettings().second;
	auto buff = io.buffer();
	if(!buff)
	{
		throwFileReadError();
	}
	#ifndef SNES9X_VERSION_1_4
	if(isSufamiTurboCart(buff)) // TODO: loading dual carts
	{
		logMsg("detected Sufami Turbo cart");
		auto biosBuff = readSufamiTurboBios();
		if(!Memory.LoadMultiCartMem((const uint8*)buff.data(), buff.size(),
			nullptr, 0,
			biosBuff.data(), biosBuff.size()))
		{
			throw std::runtime_error("Error loading ROM");
		}
	}
	else
	#endif
	{
		if(!Memory.LoadROMMem((const uint8*)buff.data(), buff.size()))
		{
			throw std::runtime_error("Error loading ROM");
		}
	}
	setupSNESInput(EmuApp::get(appContext()).defaultVController());
	IPPU.RenderThisFrame = TRUE;
}

void Snes9xSystem::configAudioRate(IG::FloatSeconds frameTime, int rate)
{
	const double systemFrameTime = videoSystem() == VideoSystem::PAL ? staticPalFrameTime : staticFrameTime;
	#ifndef SNES9X_VERSION_1_4
	Settings.SoundPlaybackRate = rate;
	Settings.SoundInputRate = systemFrameTime / frameTime.count() * 32040.;
	S9xUpdateDynamicRate(0, 10);
	logMsg("sound input rate:%.2f from system frame rate:%f",
		Settings.SoundInputRate, 1. / systemFrameTime);
	#else
	Settings.SoundPlaybackRate = std::round(rate / systemFrameTime * frameTime.count());
	S9xSetPlaybackRate(Settings.SoundPlaybackRate);
	logMsg("sound playback rate:%u from system frame rate:%f",
		Settings.SoundPlaybackRate, 1. / systemFrameTime);
	#endif
}

static void mixSamples(int samples, EmuAudio *audio)
{
	if(!samples) [[unlikely]]
		return;
	assumeExpr(samples % 2 == 0);
	int16_t audioBuff[1800];
	S9xMixSamples((uint8*)audioBuff, samples);
	if(audio)
	{
		//logMsg("%d frames", samples / 2);
		audio->writeFrames(audioBuff, samples / 2);
	}
}

void Snes9xSystem::runFrame(EmuSystemTaskContext taskCtx, EmuVideo *video, EmuAudio *audio)
{
	if(snesActiveInputPort != SNES_JOYPAD)
	{
		if(doubleClickFrames)
			doubleClickFrames--;
		if(rightClickFrames)
			rightClickFrames--;
		#ifndef SNES9X_VERSION_1_4
		if(snesActiveInputPort == SNES_MOUSE_SWAPPED)
		{
			int x,y;
			uint32 buttons;
			S9xReadMousePosition(0, x, y, buttons);
			*S9xGetMouseBits(0) &= ~(0x40 | 0x80);
			if(buttons == 1)
				*S9xGetMouseBits(0) |= 0x40;
			else if(buttons == 2)
				*S9xGetMouseBits(0) |= 0x80;
			S9xGetMousePosBits(0)[0] = x;
			S9xGetMousePosBits(0)[1] = y;
		}
		else // light gun
		{
			if(snesMouseClick)
				DoGunLatch(snesPointerX, snesPointerY);
		}
		#endif
	}
	emuSysTask = taskCtx;
	emuVideo = video;
	IPPU.RenderThisFrame = video ? TRUE : FALSE;
	#ifndef SNES9X_VERSION_1_4
	S9xSetSamplesAvailableCallback([](void *audio)
		{
			int samples = S9xGetSampleCount();
			mixSamples(samples, (EmuAudio*)audio);
		}, (void*)audio);
	#endif
	S9xMainLoop();
	// video rendered in S9xDeinitUpdate
	#ifdef SNES9X_VERSION_1_4
	auto samples = updateAudioFramesPerVideoFrame() * 2;
	mixSamples(samples, audio);
	#endif
}

void EmuApp::onCustomizeNavView(EmuApp::NavView &view)
{
	const Gfx::LGradientStopDesc navViewGrad[] =
	{
		{ .0, Gfx::VertexColorPixelFormat.build((139./255.) * .4, (149./255.) * .4, (230./255.) * .4, 1.) },
		{ .3, Gfx::VertexColorPixelFormat.build((139./255.) * .4, (149./255.) * .4, (230./255.) * .4, 1.) },
		{ .97, Gfx::VertexColorPixelFormat.build((46./255.) * .4, (50./255.) * .4, (77./255.) * .4, 1.) },
		{ 1., view.separatorColor() },
	};
	view.setBackgroundGradient(navViewGrad);
}

}

#ifndef SNES9X_VERSION_1_4
bool8 S9xDeinitUpdate (int width, int height)
#else
bool8 S9xDeinitUpdate(int width, int height, bool8)
#endif
{
	using namespace EmuEx;
	auto &sys = gSnes9xSystem();
	assumeExpr(emuVideo);
	if((height == SNES_HEIGHT_EXTENDED || height == SNES_HEIGHT_EXTENDED_480i)
		&& !sys.optionAllowExtendedVideoLines)
	{
		bool is480i = height >= SNES_HEIGHT_480i;
		height = is480i ? SNES_HEIGHT_480i : SNES_HEIGHT;
	}
	emuVideo->startFrameWithFormat(emuSysTask, snesPixmapView({width, height}));
	#ifndef SNES9X_VERSION_1_4
	memset(GFX.ZBuffer, 0, GFX.ScreenSize);
	memset(GFX.SubZBuffer, 0, GFX.ScreenSize);
	#endif
	return true;
}

#ifndef SNES9X_VERSION_1_4
bool8 S9xContinueUpdate(int width, int height)
{
	return S9xDeinitUpdate(width, height);
}
#endif
