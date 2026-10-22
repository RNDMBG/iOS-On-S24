param(
    [string]$Zig = $env:ZIG,
    [switch]$Pack,
    [switch]$Qemu
)

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = Resolve-Path (Join-Path $here "../..")
$outName = "out"
if ($Qemu) { $outName = "out-qemu" }
$out = Join-Path $here $outName
$target = "aarch64-freestanding"

if ($Qemu -and $Pack) {
    throw "-Qemu and -Pack cannot be used together"
}

if (-not $Zig) {
    $cmd = Get-Command zig -ErrorAction SilentlyContinue
    if ($cmd) { $Zig = $cmd.Source }
}
if (-not $Zig -or -not (Test-Path -LiteralPath $Zig)) {
    throw "zig not found; pass -Zig C:\path\to\zig.exe"
}
$Zig = (Resolve-Path -LiteralPath $Zig).Path
New-Item -ItemType Directory -Force -Path $out | Out-Null

$cflags = @(
    "--target=$target", "-c", "-O2", "-g0", "-ffreestanding",
    "-fno-builtin", "-fno-stack-protector", "-fno-pic",
    "-fno-zero-initialized-in-bss", "-mstrict-align",
    "-mgeneral-regs-only", "-Wall", "-Wextra", "-Werror"
)
if ($Qemu) { $cflags += "-DPROBE_QEMU=1" }

& $Zig cc @cflags (Join-Path $here "probe.c") -o (Join-Path $out "probe.o")
if ($LASTEXITCODE -ne 0) { throw "probe.c compile failed" }

& $Zig cc "--target=$target" -c -g0 -ffreestanding -fno-pic `
    (Join-Path $here "start.S") -o (Join-Path $out "start.o")
if ($LASTEXITCODE -ne 0) { throw "start.S compile failed" }

$ld = Join-Path $here "loader.ld"
& $Zig cc "--target=$target" -g0 -nostdlib "-Wl,-T,$ld" `
    "-Wl,-e,_head" "-Wl,--build-id=none" `
    (Join-Path $out "start.o") (Join-Path $out "probe.o") `
    -o (Join-Path $out "probe.elf")
if ($LASTEXITCODE -ne 0) { throw "link failed" }

& $Zig objcopy -O binary (Join-Path $out "probe.elf") (Join-Path $out "Image")
if ($LASTEXITCODE -ne 0) { throw "objcopy failed" }

python (Join-Path $root "tools/e1s/fix_image.py") (Join-Path $out "Image")
if ($LASTEXITCODE -ne 0) { throw "image padding failed" }

Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $out "Image") |
    Select-Object Algorithm, Hash, Path

if ($Pack) {
    python (Join-Path $root "tools/e1s/mkboot.py") `
        --kernel (Join-Path $out "Image") `
        --output (Join-Path $root "out/boot.img") --tar
    if ($LASTEXITCODE -ne 0) { throw "boot image packing failed" }
}
