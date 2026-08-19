#include "GodfreyBrainLauncher.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Interfaces/IPv4/IPv4Address.h"

DEFINE_LOG_CATEGORY_STATIC(LogGodfreyBrainLauncher, Log, All);

FProcHandle FGodfreyBrainLauncher::OwnedProcessHandle;
bool FGodfreyBrainLauncher::bOwnsProcess = false;

namespace GodfreyBrainLauncherPrivate
{
	static FCriticalSection GLock;
}

bool FGodfreyBrainLauncher::ParseHostAndPort(const FString& BrainBaseUrl, FString& OutHost, int32& OutPort)
{
	OutHost = TEXT("127.0.0.1");
	OutPort = 3000;

	FString Url = BrainBaseUrl.TrimStartAndEnd();
	if (Url.IsEmpty())
	{
		return true;
	}

	Url.ReplaceInline(TEXT("http://"), TEXT(""), ESearchCase::IgnoreCase);
	Url.ReplaceInline(TEXT("https://"), TEXT(""), ESearchCase::IgnoreCase);

	int32 SlashIdx = INDEX_NONE;
	if (Url.FindChar(TEXT('/'), SlashIdx))
	{
		Url.LeftInline(SlashIdx);
	}

	FString HostPart;
	FString PortPart;
	if (Url.Split(TEXT(":"), &HostPart, &PortPart))
	{
		OutHost = HostPart.TrimStartAndEnd();
		OutPort = FCString::Atoi(*PortPart);
		if (OutPort <= 0)
		{
			OutPort = 3000;
		}
	}
	else
	{
		OutHost = Url.TrimStartAndEnd();
	}

	if (OutHost.Equals(TEXT("localhost"), ESearchCase::IgnoreCase))
	{
		OutHost = TEXT("127.0.0.1");
	}
	return !OutHost.IsEmpty();
}

bool FGodfreyBrainLauncher::ProbeBrainReachable(const FString& BrainBaseUrl)
{
	FString Host;
	int32 Port = 3000;
	if (!ParseHostAndPort(BrainBaseUrl, Host, Port))
	{
		return false;
	}

	ISocketSubsystem* const Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets)
	{
		return false;
	}

	const TSharedRef<FInternetAddr> Addr = Sockets->CreateInternetAddr();
	bool bIsValid = false;
	Addr->SetIp(*Host, bIsValid);
	if (!bIsValid)
	{
		FIPv4Address IPv4;
		if (FIPv4Address::Parse(Host, IPv4))
		{
			Addr->SetIp(IPv4.Value);
			bIsValid = true;
		}
	}
	if (!bIsValid)
	{
		return false;
	}
	Addr->SetPort(Port);

	FSocket* const Socket = Sockets->CreateSocket(NAME_Stream, TEXT("GodfreyBrainProbe"), false);
	if (!Socket)
	{
		return false;
	}

	Socket->SetNonBlocking(true);
	Socket->Connect(*Addr);

	const double Deadline = FPlatformTime::Seconds() + 0.35;
	bool bConnected = false;
	while (FPlatformTime::Seconds() < Deadline)
	{
		const ESocketConnectionState State = Socket->GetConnectionState();
		if (State == SCS_Connected)
		{
			bConnected = true;
			break;
		}
		if (State == SCS_ConnectionError)
		{
			break;
		}
		FPlatformProcess::Sleep(0.02f);
	}

	Socket->Close();
	Sockets->DestroySocket(Socket);
	return bConnected;
}

FString FGodfreyBrainLauncher::ResolveNodeExecutable(const FString& ConfiguredPath)
{
	if (!ConfiguredPath.IsEmpty() && FPaths::FileExists(ConfiguredPath))
	{
		return ConfiguredPath;
	}

	const TArray<FString> Candidates = {
		TEXT("C:/Program Files/nodejs/node.exe"),
		TEXT("C:/Program Files (x86)/nodejs/node.exe"),
	};
	for (const FString& Candidate : Candidates)
	{
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}

	// Last resort: rely on PATH resolution via CreateProc (may fail on some Windows setups).
	return TEXT("node");
}

bool FGodfreyBrainLauncher::LaunchBrainProcess(
	const FString& WorkingDirectory,
	const FString& NodeExecutable,
	const FString& StartScript,
	const bool bShowConsoleWindow)
{
	if (WorkingDirectory.IsEmpty() || !FPaths::DirectoryExists(WorkingDirectory))
	{
		UE_LOG(LogGodfreyBrainLauncher, Error,
			TEXT("Cannot start Godfrey Brain — working directory missing: %s"),
			*WorkingDirectory);
		return false;
	}

	const FString ScriptPath = FPaths::Combine(WorkingDirectory, StartScript);
	if (!FPaths::FileExists(ScriptPath))
	{
		UE_LOG(LogGodfreyBrainLauncher, Error,
			TEXT("Cannot start Godfrey Brain — script missing: %s"),
			*ScriptPath);
		return false;
	}

	const FString NodePath = ResolveNodeExecutable(NodeExecutable);
	const FString Params = FString::Printf(TEXT("\"%s\""), *ScriptPath);

	UE_LOG(LogGodfreyBrainLauncher, Log,
		TEXT("Starting Godfrey Brain: exe=%s params=%s cwd=%s showConsole=%d"),
		*NodePath, *Params, *WorkingDirectory, bShowConsoleWindow ? 1 : 0);

	const bool bLaunchDetached = true;
	const bool bLaunchHidden = !bShowConsoleWindow;
	const bool bLaunchReallyHidden = false;

	OwnedProcessHandle = FPlatformProcess::CreateProc(
		*NodePath,
		*Params,
		bLaunchDetached,
		bLaunchHidden,
		bLaunchReallyHidden,
		nullptr,
		0,
		*WorkingDirectory,
		nullptr);

	if (!OwnedProcessHandle.IsValid())
	{
		UE_LOG(LogGodfreyBrainLauncher, Error,
			TEXT("Failed to CreateProc for Godfrey Brain (node=%s). Is Node.js installed?"),
			*NodePath);
		bOwnsProcess = false;
		return false;
	}

	bOwnsProcess = true;
	UE_LOG(LogGodfreyBrainLauncher, Log, TEXT("Godfrey Brain process started (owned by UnrealPerformer)."));
	return true;
}

bool FGodfreyBrainLauncher::EnsureRunning(
	const FString& BrainBaseUrl,
	const FString& WorkingDirectory,
	const FString& NodeExecutable,
	const FString& StartScript,
	const bool bShowConsoleWindow)
{
	FScopeLock Lock(&GodfreyBrainLauncherPrivate::GLock);

	if (ProbeBrainReachable(BrainBaseUrl))
	{
		UE_LOG(LogGodfreyBrainLauncher, Log,
			TEXT("Godfrey Brain already reachable at %s — skip launch."), *BrainBaseUrl);
		return true;
	}

	if (bOwnsProcess && OwnedProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(OwnedProcessHandle))
	{
		UE_LOG(LogGodfreyBrainLauncher, Log,
			TEXT("Godfrey Brain process still starting; not reachable yet at %s."), *BrainBaseUrl);
		return true;
	}

	return LaunchBrainProcess(WorkingDirectory, NodeExecutable, StartScript, bShowConsoleWindow);
}

void FGodfreyBrainLauncher::StopOwnedProcess()
{
	FScopeLock Lock(&GodfreyBrainLauncherPrivate::GLock);
	if (!bOwnsProcess || !OwnedProcessHandle.IsValid())
	{
		return;
	}

	if (FPlatformProcess::IsProcRunning(OwnedProcessHandle))
	{
		UE_LOG(LogGodfreyBrainLauncher, Log, TEXT("Stopping owned Godfrey Brain process."));
		FPlatformProcess::TerminateProc(OwnedProcessHandle, true);
	}
	FPlatformProcess::CloseProc(OwnedProcessHandle);
	OwnedProcessHandle = FProcHandle();
	bOwnsProcess = false;
}

bool FGodfreyBrainLauncher::IsOwnedProcessRunning()
{
	FScopeLock Lock(&GodfreyBrainLauncherPrivate::GLock);
	return bOwnsProcess && OwnedProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(OwnedProcessHandle);
}
