# Build the experimental e1s XNU loader with the Zig toolchain.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Resolve-Path (Join-Path $here '..\..')
$zig = Join-Path $env:TEMP 'opencode\ziglang\ziglang\zig.exe'
if (-not (Test-Path -LiteralPath $zig)) {
    throw "zig not found at $zig"
}
$out = Join-Path $here 'out'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$ld = Join-Path $here 'loader.ld'
$cflags = @(
    '-target', 'aarch64-freestanding',
    '-O2', '-ffreestanding', '-nostdlib', '-fno-pic',
    '-mgeneral-regs-only', '-mstrict-align',
    '-fno-stack-protector', '-Wall', '-Wextra'
)
$linkflags = @(
    "-Wl,-T,$ld",
    '-Wl,--build-id=none',
    '-static'
)
& $zig cc @cflags @linkflags (Join-Path $here 'start.S') (Join-Path $here 'loader.c') (Join-Path $here 'adt_patch.c') (Join-Path $here 'gic.c') (Join-Path $here 'fdt.c') -o (Join-Path $out 'loader.elf')
if ($LASTEXITCODE -ne 0) { throw 'zig cc failed' }
& $zig objcopy -O binary (Join-Path $out 'loader.elf') (Join-Path $out 'Image')
if ($LASTEXITCODE -ne 0) { throw 'objcopy failed' }
& python (Join-Path $root 'tools\e1s\fix_image.py') (Join-Path $out 'Image')
if ($LASTEXITCODE -ne 0) { throw 'fix_image failed' }
Get-Item (Join-Path $out 'loader.elf'), (Join-Path $out 'Image') | Select-Object FullName, Length
