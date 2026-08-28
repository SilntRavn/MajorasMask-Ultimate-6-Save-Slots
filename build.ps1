param(
    [switch]$English,
    [string]$DecompRoot = "",
    [string]$Clang = "",
    [string]$Ld = "",
    [string]$RecompModTool = "",
    [string]$Texconv = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$WorkspaceRoot = (Resolve-Path (Join-Path $ProjectRoot "..")).Path
$OldProjectRoot = Join-Path $WorkspaceRoot "Z64RE_CN_Mod"
if (-not $DecompRoot) { $DecompRoot = Join-Path $WorkspaceRoot ".reference-mm-decomp" }
if (-not $Clang) {
    $Clang = @(
        (Join-Path $OldProjectRoot "CNmod\toolchain\LLVM\bin\clang.exe"),
        "C:\Program Files\LLVM\bin\clang.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Ld) {
    $Ld = @(
        (Join-Path $OldProjectRoot "CNmod\toolchain\LLVM\bin\ld.lld.exe"),
        "C:\Program Files\LLVM\bin\ld.lld.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $RecompModTool) { $RecompModTool = Join-Path $WorkspaceRoot "Z64RE_CN_Mod\CNmod\tools\RecompModTool.exe" }
if (-not $Texconv) { $Texconv = Join-Path $WorkspaceRoot "Z64RE_CN_Mod\CNmod\tools\texconv.exe" }

foreach ($Tool in @($Clang, $Ld, $RecompModTool, $Texconv)) {
    if (-not $Tool -or -not (Test-Path $Tool)) { throw "Required build tool not found: $Tool" }
}
if (-not (Test-Path (Join-Path $DecompRoot "include\global.h"))) {
    throw "Majora's Mask decomp headers not found at $DecompRoot"
}

$BuildDir = Join-Path $ProjectRoot "build"
$LanguageDefine = if ($English) { "-DMM_MULTI_SAVE_ENGLISH=1" } else { "-DMM_MULTI_SAVE_ENGLISH=0" }
$Manifest = if ($English) { "mod-english.toml" } else { "mod.toml" }
$NrmName = if ($English) { "mm_multi_save_english.nrm" } else { "mm_multi_save.nrm" }
New-Item -ItemType Directory -Force $BuildDir | Out-Null
$Generator = Join-Path $ProjectRoot "tools\generate_ui_glyphs.py"
& python $Generator
if ($LASTEXITCODE -ne 0) { throw "UI glyph generation failed." }
$Rt64Dir = Join-Path $BuildDir "rt64"
Get-ChildItem -LiteralPath $Rt64Dir -Filter "*.dds" -File -ErrorAction SilentlyContinue | Remove-Item -Force
& $Texconv "-f" "BC7_UNORM" "-y" "-o" $Rt64Dir (Get-ChildItem -LiteralPath $Rt64Dir -Filter "*.png" -File | Select-Object -ExpandProperty FullName)
if ($LASTEXITCODE -ne 0) { throw "RT64 DDS conversion failed." }
$Object = Join-Path $BuildDir "multi_save.o"
$Elf = Join-Path $BuildDir "mod.elf"
$Map = Join-Path $BuildDir "mod.map"

$CompileArgs = @(
    "-target", "mips", "-mips2", "-mabi=32", "-O2", "-G0", "-mno-abicalls", "-mno-odd-spreg",
    "-mno-check-zero-division", "-fomit-frame-pointer", "-ffast-math", "-fno-unsafe-math-optimizations",
    "-fno-builtin-memset", "-Wall", "-Wextra", "-Wno-incompatible-library-redeclaration",
    "-Wno-unused-parameter", "-Wno-unknown-pragmas", "-Wno-unused-variable", "-Wno-missing-braces",
    "-Wno-unsupported-floating-point-opt", "-Werror=section", "-D_LANGUAGE_C", "-DMM_VERSION=4", "-DMIPS",
    "-DF3DEX_GBI_2", "-DF3DEX_GBI_PL", "-DGBI_DOWHILE", $LanguageDefine, "-nostdinc", "-ffunction-sections",
    "-I", (Join-Path $ProjectRoot "include"),
    "-I", (Join-Path $ProjectRoot "include\dummy_headers"),
    "-I", (Join-Path $DecompRoot "include"),
    "-I", (Join-Path $DecompRoot "src"),
    "-idirafter", (Join-Path $DecompRoot "include\libc"),
    "-c", (Join-Path $ProjectRoot "src\multi_save.c"), "-o", $Object
)
& $Clang $CompileArgs
if ($LASTEXITCODE -ne 0) { throw "MIPS compilation failed." }

$LinkArgs = @(
    $Object, "-nostdlib", "-T", (Join-Path $ProjectRoot "mod.ld"), "-Map", $Map,
    "--unresolved-symbols=ignore-all", "--emit-relocs", "-e", "0", "--no-nmagic", "-gc-sections", "-o", $Elf
)
& $Ld $LinkArgs
if ($LASTEXITCODE -ne 0) { throw "MIPS link failed." }

Push-Location $ProjectRoot
try {
    & $RecompModTool $Manifest "build"
    if ($LASTEXITCODE -ne 0) { throw "RecompModTool packaging failed." }
} finally {
    Pop-Location
}

Get-Item $Elf, (Join-Path $BuildDir $NrmName) |
    Select-Object FullName, Length, LastWriteTime
