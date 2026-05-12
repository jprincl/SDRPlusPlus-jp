param(
	[Parameter(Mandatory=$true, Position=0)]
	[string]$BuildDir,
	[Parameter(Mandatory=$true, Position=1)]
	[string]$RootDir,
	[Parameter(Position=2)]
	[string]$BuildConfig = "Release",
	[Parameter(Position=3)]
	[ValidateSet("auto", "x64", "arm64")]
	[string]$Arch = "auto"
)

function Get-CMakeCacheValue($cachePath, $name) {
	$match = Select-String -Path $cachePath -Pattern "^${name}:[^=]*=(.*)$" | Select-Object -First 1
	if ($match) {
		return $match.Matches[0].Groups[1].Value.Trim()
	}
	return $null
}

function Resolve-Arch($buildDir, $explicit) {
	if ($explicit -ne "auto") {
		return $explicit.ToLower()
	}
	$cachePath = Join-Path $buildDir "CMakeCache.txt"
	$platform = Get-CMakeCacheValue $cachePath "CMAKE_GENERATOR_PLATFORM"
	switch -Regex ($platform) {
		'^(?i)ARM64$' { return "arm64" }
		'^(?i)x64$'   { return "x64" }
		default {
			throw "Cannot infer architecture from CMakeCache CMAKE_GENERATOR_PLATFORM='$platform'. Pass -Arch x64|arm64 explicitly."
		}
	}
}

function Get-WindowsStageDir($buildDir, $buildConfig) {
	$cachePath = Join-Path $buildDir "CMakeCache.txt"
	$stageRoot = Get-CMakeCacheValue $cachePath "SDRPP_WINDOWS_BUNDLE_STAGE_ROOT"
	if (-not $stageRoot) {
		$stageRoot = Join-Path $buildDir "stage\windows-bundle"
	}

	$stageDir = Join-Path $stageRoot $buildConfig
	if (-not (Test-Path $stageDir)) {
		throw "Windows bundle staging directory not found: $stageDir"
	}
	return $stageDir
}


$resolved_arch = Resolve-Arch $BuildDir $Arch
$package_dir = "sdrpp_windows_$resolved_arch"
$zip_path = "$package_dir.zip"

& cmake --build $BuildDir --config $BuildConfig --target stage_windows_bundle
if ($LASTEXITCODE -ne 0) {
	throw "Failed to build stage_windows_bundle for config '$BuildConfig'."
}

$stage_dir = Get-WindowsStageDir $BuildDir $BuildConfig

Remove-Item -Force -Recurse $package_dir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $package_dir -Force | Out-Null
Copy-Item -Recurse "$stage_dir\*" "$package_dir/"

Compress-Archive -Path "$package_dir/" -DestinationPath $zip_path -Force

Remove-Item -Force -Recurse $package_dir
