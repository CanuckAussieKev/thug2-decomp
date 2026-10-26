#pragma once

// NOT ORIGINAL - every word the new front end shows the player (ours; see new_frontend.h), in one table so the copy is
// reviewed in one place: screen titles, item labels, helper lines (the line at the foot of a list - empty: none), kickers,
// tags, hints, the options' names / help / choices, notes. The voice is a skate game's menus: short, plain, no
// parentheses, no explanations of how things work, no file names or ini keys (those only behind [frontend]
// show_ini_keys=1). The original game's own words that ours repeats (the difficulty lines, the career warning) are
// THUG2's text as it ships. Log lines, automation replies and ids are not here (nobody playing sees them).
namespace Front::NewFrontend::Text {

// --- headers (the mono line at the top of the column) and screen titles ------------------------------------------
inline constexpr const char* kTitleMain = "THUG2 / Decomp edition";
inline constexpr const char* kTitleCareer = "Career";
inline constexpr const char* kTitleStory = "THUG2 story";
inline constexpr const char* kTitleClassic = "Classic mode";
inline constexpr const char* kTitleStoryDifficulty = "THUG2 story / Difficulty";
inline constexpr const char* kTitleClassicDifficulty = "Classic mode / Difficulty";
inline constexpr const char* kTitleConfirm = "Are you sure?";
inline constexpr const char* kTitleCreate = "Create";
inline constexpr const char* kTitleOnline = "Online";
inline constexpr const char* kTitleOptions = "Options";
inline constexpr const char* kTitleFreeSkate = "Free skate";
inline constexpr const char* kTitleChangeLevel = "Change level";
inline constexpr const char* kTitleEditLevel = "Level editor";
inline constexpr const char* kTitlePaused = "Paused";
inline constexpr const char* kTitlePauseGoals = "Paused / Goals and gaps";
inline constexpr const char* kTitleMenu = "Menu";     // (a restyled list without a title)
inline constexpr const char* kTitleSkaters = "Select skater";
inline constexpr const char* kBack = "Back";          // (the header's back arrow)

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
inline constexpr const char* kContinueNoneHelp = "No career started yet.";
inline constexpr const char* kNewStory = "New story";
inline constexpr const char* kNewClassic = "New classic";
inline constexpr const char* kNewHelp = "";
inline constexpr const char* kLoad = "Load game";
inline constexpr const char* kLoadHelp = "";
// The difficulties: THUG2's own lines (its difficulty menu's helper text).
inline constexpr const char* kEasy = "Easy";
inline constexpr const char* kEasyHelp = "Less difficult goals for those fairly new to the Tony Hawk series.";
inline constexpr const char* kNormal = "Normal";
inline constexpr const char* kNormalHelp = "Challenging goals for the average to good Tony Hawk player.";
inline constexpr const char* kSick = "Sick";
inline constexpr const char* kSickHelp = "For hardcore veterans who have mastered the Tony Hawk games.";
// Before a new career / a load over a started one: THUG2's own warning ("Any unsaved changes to your current story
// mode will be lost." - the part in the middle is the mode).
inline constexpr const char* kWarnLostBefore = "Any unsaved changes to your current ";
inline constexpr const char* kWarnLostAfter = " will be lost.";
inline constexpr const char* kWarnStory = "story mode";
inline constexpr const char* kWarnClassic = "classic mode";
inline constexpr const char* kWarnProgress = "game progress";
inline constexpr const char* kWarnUnlocksStay = " Items unlocked from beating the game stay.";
inline constexpr const char* kStartOver = "Start over";
inline constexpr const char* kLoadAnyway = "Load anyway";
inline constexpr const char* kCancel = "Cancel";
inline constexpr const char* kCancelHelp = "";

// --- create -------------------------------------------------------------------------------------------------------
inline constexpr const char* kCas = "Create-A-Skater";
inline constexpr const char* kCat = "Create-A-Trick";
inline constexpr const char* kCag = "Create-A-Goal";
inline constexpr const char* kCap = "Create-A-Park";
inline constexpr const char* kCagr = "Create-A-Graphic";
inline constexpr const char* kLevelEditor = "Level Editor";
inline constexpr const char* kCreateItemHelp = "";
inline constexpr const char* kLevelEditorOffHelp = "Turn it on in Options.";

// --- online -------------------------------------------------------------------------------------------------------
inline constexpr const char* kPlayOnline = "Play online";
inline constexpr const char* kPlayOnlineHelp = "";
inline constexpr const char* kLobbies = "Lobbies";
inline constexpr const char* kLobbiesHelp = "";
// The mono line under the big name: "THUG2 lobbies · Beenox".
inline constexpr const char* kLobbiesOf = " lobbies";
inline constexpr const char* kIdentityThug2 = "THUG2";
inline constexpr const char* kIdentityThugpro = "THUGPRO";
inline constexpr const char* kIdentityCustom = "Custom";
inline constexpr const char* kLobbyThug2 = "Beenox";
inline constexpr const char* kLobbyThugpro = "General, Newbies";

// --- the pause menu -----------------------------------------------------------------------------------------------
inline constexpr const char* kResume = "Resume";
inline constexpr const char* kRetryGoal = "Retry goal";
inline constexpr const char* kRestart = "Restart";
inline constexpr const char* kGoalsAndGaps = "Goals and gaps";
inline constexpr const char* kChangeLevel = "Change level";
inline constexpr const char* kPauseLevelEditor = "Level editor";
inline constexpr const char* kRideBmx = "Ride BMX";
inline constexpr const char* kGetOffBmx = "Get off BMX";
inline constexpr const char* kPauseOptions = "Options";
inline constexpr const char* kPauseMenu = "Pause menu";
inline constexpr const char* kQuitToMenu = "Quit to menu";
inline constexpr const char* kViewGoals = "View goals";
inline constexpr const char* kViewGaps = "View gaps";
inline constexpr const char* kYourOptions = "Your options";
inline constexpr const char* kPauseItemHelp = "";
// The session row: "Best combo 20,005", "Gaps 3/14", "Time 01:52".
inline constexpr const char* kBestCombo = "Best combo";
inline constexpr const char* kGaps = "Gaps";
inline constexpr const char* kGoals = "Goals";
inline constexpr const char* kTime = "Time";

// --- hints ----------------------------------------------------------------------------------------------------------
inline constexpr const char* kHintSelect = "A Select";
inline constexpr const char* kHintOpen = "A Open";
inline constexpr const char* kHintAccept = "A Accept";
inline constexpr const char* kHintBack = "B Back";
inline constexpr const char* kHintQuit = "B Quit";
inline constexpr const char* kHintResumeLevel = "B Resume";
inline constexpr const char* kHintChange = "Left Right Change";
inline constexpr const char* kHintGroup = "LB RB Group";
inline constexpr const char* kHintSkater = "Left Right Skater";
inline constexpr const char* kHintLoadSkater = "E Load skater";
inline constexpr const char* kPleaseWait = "Please wait";

// --- the level select ---------------------------------------------------------------------------------------------
inline constexpr const char* kSkate = "Skate";
inline constexpr const char* kEdit = "Edit";
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
// Each option: its name, its help line ("" none) and the choices that aren't Off / On.
inline constexpr const char* kOptAnisotropy = "Anisotropic filtering";
inline constexpr const char* kOptAnisotropyHelp = "Sharper textures at an angle.";
inline constexpr const char* kOptMsaa = "Anti-aliasing";
inline constexpr const char* kOptMsaaHelp = "Smooths jagged edges.";
inline constexpr const char* kOptDrawDistance = "Draw distance";
inline constexpr const char* kOptDrawDistanceHelp = "How far you can see.";
inline constexpr const char* kOptLodDistance = "Object detail distance";
inline constexpr const char* kOptLodDistanceHelp = "Keeps far objects detailed.";
inline constexpr const char* kOptNewFrontend = "New front end";
inline constexpr const char* kOptNewFrontendHelp = "Off brings back the classic menus.";
inline constexpr const char* kOptHdUi = "HD menus";
inline constexpr const char* kOptHdUiHelp = "Sharper menu art and fonts.";
inline constexpr const char* kOptBackdrop = "Menu backdrop";
inline constexpr const char* kOptBackdropHelp = "";
inline constexpr const char* kOptBackdropClassic = "THUG2 shop";
inline constexpr const char* kOptBackdropThps4 = "THPS4 shop";
inline constexpr const char* kOptMouse = "Mouse in menus";
inline constexpr const char* kOptMouseHelp = "";
inline constexpr const char* kOptLoadingConsole = "Loading console";
inline constexpr const char* kOptLoadingConsoleHelp = "Shows what's loading.";
inline constexpr const char* kOptSkipIntro = "Skip intro movies";
inline constexpr const char* kOptSkipIntroHelp = "";
inline constexpr const char* kOptRestartTester = "Restart tester";
inline constexpr const char* kOptRestartTesterHelp = "F8 jumps to the next restart point.";
inline constexpr const char* kOptLevelEditor = "Level editor";
inline constexpr const char* kOptLevelEditorHelp = "Adds Level Editor to the pause menu.";
inline constexpr const char* kOptScreenshots = "Screenshots";
inline constexpr const char* kOptScreenshotsHelp = "Print Screen saves a screenshot.";
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

// --- restyled original screens --------------------------------------------------------------------------------------
inline constexpr const char* kDialogConfirm = "Confirm";
inline constexpr const char* kDialogNotice = "Notice";
inline constexpr const char* kSkater = "Skater";

} // namespace Front::NewFrontend::Text
