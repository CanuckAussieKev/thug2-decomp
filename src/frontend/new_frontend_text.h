#pragma once

// NOT ORIGINAL - every word the new front end shows the player (ours; see new_frontend.h), in one table so the copy is
// reviewed in one place: screen titles, item labels, helper lines (the line at the foot of a list - empty: none), kickers,
// tags, hints, the options' names / help / choices, notes. The voice is a skate game's menus: short, plain, no
// parentheses, no explanations of how things work, no file names or ini keys (those only behind [frontend]
// show_ini_keys=1). The original game's own words that ours repeats (the difficulty lines, the career warning) are
// THUG2's text as it ships. Log lines, automation replies and ids are not here (nobody playing sees them).
namespace frontend::new_frontend::text_tools {

// --- headers (the mono line at the top of the column) and screen titles ------------------------------------------
inline constexpr const char* kTitleMain = "THUG2 / Decomp edition";
inline constexpr const char* kTitleCareer = "Career";
inline constexpr const char* kTitleStory = "THUG2 story";
inline constexpr const char* kTitleClassic = "Classic mode";
inline constexpr const char* kTitleCreate = "Create";
inline constexpr const char* kTitleOptions = "Options";
inline constexpr const char* kTitleFreeSkate = "Free skate";
inline constexpr const char* kTitleChangeLevel = "Change level";
inline constexpr const char* kTitleEditLevel = "Level editor";
inline constexpr const char* kTitleNetLevel = "Net game level";
inline constexpr const char* kTitlePaused = "Paused";
inline constexpr const char* kTitleMenu = "Menu"; // (a restyled list without a title)
inline constexpr const char* kTitleSkaters = "Select skater";
inline constexpr const char* kTitleKeyboard = "Enter text"; // (the on-screen keyboard without a title)
inline constexpr const char* kTitleStats = "Stats"; // (the stats screen without a title of its own)
inline constexpr const char* kBack = "Back";                // (the header's back arrow)

// --- the main menu: label, helper line, the art's kicker -----------------------------------------------------------
inline constexpr const char* kCareer = "Career";
inline constexpr const char* kCareerHelp = "";
inline constexpr const char* kFreeSkate = "Free Skate";
inline constexpr const char* kFreeSkateHelp = "No goals. No clock. Just skate.";
inline constexpr const char* kOnline = "Online";
inline constexpr const char* kOnlineHelp = "";
inline constexpr const char* kCreate = "Create";
inline constexpr const char* kCreateHelp = "";
inline constexpr const char* kOptions = "Options";
inline constexpr const char* kOptionsHelp = "";
inline constexpr const char* kQuit = "Quit";
inline constexpr const char* kQuitHelp = "";
// The kicker over the highlighted item's big name: "Main menu — 02".
inline constexpr const char* kMainKicker = "Main menu";
// The last level played, on the hints row: "X Resume Airport".
inline constexpr const char* kHintResume = "X Resume";

// --- the career ---------------------------------------------------------------------------------------------------
inline constexpr const char* kStoryThug2 = "THUG2 Story";
inline constexpr const char* kStoryThug2Help = "The World Destruction Tour.";
inline constexpr const char* kClassic = "Classic";
inline constexpr const char* kClassicHelp = "Two-minute runs. Old-school goals.";
inline constexpr const char* kStoryThug = "THUG Story";
inline constexpr const char* kStoryThugHelp = "";
inline constexpr const char* kComingSoon = "Coming soon"; // (the tag after a disabled item)
inline constexpr const char* kContinue = "Continue";
inline constexpr const char* kContinueHelp = "";
inline constexpr const char* kNewStory = "New story";
inline constexpr const char* kNewClassic = "New classic";
inline constexpr const char* kNewHelp = "";
inline constexpr const char* kLoad = "Load game";
inline constexpr const char* kLoadHelp = "";
// The difficulties: THUG2's own lines (its difficulty menu's helper text).
inline constexpr const char* kNormal = "Normal";
// Before a new career / a load over a started one: THUG2's own warning ("Any unsaved changes to your current story
// mode will be lost." - the part in the middle is the mode).

// --- create -------------------------------------------------------------------------------------------------------
inline constexpr const char* kCas = "Create-A-Skater";
inline constexpr const char* kCat = "Create-A-Trick";
inline constexpr const char* kCag = "Create-A-Goal";
inline constexpr const char* kCap = "Create-A-Park";
inline constexpr const char* kCagr = "Create-A-Graphic";
inline constexpr const char* kLevelEditor = "Level Editor";
inline constexpr const char* kCreateItemHelp = "";

// --- online -------------------------------------------------------------------------------------------------------
inline constexpr const char* kLobbies = "Lobbies";
// The mono line under the big name: "THUG2 lobbies · Beenox".
inline constexpr const char* kIdentityThug2 = "THUG2";
inline constexpr const char* kIdentityThugpro = "THUGPRO";

// --- the pause menu -----------------------------------------------------------------------------------------------
inline constexpr const char* kResume = "Resume";
inline constexpr const char* kRetryGoal = "Retry goal";
inline constexpr const char* kRestart = "Restart";
inline constexpr const char* kChangeLevel = "Change level";
inline constexpr const char* kPauseLevelEditor = "Level editor";
inline constexpr const char* kPauseOptions = "Options";
inline constexpr const char* kPauseMenu = "Pause menu";
inline constexpr const char* kQuitToMenu = "Quit to menu";
inline constexpr const char* kViewGoals = "View goals";
inline constexpr const char* kYourOptions = "Your options";
// The session row: "Best combo 20,005", "Gaps 3/14", "Time 01:52".
inline constexpr const char* kLabelBestChain = "Best combo";
inline constexpr const char* kGaps = "Gaps";
inline constexpr const char* kGoals = "Goals";
inline constexpr const char* kTime = "Time";

// --- hints ----------------------------------------------------------------------------------------------------------
inline constexpr const char* kHintSelect = "A Select";
inline constexpr const char* kHintOpen = "A Open";
inline constexpr const char* kHintAccept = "A Accept";
inline constexpr const char* kHintBack = "B Back";
inline constexpr const char* kHintResumeLevel = "B Resume";
inline constexpr const char* kHintChange = "Left Right Change";
// The arrow buttons in the game's own words (its helper lines' arrow glyphs).
inline constexpr const char* kKeyUp = "Up";
inline constexpr const char* kKeyDown = "Down";
inline constexpr const char* kKeyLeft = "Left";
inline constexpr const char* kKeyRight = "Right";
inline constexpr const char* kKeyUpLeft = "Up-Left";
inline constexpr const char* kKeyUpRight = "Up-Right";
inline constexpr const char* kKeyDownLeft = "Down-Left";
inline constexpr const char* kKeyDownRight = "Down-Right";
inline constexpr const char* kHintGroup = "LB RB Group";
inline constexpr const char* kHintSkater = "Left Right Skater";
inline constexpr const char* kHintLoadSkater = "E Load skater";
// (the stats screen)
inline constexpr const char* kHintStat = "Up Down Stat";
inline constexpr const char* kHintColumn = "Left Right Column";
inline constexpr const char* kHintEditStat = "A Change";
inline constexpr const char* kHintDone = "B Done";
inline constexpr const char* kPointsLeft = "Points left";
inline constexpr const char* kPleaseWait = "Please wait";

// --- the level select ---------------------------------------------------------------------------------------------
inline constexpr const char* kSkate = "Skate";
inline constexpr const char* kEdit = "Edit";
inline constexpr const char* kSelect = "Select"; // (a network game's level: chosen, not skated yet)
inline constexpr const char* kLocked = "Locked";
inline constexpr const char* kNoLevels = "No levels here.";

// --- options ------------------------------------------------------------------------------------------------------
inline constexpr const char* kGameOptions = "Game options";
inline constexpr const char* kGameOptionsValue = "Sound, controls";
inline constexpr const char* kGameOptionsHelp = "Sound, controls, display and more.";
inline constexpr const char* kOpen = "Open";
inline constexpr const char* kRestartToApply = "Restart to apply";
inline constexpr const char* kCouldntSave = "Couldn't save settings.";
inline constexpr const char* kOff = "Off";
inline constexpr const char* kOn = "On";
// The groups.
inline constexpr const char* kGroupGraphics = "Graphics";
inline constexpr const char* kGroupInterface = "Interface";
inline constexpr const char* kGroupGameplay = "Gameplay";
inline constexpr const char* kGroupOnline = "Online";
inline constexpr const char* kGroupMods = "Mods";
inline constexpr const char* kGroupSound = "Sound";
// Each option: its name, its help line ("" none) and the choices that aren't Off / On.
inline constexpr const char* kOptTextureSet = "Textures";
inline constexpr const char* kOptTextureSetHelp = "The original textures or an upscaled set. Changes the level you're in.";
inline constexpr const char* kTextureSetOriginal = "Original";
inline constexpr const char* kOptAnisotropy = "Anisotropic filtering";
inline constexpr const char* kOptAnisotropyHelp = "Sharper textures at an angle.";
inline constexpr const char* kOptMsaa = "Anti-aliasing";
inline constexpr const char* kOptMsaaHelp = "Smooths jagged edges.";
inline constexpr const char* kOptDrawDistance = "Draw distance";
inline constexpr const char* kOptDrawDistanceHelp = "How far you can see.";
inline constexpr const char* kOptLodDistance = "Object detail distance";
inline constexpr const char* kOptLodDistanceHelp = "Keeps far objects detailed.";
inline constexpr const char* kOptMaxFps = "Frame rate";
inline constexpr const char* kOptMaxFpsHelp = "Smoother motion above 60. The game still plays at 60.";
inline constexpr const char* kMaxFpsOriginal = "60 (original)";
inline constexpr const char* kMaxFpsUncapped = "Uncapped";
inline constexpr const char* kOptFrameGeneration = "Frame generation";
inline constexpr const char* kOptFrameGenerationHelp = "Extra frames from DLSS or FSR. Turns the upscaler on.";
inline constexpr const char* kOptReflex = "NVIDIA Reflex";
inline constexpr const char* kOptReflexHelp = "Lower input lag. Always on with DLSS frame generation.";
inline constexpr const char* kReflexBoost = "On + Boost";
inline constexpr const char* kOptPresentMode = "V-sync";
inline constexpr const char* kOptPresentModeHelp = "On waits for your display. Off can tear.";
inline constexpr const char* kOptShowFps = "Show FPS";
inline constexpr const char* kOptShowFpsHelp = "Frame rate in the corner. F3 toggles it.";
inline constexpr const char* kPresentMailbox = "Off (no tearing)";
inline constexpr const char* kPresentImmediate = "Off";
inline constexpr const char* kOptNewFrontend = "New front end";
inline constexpr const char* kOptNewFrontendHelp = "Off brings back the classic menus.";
inline constexpr const char* kOptHdUi = "HD menus";
inline constexpr const char* kOptHdUiHelp = "Sharper menu art and fonts.";
inline constexpr const char* kOptMouse = "Mouse in menus";
inline constexpr const char* kOptMouseHelp = "";
inline constexpr const char* kOptLoadingConsole = "Loading console";
inline constexpr const char* kOptLoadingConsoleHelp = "Shows what's loading.";
inline constexpr const char* kOptSkipIntro = "Skip intro movies";
inline constexpr const char* kOptSkipIntroHelp = "";
inline constexpr const char* kOptRestartTester = "Restart tester";
inline constexpr const char* kOptRestartTesterHelp = "F8 jumps to the next restart point.";
inline constexpr const char* kOptScreenshots = "Screenshots";
inline constexpr const char* kOptScreenshotsHelp = "Print Screen saves a screenshot.";
inline constexpr const char* kOptLivePause = "Pause";
inline constexpr const char* kOptLivePauseHelp = "Free Skate: the world keeps moving behind the pause menu.";
inline constexpr const char* kOptLivePauseFreeze = "Freeze";
inline constexpr const char* kOptLivePauseLive = "Keep going";
inline constexpr const char* kOptLocks = "Free Skate levels";
inline constexpr const char* kOptLocksHelp = "";
inline constexpr const char* kOptLocksAll = "All";
inline constexpr const char* kOptLocksUnlocked = "Unlocked";
inline constexpr const char* kOptIdentity = "Lobbies";
inline constexpr const char* kOptIdentityHelp = "";
inline constexpr const char* kOptThugproChat = "THUGPRO chat";
inline constexpr const char* kOptThugproChatHelp = "Chat with THUGPRO players.";
inline constexpr const char* kOptThugproParts = "THUGPRO parts";
inline constexpr const char* kOptThugproPartsHelp = "Extra CAS parts from THUGPRO.";
inline constexpr const char* kOptThugproPartsHidden = "Hidden";
inline constexpr const char* kOptThugproPartsShown = "Shown";
inline constexpr const char* kOptAnteara = "Anteara Mod";
inline constexpr const char* kOptAntearaHelp = "";
inline constexpr const char* kOptBmx = "THAW BMX";
inline constexpr const char* kOptBmxHelp = "Ride BMX. F7 or the pause menu.";
inline constexpr const char* kOptDebug = "Debug mode";
inline constexpr const char* kOptDebugHelp = "Neversoft's debug menus and free camera.";
inline constexpr const char* kOptContinueMusic = "Keep music playing";
inline constexpr const char* kOptContinueMusicHelp = "The song plays on between levels.";
// The graphics groups (the renderer's effects: README's [Graphics] keys).
inline constexpr const char* kGroupLighting = "Lighting";
inline constexpr const char* kGroupSurfaces = "Surfaces";
inline constexpr const char* kGroupSky = "Sky";
inline constexpr const char* kGroupColour = "Colour";
inline constexpr const char* kGroupCamera = "Camera";
inline constexpr const char* kGroupStyle = "Style";
inline constexpr const char* kGroupUpscaling = "Upscaling";
inline constexpr const char* kOptPreset = "Graphics preset";
inline constexpr const char* kOptPresetHelp = "Sets every effect at once.";
inline constexpr const char* kPresetCustom = "Custom";
inline constexpr const char* kPresetOriginal = "Original";
inline constexpr const char* kPresetHigh = "High";
inline constexpr const char* kPresetUltra = "Ultra";
inline constexpr const char* kOptRenderer = "Renderer";
inline constexpr const char* kOptRendererHelp = "";
inline constexpr const char* kOptRayTracing = "Ray tracing";
inline constexpr const char* kOptRayTracingHelp = "Needed by most effects. RTX-class GPU.";
inline constexpr const char* kOptTessellation = "Smooth curves";
inline constexpr const char* kOptTessellationHelp = "Rounder quarter pipes, rails and props.";
inline constexpr const char* kOptTessDeviation = "Curve smoothing";
inline constexpr const char* kOptTessDeviationHelp = "How far a curve may round off.";
inline constexpr const char* kOptSunSource = "Sun";
inline constexpr const char* kOptSunSourceHelp = "Sky: the sun you see lights and relights the level. Level: its own light.";
inline constexpr const char* kOptSunSky = "Sky";
inline constexpr const char* kOptSunLevel = "Level";
inline constexpr const char* kOptShadows = "Shadows";
inline constexpr const char* kOptShadowsHelp = "Ray-traced sun shadows.";
inline constexpr const char* kOptShadowStrength = "Shadow darkness";
inline constexpr const char* kOptShadowSoftness = "Shadow softness";
inline constexpr const char* kOptShadowObjects = "Skater shadows";
inline constexpr const char* kOptShadowObjectsHelp = "Skaters, peds and cars cast them too.";
inline constexpr const char* kOptAo = "Ambient occlusion";
inline constexpr const char* kOptAoHelp = "Darker corners and contact shadows.";
inline constexpr const char* kOptAoStrength = "Occlusion darkness";
inline constexpr const char* kOptAoRadius = "Occlusion reach";
inline constexpr const char* kOptAoRays = "Occlusion rays";
inline constexpr const char* kOptGi = "Bounce light";
inline constexpr const char* kOptGiHelp = "Sunlit walls light what's near them.";
inline constexpr const char* kOptGiStrength = "Bounce strength";
inline constexpr const char* kOptGiRays = "Bounce rays";
inline constexpr const char* kOptLights = "Lamp light";
inline constexpr const char* kOptLightsHelp = "Street lamps, neon and lit windows light the night.";
inline constexpr const char* kOptLightsStrength = "Lamp brightness";
inline constexpr const char* kOptLightsShadowed = "Lamp shadows";
inline constexpr const char* kOptRelit = "Relit levels";
inline constexpr const char* kOptRelitHelp = "Lights each level again with rays.";
inline constexpr const char* kOptRelitStrength = "Relit strength";
inline constexpr const char* kOptPbr = "Per-pixel lighting";
inline constexpr const char* kOptPbrHelp = "Bumps and highlights. Needs surface maps.";
inline constexpr const char* kOptPbrSpecular = "Highlights";
inline constexpr const char* kOptPbrNormal = "Surface bumps";
inline constexpr const char* kOptRelief = "Surface depth";
inline constexpr const char* kOptReliefHelp = "Brick, stone and cobbles get real depth. Needs per-pixel lighting.";
inline constexpr const char* kOptReliefStrength = "Depth strength";
inline constexpr const char* kOptDetail = "Surface detail";
inline constexpr const char* kOptDetailHelp = "Real grain and materials up close. Needs per-pixel lighting.";
inline constexpr const char* kOptDetailStrength = "Detail strength";
inline constexpr const char* kOptDelight = "Flatten painted shadows";
inline constexpr const char* kOptDelightHelp = "Evens out shading baked into textures. Needs per-pixel lighting.";
inline constexpr const char* kOptDelightStrength = "Flatten strength";
inline constexpr const char* kOptReflections = "Reflections";
inline constexpr const char* kOptReflectionsHelp = "Ray-traced reflections on shiny surfaces.";
inline constexpr const char* kOptReflectionStrength = "Reflection strength";
inline constexpr const char* kOptFresnel = "Angle reflections";
inline constexpr const char* kOptFresnelHelp = "Water and glass mirror more at an angle.";
inline constexpr const char* kOptGlassReflectance = "Glass shine";
inline constexpr const char* kOptGlassRoughness = "Glass blur";
inline constexpr const char* kOptGrass = "Grass";
inline constexpr const char* kOptGrassHelp = "Real blades on lawns. They part under your board.";
inline constexpr const char* kOptGrassDensity = "Grass density";
inline constexpr const char* kOptGrassHeight = "Grass height";
inline constexpr const char* kOptGrassDistance = "Grass distance";
inline constexpr const char* kOptWater = "Real water";
inline constexpr const char* kOptWaterHelp = "Waves, reflections and depth in the water.";
inline constexpr const char* kOptWaterWaves = "Waves";
inline constexpr const char* kOptWaterWind = "Wind";
inline constexpr const char* kOptWaterClarity = "Water clarity";
inline constexpr const char* kOptWaterFoam = "Shore foam";
inline constexpr const char* kOptWaterTint = "Water colour";
inline constexpr const char* kOptWaterGeometry = "Rolling waves";
inline constexpr const char* kOptWaterGeometryHelp = "The water rises and falls with the swell. Takes effect on the next level load.";
inline constexpr const char* kOptSky = "Real sky";
inline constexpr const char* kOptSkyHelp = "A live sky in the level's colours.";
inline constexpr const char* kOptSkyModel = "Sun colour from sky";
inline constexpr const char* kOptSkyModelHelp = "Warm when low, white when high. Needs ray tracing.";
inline constexpr const char* kOptClouds = "Clouds";
inline constexpr const char* kOptCloudsHelp = "Volumetric clouds. Needs Real sky.";
inline constexpr const char* kOptCloudCoverage = "Cloud cover";
inline constexpr const char* kOptCloudQuality = "Cloud quality";
inline constexpr const char* kOptSun = "Sun";
inline constexpr const char* kOptSunHelp = "A bright sun disc instead of the old sprite.";
inline constexpr const char* kOptSunColour = "Sun tint";
inline constexpr const char* kOptShafts = "Sun rays";
inline constexpr const char* kOptShaftsHelp = "Light streaming past buildings.";
inline constexpr const char* kOptFog = "Volumetric fog";
inline constexpr const char* kOptFogHelp = "Fog the sun shines through.";
inline constexpr const char* kOptHaze = "Haze";
inline constexpr const char* kOptHazeHelp = "Thin air for levels with no fog.";
inline constexpr const char* kOptHazeDensity = "Haze thickness";
inline constexpr const char* kOptHdr = "HDR";
inline constexpr const char* kOptHdrHelp = "Brighter than white. Unlocks the rows below.";
inline constexpr const char* kOptExposure = "Exposure";
inline constexpr const char* kOptTonemap = "Tone curve";
inline constexpr const char* kOptWhitePoint = "White point";
inline constexpr const char* kOptSaturation = "Saturation";
inline constexpr const char* kOptContrast = "Contrast";
inline constexpr const char* kOptBloom = "Bloom";
inline constexpr const char* kOptBloomHelp = "Bright light glows.";
inline constexpr const char* kOptBloomStrength = "Bloom strength";
// (the camera effects - gpu_vulkan/vulkan_camera_fx.h)
inline constexpr const char* kOptMotionBlur = "Motion blur";
inline constexpr const char* kOptMotionBlurHelp = "Fast moves streak like on film.";
inline constexpr const char* kOptMotionBlurStrength = "Shutter";
inline constexpr const char* kOptDof = "Depth of field";
inline constexpr const char* kOptDofHelp = "Your skater sharp, the distance soft.";
inline constexpr const char* kOptDofStrength = "Focus blur";
inline constexpr const char* kOptGrade = "Colour grade";
inline constexpr const char* kOptGradeHelp = "A film look over the whole picture.";
inline constexpr const char* kOptGradeStrength = "Grade mix";
inline constexpr const char* kOptGradeVignette = "Lens vignette";
inline constexpr const char* kOptGradeGrain = "Film grain";
inline constexpr const char* kGradeNeutral = "Neutral";
inline constexpr const char* kGradeWarm = "Warm";
inline constexpr const char* kGradeCool = "Cool";
inline constexpr const char* kGradeFilmic = "Filmic";
inline constexpr const char* kGradeBleach = "Bleach bypass";
inline constexpr const char* kGradeSkateVideo = "Skate video";
inline constexpr const char* kGradeLevel = "Per level";
// (the fisheye styles' lens - gpu_vulkan/vulkan_fisheye.h)
inline constexpr const char* kOptFisheyeStrength = "Fisheye bend";
inline constexpr const char* kOptFisheyeVignette = "Lens corners";
inline constexpr const char* kOptFisheyeCircle = "Lens frame";
inline constexpr const char* kOptFisheyeCircleHelp = "Full frame or the round porthole.";
inline constexpr const char* kFisheyeFullFrame = "Full frame";
inline constexpr const char* kFisheyeRounded = "Rounded";
inline constexpr const char* kFisheyePorthole = "Porthole";
inline constexpr const char* kOptStyle = "Art style";
inline constexpr const char* kOptStyleHelp = "Paint the world in a whole new look.";
inline constexpr const char* kStyleToon = "Toon";
inline constexpr const char* kStyleCel = "Cel";
inline constexpr const char* kStyleComic = "Comic";
inline constexpr const char* kStyleSketch = "Sketch";
inline constexpr const char* kStyleVhs = "VHS";
inline constexpr const char* kStyleRetro = "Retro";
inline constexpr const char* kStyleWatercolour = "Watercolour";
inline constexpr const char* kStyleNoir = "Noir";
inline constexpr const char* kStyleThermal = "Thermal";
inline constexpr const char* kStyleFisheye = "Fish Eye";
inline constexpr const char* kStyleFisheyeVhs = "Fish Eye + VHS";
inline constexpr const char* kOptStyleHalftone = "Halftone dots";
inline constexpr const char* kOptStyleHatch = "Pencil strokes";
inline constexpr const char* kOptStyleWash = "Colour wash";
inline constexpr const char* kOptStylePaper = "Paper";
inline constexpr const char* kOptStyleGrain = "Grain";
inline constexpr const char* kOptStyleVignette = "Vignette";
inline constexpr const char* kOptStyleContrast = "Noir contrast";
inline constexpr const char* kOptStyleDark = "Shadow tint";
inline constexpr const char* kOptStyleLight = "Light tint";
inline constexpr const char* kOptStyleFocus = "Body heat";
inline constexpr const char* kOptStyleStrength = "Style mix";
inline constexpr const char* kOptStyleSmooth = "Paint";
inline constexpr const char* kOptStyleBands = "Shade bands";
inline constexpr const char* kOptStyleOutlines = "Ink lines";
inline constexpr const char* kOptStyleOutlineWidth = "Line width";
inline constexpr const char* kOptStyleSaturation = "Style colour";
// (added by cel shading)
inline constexpr const char* kOptCelBands = "Light bands";
inline constexpr const char* kOptCelShadow = "Shadow tint";
inline constexpr const char* kCelShadowCool = "Cool";
inline constexpr const char* kCelShadowDusk = "Dusk";
inline constexpr const char* kCelShadowWarm = "Warm";
inline constexpr const char* kCelShadowGrey = "Grey";
inline constexpr const char* kOptCelRim = "Rim light";
inline constexpr const char* kOptCelOutlines = "Cel ink";
inline constexpr const char* kOptCelFlatten = "Flat colour";
// (NOT ORIGINAL - the vhs, retro and watercolour styles' rows)
inline constexpr const char* kOptVhsFisheye = "VHS fisheye";
inline constexpr const char* kOptVhsBleed = "VHS colour bleed";
inline constexpr const char* kOptVhsWobble = "VHS tape wobble";
inline constexpr const char* kOptVhsNoise = "VHS tape noise";
inline constexpr const char* kOptVhsScanlines = "VHS scanlines";
inline constexpr const char* kOptVhsSaturation = "VHS colour";
inline constexpr const char* kOptVhsOverlay = "VHS REC display";
inline constexpr const char* kOptVhsOverlayHelp = "Blinking REC and the tape counter.";
inline constexpr const char* kOptRetroLines = "Retro resolution";
inline constexpr const char* kOptRetroBits = "Retro colours";
inline constexpr const char* kOptRetroDither = "Retro dither";
inline constexpr const char* kOptRetroSnap = "Retro vertex jitter";
inline constexpr const char* kOptRetroAffine = "Retro texture warp";
inline constexpr const char* kOptWatercolourBleed = "Watercolour bleed";
inline constexpr const char* kOptWatercolourFlow = "Watercolour flow";
inline constexpr const char* kOptWatercolourGranulation = "Paper grain";
inline constexpr const char* kOptWatercolourEdges = "Wet edges";
inline constexpr const char* kOptWatercolourPaper = "Paper wash";
inline constexpr const char* kOptWatercolourOutlines = "Pencil lines";
inline constexpr const char* kOptUpscaler = "Upscaler";
inline constexpr const char* kOptUpscalerHelp = "Faster frames, sharp menus.";
inline constexpr const char* kOptUpscaleQuality = "Upscale quality";
inline constexpr const char* kOptUpscaleQualityHelp = "DLAA draws it all. Lower is faster.";
inline constexpr const char* kOptRayReconstruction = "Ray Reconstruction";
inline constexpr const char* kOptRayReconstructionHelp = "DLSS cleans up ray-traced noise.";
inline constexpr const char* kLow = "Low";
inline constexpr const char* kMedium = "Medium";
inline constexpr const char* kHigh = "High";
inline constexpr const char* kAuto = "Auto";

// --- restyled original screens --------------------------------------------------------------------------------------
inline constexpr const char* kDialogConfirm = "Confirm";
inline constexpr const char* kDialogNotice = "Notice";
inline constexpr const char* kSkater = "Skater";

} // namespace frontend::new_frontend::text_tools
