using System;
using IDE;
using BeefBuild;
using Beefy.utils;

namespace GodotBeefBuild
{
	// Resident BeefBuild: overriding Stop() keeps the app (and its warm type system) alive across
	// builds instead of BFApp_Shutdown-ing. Compile completion sets mBuildDone instead.
	class ResidentBuildApp : BuildApp
	{
		public bool mBuildDone;
		public override void Stop()
		{
			mBuildDone = true;
		}
	}

	// C ABI surface — godot.exe loads this DLL and drives it. Everything crosses as C types only
	// (no Beef objects leave the DLL), so the wrapper's runtime stays isolated from the game DLL's.
	static class Api
	{
		static ResidentBuildApp sApp;

		[CLink, Export]
		public static int32 GodotBeefBuild_Init(char8* workspaceDir, char8* config)
		{
			if (sApp != null)
				return 0;
			sApp = new ResidentBuildApp();
			var cmd = scope String();
			cmd.AppendF("-proddir={} -config={}", StringView(workspaceDir), StringView(config));
			sApp.ParseCommandLine(cmd);
			if (sApp.mFailed)
				return 1;
			sApp.Init();
			return sApp.mFailed ? 1 : 0;
		}

		// Tell the resident compiler a source file changed on disk (headless BuildApp has no file
		// watcher). Drops stale cached content, forces a fresh disk read, and marks the source dirty
		// so the next Compile() reparses just this file (+ its dependents).
		[CLink, Export]
		public static void GodotBeefBuild_MarkChanged(char8* filePath)
		{
			if (sApp == null)
				return;
			var path = scope String(StringView(filePath));
			let projectSource = sApp.FindProjectSourceItem(path);
			if (projectSource == null)
				return;
			// Clear stale saved content so FindProjectSourceContent doesn't short-circuit on it.
			if (projectSource.[Friend]mEditData != null)
				projectSource.[Friend]mEditData.SetSavedData(null, default);
			// Force a fresh disk read into editData.
			String content = scope .();
			IdSpan idData = default;
			sApp.FindProjectSourceContent(projectSource, out idData, true, content, null);
			idData.Dispose();
			projectSource.HasChangedSinceLastCompile = true;
		}

		// Run one build on the warm app by pumping Update() until the (overridden) Stop() fires.
		[CLink, Export]
		public static int32 GodotBeefBuild_Compile()
		{
			if (sApp == null)
				return -1;
			sApp.mHandledVerb = false;
			sApp.mFailed = false;
			sApp.mBuildDone = false;
			while (!sApp.mBuildDone)
				sApp.Update(false);
			return sApp.mFailed ? 1 : 0;
		}
	}
}
