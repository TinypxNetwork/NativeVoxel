param([string]$Compiler = "g++", [switch]$Reference, [switch]$Graphics, [switch]$AllAudio)
$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot
$testDirectory = Join-Path $projectRoot "build\tests"
New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
$compilerPath = (Get-Command $Compiler -ErrorAction Stop).Source
$flags = @("-std=c++17", "-O2", "-ffp-contract=off", "-fno-fast-math", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN", "-D_WIN32_WINNT=0x0601", "-I", (Join-Path $projectRoot "include"), "-static-libgcc", "-static-libstdc++")
$networkLibraries = @("-lws2_32", "-lwinhttp", "-ldnsapi", "-lcrypto", "-lz")
$results = [System.Collections.Generic.List[object]]::new()
$testObjects = @{}

function Invoke-CheckedTest {
    param([string]$Name, [string]$Command, [string[]]$Arguments)
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    & $Command @Arguments
    $code = $LASTEXITCODE
    $timer.Stop()
    $results.Add([pscustomobject]@{name=$Name; exitCode=$code; seconds=$timer.Elapsed.TotalSeconds})
    if ($code -ne 0) { throw ("Test failed: " + $Name) }
}

function Build-NativeTest {
    param([string]$Name, [string[]]$Sources, [string[]]$Libraries = @(), [string[]]$Definitions = @())
    $target = Join-Path $testDirectory ($Name + ".exe")
    $objects = @()
    foreach ($source in $Sources) {
        $sourcePath = Join-Path $projectRoot $source
        $objectKey = $sourcePath + "|" + ($Definitions -join "|")
        if (-not $testObjects.ContainsKey($objectKey)) {
            $objectPath = Join-Path $testDirectory ("unit-" + $testObjects.Count + ".o")
            Write-Host ("Compiling test dependency " + $source)
            & $compilerPath @flags @Definitions "-c" $sourcePath "-o" $objectPath
            if ($LASTEXITCODE -ne 0) { throw ("Test compilation failed: " + $source) }
            $testObjects[$objectKey] = $objectPath
        }
        $objects += $testObjects[$objectKey]
    }
    & $compilerPath @objects @Libraries "-static-libgcc" "-static-libstdc++" "-o" $target
    if ($LASTEXITCODE -ne 0) { throw ("Test compilation failed: " + $Name) }
    return $target
}

Push-Location $projectRoot
try {
    Invoke-CheckedTest "asset downloader (offline fixtures)" "python" @("tests/asset_download_tests.py")
    Get-ChildItem -LiteralPath (Join-Path $projectRoot "bin") -Filter "*.dll" | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $testDirectory $_.Name) -Force }
    $protocolTest = Build-NativeTest "protocol_tests" @("tests/protocol_tests.cpp","src/protocol.cpp") $networkLibraries @("-DMC_PROTOCOL_TESTING")
    Invoke-CheckedTest "protocol" $protocolTest @()
    Invoke-CheckedTest "protocol loopback" "python" @("tests/protocol_integration.py","--executable",$protocolTest)
    $worldTest = Build-NativeTest "world_tests" @("tests/world_tests.cpp","src/world.cpp")
    Invoke-CheckedTest "world physics/chunks/rays" $worldTest @()
    $registryTest = Build-NativeTest "registry_tests" @("tests/registry_tests.cpp")
    Invoke-CheckedTest "source registries" $registryTest @()
    $dimensionsTest = Build-NativeTest "entity_dimensions_tests" @("tests/entity_dimensions_tests.cpp")
    Invoke-CheckedTest "source entity dimensions" $dimensionsTest @()
    $fidelityTest = Build-NativeTest "render_fidelity_tests" @("tests/render_fidelity_tests.cpp")
    Invoke-CheckedTest "camera limits dynamic FOV and vanilla lightmap" $fidelityTest @()
    $ioTest = Build-NativeTest "world_io_tests" @("tests/world_io_tests.cpp","src/world_io.cpp","src/world.cpp","src/protocol.cpp") ($networkLibraries + @("-lstdc++fs"))
    Invoke-CheckedTest "Anvil save IO" $ioTest @()
    $gameSources = @("src/protocol.cpp","src/world.cpp","src/game.cpp","src/crafting.cpp","src/projectiles.cpp")
    $visualSources = @("src/entity_visuals.cpp","src/entity_models.cpp","src/first_person.cpp","src/item_entities.cpp","src/tile_entities.cpp","src/skins.cpp")
    $visualLibraries = $networkLibraries + @("-lwinhttp","-lcrypto","-lole32","-lwindowscodecs","-luuid","-lstdc++fs")
    $hudTest = Build-NativeTest "gui_hud_tests" (@("tests/gui_hud_tests.cpp","src/chat_text.cpp","src/gui_hud.cpp","src/server_list.cpp","src/render_assets.cpp") + $gameSources) ($networkLibraries + @("-lstdc++fs"))
    Invoke-CheckedTest "rich chat HUD and server browser" $hudTest @((Join-Path $projectRoot "assets"),(Join-Path $testDirectory "gui-hud"))
    $particlesTest = Build-NativeTest "particles_tests" @("tests/particles_tests.cpp","src/particles.cpp","src/world.cpp")
    Invoke-CheckedTest "particle display rules" $particlesTest @()
    $actualStateTest = Build-NativeTest "assets_actual_state_tests" @("tests/assets_actual_state_tests.cpp","src/assets.cpp") @("-lstdc++fs")
    Invoke-CheckedTest "double flowers and slab geometry" $actualStateTest @("assets")
    $simulationSources = @("src/simulation.cpp","src/simulation_entities.cpp","src/simulation_containers.cpp","src/brewing.cpp","src/drops.cpp","src/drop_entities.cpp","src/worldgen_noise.cpp")
    $lightingTest = Build-NativeTest "lighting_tests" @("tests/lighting_tests.cpp","src/world.cpp")
    Invoke-CheckedTest "dynamic block and sky lighting" $lightingTest @()
    $blockRenderTest = Build-NativeTest "block_rendering_tests" @("tests/block_rendering_tests.cpp","src/world.cpp")
    Invoke-CheckedTest "wall liquid geometry and smooth lighting" $blockRenderTest @()
    $localFireTest = Build-NativeTest "local_fire_visuals_tests" @("tests/local_fire_visuals_tests.cpp","src/world.cpp","src/worldgen_noise.cpp")
    Invoke-CheckedTest "local fire visual lifetime and extinguishing" $localFireTest @()
    $pickupTest = Build-NativeTest "pickup_tests" (@("tests/pickup_tests.cpp","src/pickup.cpp","src/controller.cpp") + $gameSources + $simulationSources) ($networkLibraries + @("-lstdc++fs"))
    Invoke-CheckedTest "item and XP pickup animation" $pickupTest @()
    $projectileTest = Build-NativeTest "projectile_tests" (@("tests/projectile_tests.cpp","src/controller.cpp") + $gameSources + $simulationSources) ($networkLibraries + @("-lstdc++fs"))
    Invoke-CheckedTest "projectile use movement collision and names" $projectileTest @()
    $worldgenSources = @("src/worldgen.cpp","src/worldgen_biomes.cpp","src/worldgen_surface.cpp","src/worldgen_carvers.cpp","src/worldgen_dimensions.cpp","src/worldgen_features.cpp","src/worldgen_trees.cpp","src/worldgen_decoration.cpp","src/worldgen_loot.cpp","src/worldgen_dungeons.cpp","src/worldgen_nether_population.cpp")
    $gameTest = Build-NativeTest "game_tests" (@("tests/game_tests.cpp") + $gameSources) $networkLibraries
    Invoke-CheckedTest "play packets" $gameTest @()
    $pickingTest = Build-NativeTest "picking_tests" (@("tests/picking_tests.cpp","src/picking.cpp") + $gameSources) $networkLibraries
    Invoke-CheckedTest "entity interaction targeting" $pickingTest @()
    $controllerTest = Build-NativeTest "controller_tests" (@("tests/controller_tests.cpp","src/controller.cpp") + $gameSources) $networkLibraries
    Invoke-CheckedTest "player controller" $controllerTest @()
    if (Test-Path -LiteralPath "tests/crafting_tests.cpp") {
        $craftingTest = Build-NativeTest "crafting_tests" (@("tests/crafting_tests.cpp","src/controller.cpp") + $gameSources) $networkLibraries
        Invoke-CheckedTest "crafting and containers" $craftingTest @()
    }
    if (Test-Path -LiteralPath "tests/gui_inventory_tests.cpp") {
        $guiTest = Build-NativeTest "gui_inventory_tests" (@("tests/gui_inventory_tests.cpp","src/gui_inventory.cpp","src/controller.cpp") + $gameSources) $networkLibraries
        Invoke-CheckedTest "inventory GUI packets and slot layout" $guiTest @("assets")
    }
    $audioTest = Build-NativeTest "audio_tests" @("tests/audio_tests.cpp","src/audio.cpp")
    $audioArguments = @("assets")
    if ($AllAudio) { $audioArguments += "--decode-all" }
    Invoke-CheckedTest "silent audio" $audioTest $audioArguments
    Invoke-CheckedTest "resource/model integrity" "python" @("tests/assets_tests.py")
    Invoke-CheckedTest "UI/font/item animation" "python" @("tests/render_assets_tests.py")
    $optionsTest = Build-NativeTest "gui_options_tests" @("tests/gui_options_tests.cpp","src/settings.cpp","src/gui_options.cpp") @("-luser32","-lstdc++fs")
    Invoke-CheckedTest "controls and sound options" $optionsTest @($projectRoot)
    $inputTest = Build-NativeTest "gui_input_tests" (@("tests/gui_input_tests.cpp") + $gameSources) ($networkLibraries + @("-lstdc++fs"))
    Invoke-CheckedTest "chat input completion signs and item tooltips" $inputTest @((Join-Path $projectRoot "assets"))
    $skinLibraries = @("-lwinhttp","-lcrypto","-lole32","-lwindowscodecs","-luuid","-lstdc++fs")
    $skinsTest = Build-NativeTest "skins_tests" @("tests/skins_tests.cpp","src/skins.cpp") $skinLibraries
    Invoke-CheckedTest "skin decoding/cache" $skinsTest @($projectRoot)
    $firstPersonTest = Build-NativeTest "first_person_tests" @("tests/first_person_tests.cpp") @("-lstdc++fs")
    Invoke-CheckedTest "first person meshes and transforms" $firstPersonTest @($projectRoot)
    $itemVisualTest = Build-NativeTest "item_entities_tests" (@("tests/item_entities_tests.cpp","src/world.cpp","src/protocol.cpp") + $visualSources) $visualLibraries
    Invoke-CheckedTest "item and XP rendering data" $itemVisualTest @($projectRoot)
    $entityVisualTest = Build-NativeTest "entity_visuals_tests" (@("tests/entity_visuals_tests.cpp","src/world.cpp","src/protocol.cpp") + $visualSources) $visualLibraries
    Invoke-CheckedTest "entity equipment animation and NBT" $entityVisualTest @($projectRoot)
    $effectsTest = Build-NativeTest "entity_effects_tests" (@("tests/entity_effects_tests.cpp","src/particles.cpp","src/controller.cpp") + $gameSources + $visualSources) $visualLibraries
    Invoke-CheckedTest "hurt death combat particles and action sounds" $effectsTest @()
    if ($Reference) { Invoke-CheckedTest "generate local banner pixel reference" "python" @("tools/export_banner_reference.py") }
    $tileTest = Build-NativeTest "tile_entities_tests" @("tests/tile_entities_tests.cpp","src/tile_entities.cpp","src/skins.cpp","src/world.cpp","src/protocol.cpp") ($skinLibraries + $networkLibraries)
    Invoke-CheckedTest "chest skull and banner rendering data" $tileTest @($projectRoot)
    $simulationTest = Build-NativeTest "simulation_tests" (@("tests/simulation_tests.cpp","src/controller.cpp") + $simulationSources + $gameSources) ($networkLibraries + @("-lstdc++fs"))
    Invoke-CheckedTest "native singleplayer simulation" $simulationTest @()
    $dropsTest = Build-NativeTest "drops_tests" (@("tests/drops_tests.cpp","src/drops.cpp","src/worldgen_noise.cpp","src/controller.cpp") + $gameSources) $networkLibraries
    Invoke-CheckedTest "source block drop rules" $dropsTest @()
    $dropEntityTest = Build-NativeTest "drop_entity_tests" @("tests/drop_entity_tests.cpp","src/drop_entities.cpp","src/world.cpp","src/worldgen_noise.cpp","src/protocol.cpp") $networkLibraries
    Invoke-CheckedTest "item and XP physics" $dropEntityTest @()
    $localTest = Build-NativeTest "local_session_tests" (@("tests/local_session_tests.cpp","src/local_session.cpp","src/world_io.cpp","src/controller.cpp") + $simulationSources + $worldgenSources + $gameSources) ($networkLibraries + @("-lstdc++fs"))
    Invoke-CheckedTest "local world creation containers and persistence" $localTest @()
    if ($Reference) {
        Invoke-CheckedTest "Java local fire visual lifetime oracle" "python" @("tools/verify_local_fire_visuals.py","--native",$localFireTest)
        Invoke-CheckedTest "Java pickup animation oracle" "python" @("tools/test_pickup_reference.py","--native",$pickupTest)
        Invoke-CheckedTest "Java projectile flight and potion oracle" "python" @("tools/verify_projectiles.py","--native",$projectileTest)
        Invoke-CheckedTest "Java fishing line oracle" "python" @("tools/verify_entity_effects.py","--native",$effectsTest)
        Invoke-CheckedTest "Java formatted text wrapping" "python" @("tools/verify_gui_text.py","--native",$hudTest)
        Invoke-CheckedTest "Java physics oracle" "python" @("tools/verify_physics.py")
        Invoke-CheckedTest "Java controller oracle" "python" @("tools/verify_controller.py","--native",$controllerTest)
        Invoke-CheckedTest "Java block targeting oracle" "python" @("tools/verify_selection.py")
        Invoke-CheckedTest "Java Anvil reader" "python" @("tools/verify_world_io.py","--native",$ioTest)
        Invoke-CheckedTest "Java simulation oracle" "python" @("tools/verify_simulation.py","--native",$simulationTest)
        Invoke-CheckedTest "Java drop rules oracle" "python" @("tools/verify_drops.py","--native",$dropsTest)
        Invoke-CheckedTest "Java dropped entity oracle" "python" @("tools/verify_drop_entities.py","--native",$dropEntityTest)
        if (Test-Path -LiteralPath "build/server-play.nvpc") {
            Invoke-CheckedTest "captured server chunk versus Java" "python" @("tools/verify_chunk_capture.py")
        }
    }
    if ($Graphics) {
        if ($Reference) { Invoke-CheckedTest "Java panorama draw trace" "python" @("tools/test_panorama_reference.py") }
        $renderSources = @("tests/render_smoke.cpp","src/platform.cpp","src/renderer.cpp","src/assets.cpp","src/render_assets.cpp","src/world.cpp","src/protocol.cpp","src/resource_packs.cpp") + $visualSources
        $renderTest = Build-NativeTest "render_smoke" $renderSources (@("-lopengl32","-lgdi32","-luser32") + $skinLibraries + $networkLibraries)
        Invoke-CheckedTest "OpenGL hardware smoke" $renderTest @("build/render-smoke.ppm")
        $effectsRenderSources = @("tests/entity_effects_render_smoke.cpp") + ($renderSources | Where-Object { $_ -ne "tests/render_smoke.cpp" })
        $effectsRenderTest = Build-NativeTest "entity_effects_render_smoke" $effectsRenderSources (@("-lopengl32","-lgdi32","-luser32") + $skinLibraries + $networkLibraries)
        Invoke-CheckedTest "OpenGL environment and entity effects" $effectsRenderTest @()
        $roamingSources = @("tests/roaming_memory_smoke.cpp") + ($renderSources | Where-Object { $_ -ne "tests/render_smoke.cpp" })
        $roamingTest = Build-NativeTest "roaming_memory_smoke" $roamingSources (@("-lopengl32","-lgdi32","-luser32","-lpsapi") + $skinLibraries + $networkLibraries)
        Invoke-CheckedTest "OpenGL roaming mesh and private memory" $roamingTest @()
    }
    Write-Host ("All " + $results.Count + " test groups passed.")
} finally {
    $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $testDirectory "results.json") -Encoding UTF8
    Pop-Location
}
