// winmm exports (from `dumpbin /exports` of winmm 10.0.26100, same names and ordinals). Each one is a stub in
// src/winmm_stubs.asm that jumps through g_kmWinmm to the real System32 winmm.dll, loaded and resolved in DllMain.
// Linker forwarders to a \\.\GLOBALROOT path did this before, but Wine / Proton never resolves them: the game's
// winmm imports stayed empty and it crashed at boot on Steam Deck.
#pragma once

inline constexpr int kWinmmCount = 181;
extern "C" void* g_kmWinmm[kWinmmCount]; // filled in DllMain, read by the stubs

// What each stub imports from the real DLL: a name, or "#n" for an export that only has an ordinal.
inline const char* const kWinmmImports[kWinmmCount] = {
    "CloseDriver",
    "DefDriverProc",
    "DriverCallback",
    "DrvGetModuleHandle",
    "GetDriverModuleHandle",
    "OpenDriver",
    "PlaySound",
    "PlaySoundA",
    "PlaySoundW",
    "SendDriverMessage",
    "WOWAppExit",
    "auxGetDevCapsA",
    "auxGetDevCapsW",
    "auxGetNumDevs",
    "auxGetVolume",
    "auxOutMessage",
    "auxSetVolume",
    "joyConfigChanged",
    "joyGetDevCapsA",
    "joyGetDevCapsW",
    "joyGetNumDevs",
    "joyGetPos",
    "joyGetPosEx",
    "joyGetThreshold",
    "joyReleaseCapture",
    "joySetCapture",
    "joySetThreshold",
    "mciDriverNotify",
    "mciDriverYield",
    "mciExecute",
    "mciFreeCommandResource",
    "mciGetCreatorTask",
    "mciGetDeviceIDA",
    "mciGetDeviceIDFromElementIDA",
    "mciGetDeviceIDFromElementIDW",
    "mciGetDeviceIDW",
    "mciGetDriverData",
    "mciGetErrorStringA",
    "mciGetErrorStringW",
    "mciGetYieldProc",
    "mciLoadCommandResource",
    "mciSendCommandA",
    "mciSendCommandW",
    "mciSendStringA",
    "mciSendStringW",
    "mciSetDriverData",
    "mciSetYieldProc",
    "midiConnect",
    "midiDisconnect",
    "midiInAddBuffer",
    "midiInClose",
    "midiInGetDevCapsA",
    "midiInGetDevCapsW",
    "midiInGetErrorTextA",
    "midiInGetErrorTextW",
    "midiInGetID",
    "midiInGetNumDevs",
    "midiInMessage",
    "midiInOpen",
    "midiInPrepareHeader",
    "midiInReset",
    "midiInStart",
    "midiInStop",
    "midiInUnprepareHeader",
    "midiOutCacheDrumPatches",
    "midiOutCachePatches",
    "midiOutClose",
    "midiOutGetDevCapsA",
    "midiOutGetDevCapsW",
    "midiOutGetErrorTextA",
    "midiOutGetErrorTextW",
    "midiOutGetID",
    "midiOutGetNumDevs",
    "midiOutGetVolume",
    "midiOutLongMsg",
    "midiOutMessage",
    "midiOutOpen",
    "midiOutPrepareHeader",
    "midiOutReset",
    "midiOutSetVolume",
    "midiOutShortMsg",
    "midiOutUnprepareHeader",
    "midiStreamClose",
    "midiStreamOpen",
    "midiStreamOut",
    "midiStreamPause",
    "midiStreamPosition",
    "midiStreamProperty",
    "midiStreamRestart",
    "midiStreamStop",
    "mixerClose",
    "mixerGetControlDetailsA",
    "mixerGetControlDetailsW",
    "mixerGetDevCapsA",
    "mixerGetDevCapsW",
    "mixerGetID",
    "mixerGetLineControlsA",
    "mixerGetLineControlsW",
    "mixerGetLineInfoA",
    "mixerGetLineInfoW",
    "mixerGetNumDevs",
    "mixerMessage",
    "mixerOpen",
    "mixerSetControlDetails",
    "mmDrvInstall",
    "mmGetCurrentTask",
    "mmTaskBlock",
    "mmTaskCreate",
    "mmTaskSignal",
    "mmTaskYield",
    "mmioAdvance",
    "mmioAscend",
    "mmioClose",
    "mmioCreateChunk",
    "mmioDescend",
    "mmioFlush",
    "mmioGetInfo",
    "mmioInstallIOProcA",
    "mmioInstallIOProcW",
    "mmioOpenA",
    "mmioOpenW",
    "mmioRead",
    "mmioRenameA",
    "mmioRenameW",
    "mmioSeek",
    "mmioSendMessage",
    "mmioSetBuffer",
    "mmioSetInfo",
    "mmioStringToFOURCCA",
    "mmioStringToFOURCCW",
    "mmioWrite",
    "mmsystemGetVersion",
    "sndPlaySoundA",
    "sndPlaySoundW",
    "timeBeginPeriod",
    "timeEndPeriod",
    "timeGetDevCaps",
    "timeGetSystemTime",
    "timeGetTime",
    "timeKillEvent",
    "timeSetEvent",
    "waveInAddBuffer",
    "waveInClose",
    "waveInGetDevCapsA",
    "waveInGetDevCapsW",
    "waveInGetErrorTextA",
    "waveInGetErrorTextW",
    "waveInGetID",
    "waveInGetNumDevs",
    "waveInGetPosition",
    "waveInMessage",
    "waveInOpen",
    "waveInPrepareHeader",
    "waveInReset",
    "waveInStart",
    "waveInStop",
    "waveInUnprepareHeader",
    "waveOutBreakLoop",
    "waveOutClose",
    "waveOutGetDevCapsA",
    "waveOutGetDevCapsW",
    "waveOutGetErrorTextA",
    "waveOutGetErrorTextW",
    "waveOutGetID",
    "waveOutGetNumDevs",
    "waveOutGetPitch",
    "waveOutGetPlaybackRate",
    "waveOutGetPosition",
    "waveOutGetVolume",
    "waveOutMessage",
    "waveOutOpen",
    "waveOutPause",
    "waveOutPrepareHeader",
    "waveOutReset",
    "waveOutRestart",
    "waveOutSetPitch",
    "waveOutSetPlaybackRate",
    "waveOutSetVolume",
    "waveOutUnprepareHeader",
    "waveOutWrite",
    "#2",
};

#pragma comment(linker, "/EXPORT:CloseDriver=KmStub0,@4")
#pragma comment(linker, "/EXPORT:DefDriverProc=KmStub1,@5")
#pragma comment(linker, "/EXPORT:DriverCallback=KmStub2,@6")
#pragma comment(linker, "/EXPORT:DrvGetModuleHandle=KmStub3,@7")
#pragma comment(linker, "/EXPORT:GetDriverModuleHandle=KmStub4,@8")
#pragma comment(linker, "/EXPORT:OpenDriver=KmStub5,@9")
#pragma comment(linker, "/EXPORT:PlaySound=KmStub6,@10")
#pragma comment(linker, "/EXPORT:PlaySoundA=KmStub7,@11")
#pragma comment(linker, "/EXPORT:PlaySoundW=KmStub8,@12")
#pragma comment(linker, "/EXPORT:SendDriverMessage=KmStub9,@13")
#pragma comment(linker, "/EXPORT:WOWAppExit=KmStub10,@14")
#pragma comment(linker, "/EXPORT:auxGetDevCapsA=KmStub11,@15")
#pragma comment(linker, "/EXPORT:auxGetDevCapsW=KmStub12,@16")
#pragma comment(linker, "/EXPORT:auxGetNumDevs=KmStub13,@17")
#pragma comment(linker, "/EXPORT:auxGetVolume=KmStub14,@18")
#pragma comment(linker, "/EXPORT:auxOutMessage=KmStub15,@19")
#pragma comment(linker, "/EXPORT:auxSetVolume=KmStub16,@20")
#pragma comment(linker, "/EXPORT:joyConfigChanged=KmStub17,@21")
#pragma comment(linker, "/EXPORT:joyGetDevCapsA=KmStub18,@22")
#pragma comment(linker, "/EXPORT:joyGetDevCapsW=KmStub19,@23")
#pragma comment(linker, "/EXPORT:joyGetNumDevs=KmStub20,@24")
#pragma comment(linker, "/EXPORT:joyGetPos=KmStub21,@25")
#pragma comment(linker, "/EXPORT:joyGetPosEx=KmStub22,@26")
#pragma comment(linker, "/EXPORT:joyGetThreshold=KmStub23,@27")
#pragma comment(linker, "/EXPORT:joyReleaseCapture=KmStub24,@28")
#pragma comment(linker, "/EXPORT:joySetCapture=KmStub25,@29")
#pragma comment(linker, "/EXPORT:joySetThreshold=KmStub26,@30")
#pragma comment(linker, "/EXPORT:mciDriverNotify=KmStub27,@31")
#pragma comment(linker, "/EXPORT:mciDriverYield=KmStub28,@32")
#pragma comment(linker, "/EXPORT:mciExecute=KmStub29,@3")
#pragma comment(linker, "/EXPORT:mciFreeCommandResource=KmStub30,@33")
#pragma comment(linker, "/EXPORT:mciGetCreatorTask=KmStub31,@34")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDA=KmStub32,@35")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDFromElementIDA=KmStub33,@36")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDFromElementIDW=KmStub34,@37")
#pragma comment(linker, "/EXPORT:mciGetDeviceIDW=KmStub35,@38")
#pragma comment(linker, "/EXPORT:mciGetDriverData=KmStub36,@39")
#pragma comment(linker, "/EXPORT:mciGetErrorStringA=KmStub37,@40")
#pragma comment(linker, "/EXPORT:mciGetErrorStringW=KmStub38,@41")
#pragma comment(linker, "/EXPORT:mciGetYieldProc=KmStub39,@42")
#pragma comment(linker, "/EXPORT:mciLoadCommandResource=KmStub40,@43")
#pragma comment(linker, "/EXPORT:mciSendCommandA=KmStub41,@44")
#pragma comment(linker, "/EXPORT:mciSendCommandW=KmStub42,@45")
#pragma comment(linker, "/EXPORT:mciSendStringA=KmStub43,@46")
#pragma comment(linker, "/EXPORT:mciSendStringW=KmStub44,@47")
#pragma comment(linker, "/EXPORT:mciSetDriverData=KmStub45,@48")
#pragma comment(linker, "/EXPORT:mciSetYieldProc=KmStub46,@49")
#pragma comment(linker, "/EXPORT:midiConnect=KmStub47,@50")
#pragma comment(linker, "/EXPORT:midiDisconnect=KmStub48,@51")
#pragma comment(linker, "/EXPORT:midiInAddBuffer=KmStub49,@52")
#pragma comment(linker, "/EXPORT:midiInClose=KmStub50,@53")
#pragma comment(linker, "/EXPORT:midiInGetDevCapsA=KmStub51,@54")
#pragma comment(linker, "/EXPORT:midiInGetDevCapsW=KmStub52,@55")
#pragma comment(linker, "/EXPORT:midiInGetErrorTextA=KmStub53,@56")
#pragma comment(linker, "/EXPORT:midiInGetErrorTextW=KmStub54,@57")
#pragma comment(linker, "/EXPORT:midiInGetID=KmStub55,@58")
#pragma comment(linker, "/EXPORT:midiInGetNumDevs=KmStub56,@59")
#pragma comment(linker, "/EXPORT:midiInMessage=KmStub57,@60")
#pragma comment(linker, "/EXPORT:midiInOpen=KmStub58,@61")
#pragma comment(linker, "/EXPORT:midiInPrepareHeader=KmStub59,@62")
#pragma comment(linker, "/EXPORT:midiInReset=KmStub60,@63")
#pragma comment(linker, "/EXPORT:midiInStart=KmStub61,@64")
#pragma comment(linker, "/EXPORT:midiInStop=KmStub62,@65")
#pragma comment(linker, "/EXPORT:midiInUnprepareHeader=KmStub63,@66")
#pragma comment(linker, "/EXPORT:midiOutCacheDrumPatches=KmStub64,@67")
#pragma comment(linker, "/EXPORT:midiOutCachePatches=KmStub65,@68")
#pragma comment(linker, "/EXPORT:midiOutClose=KmStub66,@69")
#pragma comment(linker, "/EXPORT:midiOutGetDevCapsA=KmStub67,@70")
#pragma comment(linker, "/EXPORT:midiOutGetDevCapsW=KmStub68,@71")
#pragma comment(linker, "/EXPORT:midiOutGetErrorTextA=KmStub69,@72")
#pragma comment(linker, "/EXPORT:midiOutGetErrorTextW=KmStub70,@73")
#pragma comment(linker, "/EXPORT:midiOutGetID=KmStub71,@74")
#pragma comment(linker, "/EXPORT:midiOutGetNumDevs=KmStub72,@75")
#pragma comment(linker, "/EXPORT:midiOutGetVolume=KmStub73,@76")
#pragma comment(linker, "/EXPORT:midiOutLongMsg=KmStub74,@77")
#pragma comment(linker, "/EXPORT:midiOutMessage=KmStub75,@78")
#pragma comment(linker, "/EXPORT:midiOutOpen=KmStub76,@79")
#pragma comment(linker, "/EXPORT:midiOutPrepareHeader=KmStub77,@80")
#pragma comment(linker, "/EXPORT:midiOutReset=KmStub78,@81")
#pragma comment(linker, "/EXPORT:midiOutSetVolume=KmStub79,@82")
#pragma comment(linker, "/EXPORT:midiOutShortMsg=KmStub80,@83")
#pragma comment(linker, "/EXPORT:midiOutUnprepareHeader=KmStub81,@84")
#pragma comment(linker, "/EXPORT:midiStreamClose=KmStub82,@85")
#pragma comment(linker, "/EXPORT:midiStreamOpen=KmStub83,@86")
#pragma comment(linker, "/EXPORT:midiStreamOut=KmStub84,@87")
#pragma comment(linker, "/EXPORT:midiStreamPause=KmStub85,@88")
#pragma comment(linker, "/EXPORT:midiStreamPosition=KmStub86,@89")
#pragma comment(linker, "/EXPORT:midiStreamProperty=KmStub87,@90")
#pragma comment(linker, "/EXPORT:midiStreamRestart=KmStub88,@91")
#pragma comment(linker, "/EXPORT:midiStreamStop=KmStub89,@92")
#pragma comment(linker, "/EXPORT:mixerClose=KmStub90,@93")
#pragma comment(linker, "/EXPORT:mixerGetControlDetailsA=KmStub91,@94")
#pragma comment(linker, "/EXPORT:mixerGetControlDetailsW=KmStub92,@95")
#pragma comment(linker, "/EXPORT:mixerGetDevCapsA=KmStub93,@96")
#pragma comment(linker, "/EXPORT:mixerGetDevCapsW=KmStub94,@97")
#pragma comment(linker, "/EXPORT:mixerGetID=KmStub95,@98")
#pragma comment(linker, "/EXPORT:mixerGetLineControlsA=KmStub96,@99")
#pragma comment(linker, "/EXPORT:mixerGetLineControlsW=KmStub97,@100")
#pragma comment(linker, "/EXPORT:mixerGetLineInfoA=KmStub98,@101")
#pragma comment(linker, "/EXPORT:mixerGetLineInfoW=KmStub99,@102")
#pragma comment(linker, "/EXPORT:mixerGetNumDevs=KmStub100,@103")
#pragma comment(linker, "/EXPORT:mixerMessage=KmStub101,@104")
#pragma comment(linker, "/EXPORT:mixerOpen=KmStub102,@105")
#pragma comment(linker, "/EXPORT:mixerSetControlDetails=KmStub103,@106")
#pragma comment(linker, "/EXPORT:mmDrvInstall=KmStub104,@107")
#pragma comment(linker, "/EXPORT:mmGetCurrentTask=KmStub105,@108")
#pragma comment(linker, "/EXPORT:mmTaskBlock=KmStub106,@109")
#pragma comment(linker, "/EXPORT:mmTaskCreate=KmStub107,@110")
#pragma comment(linker, "/EXPORT:mmTaskSignal=KmStub108,@111")
#pragma comment(linker, "/EXPORT:mmTaskYield=KmStub109,@112")
#pragma comment(linker, "/EXPORT:mmioAdvance=KmStub110,@113")
#pragma comment(linker, "/EXPORT:mmioAscend=KmStub111,@114")
#pragma comment(linker, "/EXPORT:mmioClose=KmStub112,@115")
#pragma comment(linker, "/EXPORT:mmioCreateChunk=KmStub113,@116")
#pragma comment(linker, "/EXPORT:mmioDescend=KmStub114,@117")
#pragma comment(linker, "/EXPORT:mmioFlush=KmStub115,@118")
#pragma comment(linker, "/EXPORT:mmioGetInfo=KmStub116,@119")
#pragma comment(linker, "/EXPORT:mmioInstallIOProcA=KmStub117,@120")
#pragma comment(linker, "/EXPORT:mmioInstallIOProcW=KmStub118,@121")
#pragma comment(linker, "/EXPORT:mmioOpenA=KmStub119,@122")
#pragma comment(linker, "/EXPORT:mmioOpenW=KmStub120,@123")
#pragma comment(linker, "/EXPORT:mmioRead=KmStub121,@124")
#pragma comment(linker, "/EXPORT:mmioRenameA=KmStub122,@125")
#pragma comment(linker, "/EXPORT:mmioRenameW=KmStub123,@126")
#pragma comment(linker, "/EXPORT:mmioSeek=KmStub124,@127")
#pragma comment(linker, "/EXPORT:mmioSendMessage=KmStub125,@128")
#pragma comment(linker, "/EXPORT:mmioSetBuffer=KmStub126,@129")
#pragma comment(linker, "/EXPORT:mmioSetInfo=KmStub127,@130")
#pragma comment(linker, "/EXPORT:mmioStringToFOURCCA=KmStub128,@131")
#pragma comment(linker, "/EXPORT:mmioStringToFOURCCW=KmStub129,@132")
#pragma comment(linker, "/EXPORT:mmioWrite=KmStub130,@133")
#pragma comment(linker, "/EXPORT:mmsystemGetVersion=KmStub131,@134")
#pragma comment(linker, "/EXPORT:sndPlaySoundA=KmStub132,@135")
#pragma comment(linker, "/EXPORT:sndPlaySoundW=KmStub133,@136")
#pragma comment(linker, "/EXPORT:timeBeginPeriod=KmStub134,@137")
#pragma comment(linker, "/EXPORT:timeEndPeriod=KmStub135,@138")
#pragma comment(linker, "/EXPORT:timeGetDevCaps=KmStub136,@139")
#pragma comment(linker, "/EXPORT:timeGetSystemTime=KmStub137,@140")
#pragma comment(linker, "/EXPORT:timeGetTime=KmStub138,@141")
#pragma comment(linker, "/EXPORT:timeKillEvent=KmStub139,@142")
#pragma comment(linker, "/EXPORT:timeSetEvent=KmStub140,@143")
#pragma comment(linker, "/EXPORT:waveInAddBuffer=KmStub141,@144")
#pragma comment(linker, "/EXPORT:waveInClose=KmStub142,@145")
#pragma comment(linker, "/EXPORT:waveInGetDevCapsA=KmStub143,@146")
#pragma comment(linker, "/EXPORT:waveInGetDevCapsW=KmStub144,@147")
#pragma comment(linker, "/EXPORT:waveInGetErrorTextA=KmStub145,@148")
#pragma comment(linker, "/EXPORT:waveInGetErrorTextW=KmStub146,@149")
#pragma comment(linker, "/EXPORT:waveInGetID=KmStub147,@150")
#pragma comment(linker, "/EXPORT:waveInGetNumDevs=KmStub148,@151")
#pragma comment(linker, "/EXPORT:waveInGetPosition=KmStub149,@152")
#pragma comment(linker, "/EXPORT:waveInMessage=KmStub150,@153")
#pragma comment(linker, "/EXPORT:waveInOpen=KmStub151,@154")
#pragma comment(linker, "/EXPORT:waveInPrepareHeader=KmStub152,@155")
#pragma comment(linker, "/EXPORT:waveInReset=KmStub153,@156")
#pragma comment(linker, "/EXPORT:waveInStart=KmStub154,@157")
#pragma comment(linker, "/EXPORT:waveInStop=KmStub155,@158")
#pragma comment(linker, "/EXPORT:waveInUnprepareHeader=KmStub156,@159")
#pragma comment(linker, "/EXPORT:waveOutBreakLoop=KmStub157,@160")
#pragma comment(linker, "/EXPORT:waveOutClose=KmStub158,@161")
#pragma comment(linker, "/EXPORT:waveOutGetDevCapsA=KmStub159,@162")
#pragma comment(linker, "/EXPORT:waveOutGetDevCapsW=KmStub160,@163")
#pragma comment(linker, "/EXPORT:waveOutGetErrorTextA=KmStub161,@164")
#pragma comment(linker, "/EXPORT:waveOutGetErrorTextW=KmStub162,@165")
#pragma comment(linker, "/EXPORT:waveOutGetID=KmStub163,@166")
#pragma comment(linker, "/EXPORT:waveOutGetNumDevs=KmStub164,@167")
#pragma comment(linker, "/EXPORT:waveOutGetPitch=KmStub165,@168")
#pragma comment(linker, "/EXPORT:waveOutGetPlaybackRate=KmStub166,@169")
#pragma comment(linker, "/EXPORT:waveOutGetPosition=KmStub167,@170")
#pragma comment(linker, "/EXPORT:waveOutGetVolume=KmStub168,@171")
#pragma comment(linker, "/EXPORT:waveOutMessage=KmStub169,@172")
#pragma comment(linker, "/EXPORT:waveOutOpen=KmStub170,@173")
#pragma comment(linker, "/EXPORT:waveOutPause=KmStub171,@174")
#pragma comment(linker, "/EXPORT:waveOutPrepareHeader=KmStub172,@175")
#pragma comment(linker, "/EXPORT:waveOutReset=KmStub173,@176")
#pragma comment(linker, "/EXPORT:waveOutRestart=KmStub174,@177")
#pragma comment(linker, "/EXPORT:waveOutSetPitch=KmStub175,@178")
#pragma comment(linker, "/EXPORT:waveOutSetPlaybackRate=KmStub176,@179")
#pragma comment(linker, "/EXPORT:waveOutSetVolume=KmStub177,@180")
#pragma comment(linker, "/EXPORT:waveOutUnprepareHeader=KmStub178,@181")
#pragma comment(linker, "/EXPORT:waveOutWrite=KmStub179,@182")
#pragma comment(linker, "/EXPORT:KmOrdinal2=KmStub180,@2,NONAME")
