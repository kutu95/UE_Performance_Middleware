#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

/**
 * Local Godfrey Brain (Node) process helper for PIE / Development.
 * Probes GodfreyBrainBaseUrl; if unreachable, starts `node server.js` in the configured working directory.
 */
struct FGodfreyBrainLauncher
{
	/** Returns true if Brain already responds or a new process was started successfully. */
	static bool EnsureRunning(
		const FString& BrainBaseUrl,
		const FString& WorkingDirectory,
		const FString& NodeExecutable,
		const FString& StartScript,
		bool bShowConsoleWindow);

	/** Terminate a process we launched (no-op if Brain was already running externally). */
	static void StopOwnedProcess();

	static bool IsOwnedProcessRunning();

private:
	static bool ProbeBrainReachable(const FString& BrainBaseUrl);
	static bool ParseHostAndPort(const FString& BrainBaseUrl, FString& OutHost, int32& OutPort);
	static FString ResolveNodeExecutable(const FString& ConfiguredPath);
	static bool LaunchBrainProcess(
		const FString& WorkingDirectory,
		const FString& NodeExecutable,
		const FString& StartScript,
		bool bShowConsoleWindow);

	static FProcHandle OwnedProcessHandle;
	static bool bOwnsProcess;
};
