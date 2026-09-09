// Copyright (c) 2026 GregOrigin. All Rights Reserved.
using System;
using System.IO;
using UnrealBuildTool;

/**
 * Wraps the GDAL install Tools/Setup-GDAL.ps1 builds via vcpkg (gdal:x64-windows), so
 * GRTGeoTIFFReader can link against the real library instead of a hand-rolled parser.
 *
 * Not vendored in the repo -- GDAL and its own dependency tree (PROJ, GEOS, sqlite3, libtiff,
 * openssl, and others) run to several GB once built, and building from source locally sidesteps
 * trusting a prebuilt binary blob from a third-party mirror. See the project README.
 */
public class GDAL : ModuleRules
{
	public GDAL(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform != UnrealTargetPlatform.Win64)
		{
			// Tools/Setup-GDAL.ps1 only builds the x64-windows vcpkg triplet. A different platform
			// needs its own triplet built and wired in here before this module means anything there.
			return;
		}

		string VcpkgInstalledDir = Path.GetFullPath(Path.Combine(PluginDirectory, "..", "..", "Tools", "vcpkg", "installed", "x64-windows"));
		string IncludeDir = Path.Combine(VcpkgInstalledDir, "include");
		string LibDir = Path.Combine(VcpkgInstalledDir, "lib");
		string BinDir = Path.Combine(VcpkgInstalledDir, "bin");
		string GdalLib = Path.Combine(LibDir, "gdal.lib");
		string GdalDll = Path.Combine(BinDir, "gdal.dll");

		if (!File.Exists(GdalLib) || !File.Exists(GdalDll))
		{
			throw new BuildException(
				"GDAL is not installed at " + VcpkgInstalledDir + " -- run Tools/Setup-GDAL.ps1 " +
				"from the project root first (see the README's \"Third-party setup: GDAL\" section).");
		}

		PublicSystemIncludePaths.Add(IncludeDir);
		PublicAdditionalLibraries.Add(GdalLib);

		// Every DLL vcpkg built alongside gdal.dll -- GDAL's own dependency tree (PROJ, GEOS,
		// sqlite3, openssl, libtiff, and so on) -- has to sit next to UnrealEditor-GRT.dll, or
		// GDAL fails to load at runtime with an obscure "module not found" rather than a clear link
		// error. gdal.dll pulls the rest in transitively via the normal Windows DLL search path once
		// they're all in the same directory, so nothing else needs registering as its own dependency.
		//
		// RuntimeDependencies alone does not cover this: it is a manifest UnrealBuildTool hands to
		// the packaging/staging pipeline (RunUAT), not something a plain editor Build.bat compile
		// acts on -- an editor run needs the DLLs already sitting next to the module's own binary
		// before the process starts. So this copies them directly, here, as this Build.cs is
		// evaluated -- which happens on every build, so it self-heals if Binaries/ is ever cleaned.
		// RuntimeDependencies is still declared too, since packaging genuinely does read it.
		string PluginBinariesDir = Path.Combine(PluginDirectory, "Binaries", "Win64");
		Directory.CreateDirectory(PluginBinariesDir);

		foreach (string DllPath in Directory.GetFiles(BinDir, "*.dll"))
		{
			string DllName = Path.GetFileName(DllPath);
			string DestPath = Path.Combine(PluginBinariesDir, DllName);

			RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", DllName), DllPath);

			try
			{
				if (!File.Exists(DestPath) || File.GetLastWriteTimeUtc(DllPath) > File.GetLastWriteTimeUtc(DestPath))
				{
					File.Copy(DllPath, DestPath, true);
				}
			}
			catch (IOException)
			{
				// Already loaded by a running editor -- Windows locks it. Whatever copy is already
				// there is from the same vcpkg install, so leaving it in place is harmless.
			}
		}

		// PROJ and GDAL both ship their own runtime resource files (PROJ's coordinate-transform
		// database, GDAL's own CSV/JSON reference data) that are not compiled in -- without these
		// findable, PROJ logs "Cannot find proj.db" and silently falls back to reduced accuracy.
		// GRTGeoTIFFReader points PROJ_DATA/GDAL_DATA at this same Resources copy at runtime.
		string ResourcesDir = Path.Combine(PluginBinariesDir, "GDALResources");
		CopyDataDirectory(Path.Combine(VcpkgInstalledDir, "share", "proj"), Path.Combine(ResourcesDir, "proj"));
		CopyDataDirectory(Path.Combine(VcpkgInstalledDir, "share", "gdal"), Path.Combine(ResourcesDir, "gdal"));
	}

	private static void CopyDataDirectory(string SourceDir, string DestDir)
	{
		if (!Directory.Exists(SourceDir))
		{
			return;
		}

		Directory.CreateDirectory(DestDir);
		foreach (string SourceFile in Directory.GetFiles(SourceDir, "*", SearchOption.AllDirectories))
		{
			string RelativePath = Path.GetRelativePath(SourceDir, SourceFile);
			string DestFile = Path.Combine(DestDir, RelativePath);
			Directory.CreateDirectory(Path.GetDirectoryName(DestFile));

			try
			{
				if (!File.Exists(DestFile) || File.GetLastWriteTimeUtc(SourceFile) > File.GetLastWriteTimeUtc(DestFile))
				{
					File.Copy(SourceFile, DestFile, true);
				}
			}
			catch (IOException)
			{
				// In use / already up to date from a prior build -- harmless to leave as-is.
			}
		}
	}
}
