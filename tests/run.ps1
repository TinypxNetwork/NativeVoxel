param([string]$Compiler = "g++")
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$compilerPath = (Get-Command $Compiler -ErrorAction Stop).Source
$compilerDirectory = Split-Path -Parent $compilerPath
$runName = "parity-" + (Get-Date -Format "yyyyMMdd-HHmmss") + "-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
$runDirectory = Join-Path $projectRoot ("build\" + $runName)
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

# All fixtures live in this run's directory or in exclusive temporary directories.
# These tests need only the compiler, zlib and OpenSSL: no assets, MCP or Java.
$flags = @("-std=c++17", "-O2", "-ffp-contract=off", "-fno-fast-math", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN", "-D_WIN32_WINNT=0x0601", "-I", (Join-Path $projectRoot "include"))
$libraries = @("-lws2_32", "-lwinhttp", "-ldnsapi", "-lcrypto", "-lz", "-lstdc++fs", "-static-libgcc", "-static-libstdc++")
$coreNames = @("protocol", "world", "game", "crafting", "projectiles", "controller", "brewing", "drops", "drop_entities", "world_io", "local_session")
$sources = @($coreNames | ForEach-Object { Join-Path $projectRoot ("src\" + $_ + ".cpp") })
$sources += @(Get-ChildItem -LiteralPath (Join-Path $projectRoot "src") -Filter "simulation*.cpp" | Sort-Object Name | ForEach-Object { $_.FullName })
$sources += @(Get-ChildItem -LiteralPath (Join-Path $projectRoot "src") -Filter "worldgen*.cpp" | Sort-Object Name | ForEach-Object { $_.FullName })
$tests = @("game_tests", "simulation_tests", "world_io_tests", "local_session_tests", "sign_editor_tests")
$results = [System.Collections.Generic.List[object]]::new()
$objects = @()
$savedPath = $env:PATH
try {
    $env:PATH = $compilerDirectory + ";" + $savedPath
    foreach ($sourcePath in $sources) {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($sourcePath)
        $objectPath = Join-Path $runDirectory ($name + ".o")
        Write-Host ("Compiling " + $name)
        & $compilerPath @flags "-c" $sourcePath "-o" $objectPath
        if ($LASTEXITCODE -ne 0) { throw ("Compilation failed: " + $sourcePath) }
        $objects += $objectPath
    }
    Push-Location $runDirectory
    try {
        foreach ($name in $tests) {
            $testPath = Join-Path $PSScriptRoot ($name + ".cpp")
            $target = Join-Path $runDirectory ($name + ".exe")
            Write-Host ("Building " + $name)
            & $compilerPath @flags $testPath @objects @libraries "-o" $target
            if ($LASTEXITCODE -ne 0) { throw ("Test build failed: " + $name) }
            $timer = [System.Diagnostics.Stopwatch]::StartNew()
            & $target
            $code = $LASTEXITCODE
            $timer.Stop()
            $results.Add([pscustomobject]@{ name = $name; exitCode = $code; seconds = $timer.Elapsed.TotalSeconds })
            if ($code -ne 0) { throw ("Test failed: " + $name) }
        }
    } finally {
        Pop-Location
    }
} finally {
    $env:PATH = $savedPath
    ConvertTo-Json -InputObject @($results.ToArray()) -Depth 3 | Set-Content -LiteralPath (Join-Path $runDirectory "results.json") -Encoding UTF8
}
Write-Host ("All " + $results.Count + " regression groups passed. Results: " + $runDirectory)
