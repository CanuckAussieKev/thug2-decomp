// Each callee of the skate module the test stands in for (ours too, found by its REPLACE): {the original, the bytes
// it pops, whether its object (ECX) is recorded - thiscall, the argument words recorded (cdecl: as its callers pop)}.
    {0x004068F0, 0, 1, 0}, // &Tsk::BaseTask::Remove
    {0x00406C90, 4, 1, 1}, // &Tsk::Stack::AddTask
    {0x0041F1B0, 4, 1, 1}, // &Obj::CInputComponent::BindToController
    {0x004315A0, 8, 1, 2}, // &Obj::CShadowComponent::CreateShadow
    {0x00431780, 0, 1, 0}, // &Obj::CShadowComponent::DestroyShadow
    {0x00433E20, 0, 0, 0}, // Obj::NextSuspendFrame
    {0x0043D520, 0, 1, 0}, // &Obj::CVibrationComponent::StopAll
    {0x0043D560, 4, 1, 1}, // &Obj::CVibrationComponent::SetActive
    {0x00444460, 0, 0, 0}, // Terrain::ClearTerrainTypes
    {0x004444A0, 0, 0, 0}, // Terrain::LoadTerrainTypes
    {0x004499B0, 0, 0, 1}, // static_cast<void (*)(void*)>(&Script::CScript::operator delete)
    {0x0044E4E0, 0, 1, 0}, // &GameNet::Manager::InNetMode
    {0x0044E4F0, 0, 1, 0}, // &GameNet::Manager::InNetGame
    {0x0044E630, 4, 1, 1}, // &Inp::Manager::GetPlayerController
    {0x0044F790, 0, 1, 0}, // &Mdl::Module::DestroyBody
    {0x0044F7E0, 0, 1, 0}, // &Mdl::Module::Construct
    {0x004505F0, 0, 0, 0}, // Pcm::NoMusicPlease
    {0x00450620, 0, 0, 0}, // Pcm::StreamsDisabled
    {0x00450650, 0, 0, 0}, // Pcm::StopMusic
    {0x00450D60, 0, 0, 0}, // Pcm::Init
    {0x00455920, 4, 1, 1}, // &Net::Conn::UpdateCommTime
    {0x004573D0, 4, 1, 1}, // &Net::App::NextConnection
    {0x00457EB0, 8, 1, 2}, // &Net::App::EnqueueMessage
    {0x00458AB0, 32, 1, 8}, // &Net::App::StreamMessage
    {0x00458BE0, 4, 1, 1}, // &Net::App::FirstConnection
    {0x00458CA0, 4, 1, 1}, // &Net::App::SendData
    {0x0045D990, 4, 1, 1}, // &Obj::CCompositeObject::Hide
    {0x0045DB90, 4, 1, 1}, // &Obj::CCompositeObject::GetComponent
    {0x0045F2D0, 4, 1, 1}, // &Obj::CCompositeObjectManager::GetFirstComponentByType
    {0x00461930, 4, 1, 1}, // &Obj::CObject::SetID
    {0x004619F0, 4, 1, 1}, // &Obj::CBaseManager::GetObjectByID
    {0x00461AB0, 0, 1, 0}, // &Obj::CGeneralManager::FlushDeadObjects
    {0x00461CC0, 8, 1, 2}, // &Obj::CBaseManager::ForEach
    {0x004637E0, 4, 1, 1}, // &Obj::CRefCounted::RemoveSmartPointer
    {0x004682B0, 0, 0, 0}, // Obj::Emitter_Cleanup
    {0x004683B0, 0, 0, 0}, // Obj::Emitter_Init
    {0x0046AB30, 0, 0, 0}, // Obj::Proxim_Init
    {0x0046AB50, 0, 0, 0}, // Obj::Proxim_Cleanup
    {0x0046B400, 0, 0, 0}, // Obj::Proxim_Update
    {0x0046B790, 0, 0, 3}, // Obj::GetRestartNode
    {0x0046C470, 0, 1, 0}, // &Prefs::Preferences::Init
    {0x0046C650, 4, 1, 1}, // &Prefs::Preferences::Load
    {0x004738E0, 0, 1, 0}, // &Script::CScript::Construct
    {0x00474420, 0, 1, 0}, // &Script::CScript::Destroy
    {0x004747A0, 12, 1, 3}, // static_cast<void (Script::CScript::*)(uint32_t, Script::CStruct*, Obj::CObject*)>( &Script::CScript::SetScript)
    {0x004750B0, 0, 1, 0}, // &Script::CScript::Update
    {0x004753C0, 0, 0, 0}, // Script::DeleteSpawnedScripts
    {0x004758F0, 0, 0, 4}, // static_cast<void (*)(const char*, Script::CStruct*, Obj::CObject*, bool)>(&Script::RunScript)
    {0x00478F60, 0, 0, 0}, // static_cast<Function<int, uint32_t>>(Script::GetInteger)
    {0x00478FF0, 0, 0, 0}, // static_cast<Function<const char*, const char*>>(Script::GetString)
    {0x0047B2A0, 0, 1, 0}, // &Sfx::CSfxManager::UpdateVoiceVolumes
    {0x00481340, 0, 0, 0}, // (not rewritten)
    {0x00485A00, 0, 1, 0}, // (not rewritten)
    {0x004893A0, 4, 1, 1}, // (not rewritten)
    {0x0048C0A0, 0, 0, 1}, // (not rewritten)
    {0x004915A0, 0, 0, 0}, // (not rewritten)
    {0x00496700, 0, 1, 0}, // (not rewritten)
    {0x0049AE30, 0, 1, 0}, // (not rewritten)
    {0x004A0950, 0, 0, 1}, // (not rewritten)
    {0x004A0970, 0, 0, 2}, // (not rewritten)
    {0x004A09C0, 0, 0, 1}, // (not rewritten)
    {0x004AAE20, 12, 1, 3}, // &CScreenElementManager::GetElement
    {0x004F45B0, 0, 1, 0}, // (not rewritten)
    {0x004F4970, 0, 0, 0}, // (not rewritten)
    {0x00510EA0, 4, 1, 1}, // &Obj::CSkaterEndRunComponent::EndRun
    {0x00510F30, 0, 1, 0}, // &Obj::CSkaterEndRunComponent::RunDone
    {0x0051A150, 0, 1, 0}, // &Obj::CSkaterStateHistoryComponent::IsNearLocalSkater
    {0x0051A1C0, 4, 1, 1}, // &Obj::CSkaterStateHistoryComponent::CheckForCollisions
    {0x00522410, 4, 1, 1}, // &Obj::CTrickComponent::SetTrickMappings
    {0x00532C50, 0, 1, 0}, // &Manager::GetMaxPlayers
    {0x00532EA0, 4, 1, 1}, // &Manager::GetClient
    {0x00532F70, 0, 1, 0}, // &Manager::GetNetworkPreferences
    {0x00532F80, 0, 1, 0}, // &Manager::GetTauntPreferences
    {0x00532FD0, 4, 1, 1}, // &Manager::GetStartInfoWord
    {0x00533080, 0, 1, 0}, // &Manager::GetLevelPreference
    {0x00533220, 0, 1, 0}, // &Manager::ClearCrown
    {0x00533300, 0, 1, 0}, // &Manager::SendReadyResponse
    {0x00533380, 0, 1, 0}, // &Manager::HasCreatedGoals
    {0x005333A0, 4, 1, 1}, // &Manager::PrepareCreatedGoals
    {0x005333E0, 0, 1, 0}, // &Manager::GetCreatedGoalsMessage
    {0x005333F0, 0, 1, 0}, // &Manager::GetCreatedGoalsSize
    {0x00533FA0, 0, 1, 0}, // &Manager::OnClient
    {0x00533FB0, 4, 1, 1}, // &Manager::CreateTeamFlags
    {0x00534340, 0, 1, 0}, // &Manager::PlayerCollisionEnabled
    {0x00536B90, 4, 1, 1}, // &Manager::SetLevel
    {0x00537010, 8, 1, 2}, // &Manager::NextPlayerInfo
    {0x00537070, 4, 1, 1}, // &Manager::FirstNewPlayerInfo
    {0x005372C0, 8, 1, 2}, // &Manager::FirstPlayerInfo
    {0x00537320, 0, 1, 0}, // &Manager::GetLocalPlayer
    {0x00537480, 0, 1, 0}, // &Manager::GetKingOfTheHill
    {0x00539880, 4, 1, 1}, // &Manager::CountTeamPlayers
    {0x005399F0, 0, 1, 0}, // &Manager::DisconnectClients
    {0x0053A580, 0, 1, 0}, // &Manager::NextCamPlayer
    {0x0053A910, 4, 1, 1}, // &GameNet::Manager::ObservePlayer
    {0x0053AEA0, 4, 1, 1}, // &GameNet::Manager::PlayerByObjectId
    {0x0053E910, 0, 1, 0}, // &GameNet::Manager::RemovePlayers
    {0x005430D0, 4, 1, 1}, // &PlayerInfo::SetReadyQueryOwed
    {0x00543300, 0, 1, 0}, // &PlayerInfo::IsLocalPlayer
    {0x00543410, 0, 1, 0}, // &PlayerInfo::ClearFlags
    {0x005434A0, 0, 1, 0}, // &PlayerInfo::GetConnHandle
    {0x005435F0, 4, 1, 1}, // &GameNet::Manager::GetTeamScore
    {0x005436E0, 4, 1, 1}, // &GameNet::Manager::GetPlayerScore
    {0x00544340, 4, 1, 1}, // &PlayerInfo::MarkAsKing
    {0x00544670, 4, 1, 1}, // &PlayerInfo::AwaitReadyResponse
    {0x0054F960, 4, 1, 1}, // &Mdl::FrontEnd::PauseGame
    {0x0054FFF0, 0, 1, 0}, // &Mdl::CCompetition::DestroyBody
    {0x005504B0, 4, 1, 1}, // &Mdl::CCompetition::Construct
    {0x00552E10, 0, 1, 0}, // &Game::CGameFlow::Construct
    {0x00552E30, 4, 1, 1}, // &Game::CGameFlow::Request
    {0x00552E40, 0, 1, 0}, // &Game::CGameFlow::Update
    {0x00553130, 0, 1, 0}, // &Game::CGameMode::NumTeams
    {0x00553190, 0, 1, 0}, // &Game::CGameMode::GetInitialNumberOfPlayers
    {0x005531D0, 0, 1, 0}, // &Game::CGameMode::GetMaximumNumberOfPlayers
    {0x00553220, 4, 1, 1}, // &Game::CGameMode::IsTrue
    {0x005532A0, 0, 1, 0}, // &Game::CGameMode::GetNameChecksum
    {0x005535F0, 0, 1, 0}, // &Game::CGameMode::IsTeamGame
    {0x00553910, 0, 1, 0}, // &Game::CGameMode::EndConditionsMet
    {0x00553A60, 0, 1, 0}, // &Game::CGameMode::GetFirstConditionValue
    {0x00553AD0, 4, 1, 1}, // &Game::CGameMode::LoadGameType
    {0x00553D10, 0, 1, 0}, // &Game::CGameMode::Reset
    {0x00553F10, 4, 1, 1}, // &Game::CGoal::SetTimeLeft
    {0x00554110, 0, 1, 0}, // &Game::CGoal::AllFlagsSet
    {0x00554220, 0, 1, 0}, // &Game::CGoal::IsLocked
    {0x00554280, 0, 1, 0}, // &Game::CGoal::GetGoalId
    {0x00554A70, 0, 1, 0}, // &Game::CGoal::RequiresCombo
    {0x00556110, 0, 1, 0}, // &Game::CGoal::DestroyBody
    {0x00556330, 12, 1, 3}, // &Game::CGoal::SetFlag
    {0x00556FE0, 4, 1, 1}, // &Game::CGoal::Construct
    {0x00557F90, 8, 1, 2}, // &Game::CGoalManager::GetGoal
    {0x00557FC0, 8, 1, 2}, // &Game::CGoalManager::ActivateGoal
    {0x00558030, 4, 1, 1}, // &Game::CGoalManager::WinGoal
    {0x00558180, 4, 1, 1}, // &Game::CGoalManager::LoseGoal
    {0x00558250, 4, 1, 1}, // &Game::CGoalManager::DeactivateAllGoals
    {0x00558380, 0, 1, 0}, // &Game::CGoalManager::UninitializeAllGoals
    {0x005583C0, 0, 1, 0}, // &Game::CGoalManager::InitializeAllGoals
    {0x00558AB0, 4, 1, 1}, // &Game::CGoalManager::NumGoalsBeatenBy
    {0x00558FD0, 0, 1, 0}, // &Game::CGoalManager::CreateGoalLevelObjects
    {0x005591B0, 0, 0, 0}, // Game::GetGoalManager
    {0x0055D9C0, 0, 1, 0}, // &Game::CGoalManager::Construct
    {0x0055DB50, 0, 1, 0}, // &Game::CGoalManager::DestroyBody
    {0x0055DF90, 0, 1, 0}, // &Game::CGoalManager::UnBeatAllGoals
    {0x005600D0, 0, 1, 0}, // &Mdl::CHorse::Construct
    {0x00560310, 0, 1, 0}, // &Mdl::CHorse::GetRestart
    {0x00561290, 4, 1, 1}, // &Mdl::Score::SetSkaterId
    {0x00561A80, 4, 1, 1}, // &Mdl::Score::SetTotalScore
    {0x00561B40, 0, 1, 0}, // &Mdl::Score::GetTotalScore
    {0x0056B890, 0, 1, 0}, // (not rewritten)
    {0x0056E2B0, 0, 1, 0}, // &Mdl::SkateNet::CreateNetworkTasks
    {0x00572BD0, 0, 1, 0}, // &Gfx::CTODManager::ClearDeadObjects
    {0x00574590, 0, 1, 0}, // &Gfx::CTODManager::Update
    {0x00576D40, 0, 1, 0}, // &Mdl::Viewer::RemoveViewerObject
    {0x00576EA0, 0, 0, 0}, // Mdl::GetViewer
    {0x005782E0, 0, 1, 0}, // &Obj::CCrownObject::IsWorn
    {0x0057A330, 0, 1, 0}, // &Obj::CMovieManager::Count
    {0x0057A4B0, 0, 1, 0}, // &Obj::CMovieManager::Construct
    {0x0057A530, 0, 1, 0}, // &Obj::CMovieManager::GetCamera
    {0x0057A5A0, 0, 1, 0}, // &Obj::CMovieManager::StopAll
    {0x0057A640, 0, 1, 0}, // &Obj::CMovieManager::ClearMovies
    {0x0057B630, 0, 1, 0}, // &Obj::CPlayerProfileManager::GetCurrentProfile
    {0x0057B640, 4, 1, 1}, // &Obj::CPlayerProfileManager::GetProfile
    {0x0057B690, 0, 1, 0}, // &Obj::CPlayerProfileManager::Init
    {0x0057B800, 0, 1, 0}, // &Obj::CPlayerProfileManager::ResetTemplates
    {0x0057C330, 0, 1, 0}, // &Obj::CRailManager::Cleanup
    {0x0057CC10, 0, 1, 0}, // &Obj::CRailManager::Init
    {0x0057ECD0, 8, 1, 2}, // &Records::CRecord::MaybeNewRecord
    {0x0057EEC0, 4, 1, 1}, // &Records::CGameRecords::GetLevelRecords
    {0x0057EFE0, 8, 1, 2}, // &Records::CRecordTable::MaybeNewEntry
    {0x0057FBC0, 4, 1, 1}, // &Records::CGameRecords::InitLevels
    {0x0057FEC0, 0, 1, 0}, // &Obj::CSkater::GetSkaterNumber
    {0x00580060, 4, 1, 1}, // &Obj::CSkater::LoadCATInfo
    {0x00580150, 4, 1, 1}, // &Obj::CSkater::LoadList44B66A9F
    {0x00580480, 0, 1, 0}, // &Obj::CSkater::GetScore
    {0x00580880, 0, 1, 0}, // &Obj::CSkater::GetSkaterCamera
    {0x005814D0, 0, 1, 0}, // &Obj::CSkater::Reset27C
    {0x00581510, 0, 1, 0}, // &Obj::CSkater::HideSkater
    {0x00581550, 0, 1, 0}, // &Obj::CSkater::ShowSkater
    {0x00581580, 0, 1, 0}, // &Obj::CSkater::GetDisplayName
    {0x005825E0, 4, 1, 1}, // &Obj::CSkater::WriteCATInfo
    {0x00582740, 4, 1, 1}, // &Obj::CSkater::WriteList44B66A9F
    {0x00582D90, 24, 1, 6}, // &Obj::CSkater::Construct
    {0x00583400, 0, 1, 0}, // &Obj::CSkater::ResetComponents
    {0x00583470, 8, 1, 2}, // &Obj::CSkater::Restart
    {0x00584110, 4, 1, 1}, // &Obj::CSkaterCareer::GetGapChecklist
    {0x00584130, 0, 1, 0}, // &Obj::CSkaterCareer::GetLevel
    {0x00584C60, 4, 1, 1}, // &Obj::CSkaterCareer::StartLevel
    {0x00584CC0, 4, 1, 1}, // &Obj::CSkaterCareer::GetCheat
    {0x00585270, 0, 1, 0}, // &Obj::CSkaterCareer::Construct
    {0x00587120, 0, 1, 0}, // &Obj::ChecksumSet::Construct
    {0x00587130, 0, 1, 0}, // &Obj::ChecksumSet::Fill
    {0x005889F0, 0, 1, 0}, // &Obj::CTrickObjectManager::RemoveAllClusters
    {0x005890B0, 0, 1, 0}, // &Obj::CTrickObjectManager::ApplyPlayerGraffiti
    {0x0058D690, 4, 1, 1}, // &CParkEditor::GetCompressedMapBuffer
    {0x0059FE50, 0, 0, 2}, // CFuncs::CFunc_Cleanup
    {0x005A0D90, 0, 0, 0}, // CFuncs::CFunc_IsObserving
    {0x005A1180, 0, 0, 2}, // CFuncs::CFunc_InSplitScreenGame
    {0x005A34E0, 0, 0, 0}, // CFuncs::CFunc_ParseNodeArray
    {0x005BACF0, 0, 1, 0}, // &Mem::Manager::DeleteExtraSkaterHeaps
    {0x005BB820, 4, 1, 1}, // &Mem::Manager::InitExtraSkaterHeaps
    {0x005BCC60, 0, 1, 0}, // &Sio::Device::IsPluggedIn
    {0x005BE970, 0, 0, 0}, // Tmr::GetTime
