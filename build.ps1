param(
    [switch]$DebugBuild,
    [switch]$SkipAssets,
    [switch]$OfflineAssets,
    [switch]$Tests,
    [string]$OutputName = "NativeVoxel.exe",
    [string]$Compiler = "g++",
    [string]$McpRoot = "",
    [string]$AssetCache = (Join-Path $PSScriptRoot ".cache\minecraft")
)
$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
$buildRoot = Join-Path $projectRoot "build"
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
$binaryRoot = Join-Path $projectRoot "bin"
New-Item -ItemType Directory -Force -Path $binaryRoot | Out-Null
$compilerInfo = Get-Command $Compiler -ErrorAction Stop
$compilerPath = $compilerInfo.Source
$compilerDirectory = Split-Path -Parent $compilerPath

if (-not $SkipAssets) {
    $assetArguments = @("--cache", $AssetCache, "--output", (Join-Path $projectRoot "assets"))
    if ($McpRoot) { $assetArguments += @("--mcp", $McpRoot) }
    if ($OfflineAssets) { $assetArguments += "--offline" }
    & python (Join-Path $projectRoot "tools\setup_assets.py") @assetArguments
    if ($LASTEXITCODE -ne 0) { throw "Asset setup failed." }
}

$flags = @("-std=c++17", "-ffp-contract=off", "-fno-fast-math", "-Wall", "-Wextra", "-Wno-cast-function-type", "-Wno-missing-field-initializers", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN", "-D_WIN32_WINNT=0x0601", "-I", (Join-Path $projectRoot "include"))
if ($DebugBuild) { $flags += @("-O0", "-g") } else { $flags += @("-O3", "-DNDEBUG") }
$sources = @("main", "platform", "renderer", "assets", "render_assets", "entity_models", "audio", "world", "world_io", "protocol", "game", "controller", "crafting", "gui_inventory", "picking", "settings", "gui_options", "skins", "first_person", "item_entities", "tile_entities", "local_session", "simulation", "simulation_entities", "simulation_containers", "brewing", "drops", "drop_entities", "worldgen", "worldgen_noise", "worldgen_biomes", "worldgen_surface", "worldgen_carvers", "worldgen_dimensions", "worldgen_features", "worldgen_trees", "worldgen_decoration", "worldgen_loot", "worldgen_dungeons", "worldgen_nether_population")
$sources += @("entity_visuals", "particles", "resource_packs", "chat_text", "gui_hud", "server_list", "pickup", "projectiles")
$objects = @()
foreach ($source in $sources) {
    $inputPath = Join-Path $projectRoot ("src\" + $source + ".cpp")
    $objectPath = Join-Path $buildRoot ($source + ".o")
    Write-Host ("Compiling " + $source + ".cpp")
    & $compilerPath @flags "-c" $inputPath "-o" $objectPath
    if ($LASTEXITCODE -ne 0) { throw ("Compilation failed: " + $source) }
    $objects += $objectPath
}

$libraries = @("-lopengl32", "-lgdi32", "-luser32", "-lshell32", "-lwinmm", "-lws2_32", "-lwinhttp", "-ldnsapi", "-lcrypto", "-lz", "-lole32", "-lwindowscodecs", "-luuid", "-lstdc++fs", "-static-libgcc", "-static-libstdc++")
if ([System.IO.Path]::GetFileName($OutputName) -ne $OutputName -or -not $OutputName.EndsWith(".exe")) { throw "OutputName must be an EXE filename." }
$executable = Join-Path $binaryRoot $OutputName
& $compilerPath @objects @libraries "-o" $executable
if ($LASTEXITCODE -ne 0) { throw "Linking failed." }

# Stage only the actual DLL imports found in the compiler's own runtime folder.
$objectDump = Join-Path $compilerDirectory "objdump.exe"
$queue = [System.Collections.Generic.Queue[string]]::new()
$queue.Enqueue($executable)
$visited = @{}
while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    $imports = & $objectDump -p $binary | Select-String 'DLL Name:\s*(\S+)'
    foreach ($import in $imports) {
        $dllName = $import.Matches[0].Groups[1].Value
        if ($visited.ContainsKey($dllName)) { continue }
        $visited[$dllName] = $true
        $dllSource = Join-Path $compilerDirectory $dllName
        $dllDestination = Join-Path $binaryRoot $dllName
        if (Test-Path -LiteralPath $dllSource) {
            if (-not (Test-Path -LiteralPath $dllDestination) -or (Get-FileHash -LiteralPath $dllSource).Hash -ne (Get-FileHash -LiteralPath $dllDestination).Hash) {
                Copy-Item -LiteralPath $dllSource -Destination $dllDestination -Force
            }
            $queue.Enqueue($dllSource)
        }
    }
}
Write-Host ("Built " + $executable)
if ($Tests) {
    & (Join-Path $projectRoot "test.ps1") -Compiler $compilerPath
    if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
}
