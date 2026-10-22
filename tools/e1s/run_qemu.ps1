param(
    [string]$Qemu = "qemu-system-aarch64",
    [string]$Zig = $env:ZIG,
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = Resolve-Path (Join-Path $here "../..")
$image = Join-Path $root "shim/e1s/out-qemu/Image"

if ($Build -or -not (Test-Path -LiteralPath $image)) {
    $buildArgs = @{ Qemu = $true }
    if ($Zig) { $buildArgs.Zig = $Zig }
    & (Join-Path $root "shim/e1s/build.ps1") @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "QEMU probe build failed" }
}

$qemuPath = $null
$cmd = Get-Command $Qemu -ErrorAction SilentlyContinue
if ($cmd) {
    $qemuPath = $cmd.Source
} else {
    $candidates = @(
        "C:\Program Files\qemu\qemu-system-aarch64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-aarch64.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            $qemuPath = $candidate
            break
        }
    }
}
if (-not $qemuPath) {
    throw "qemu-system-aarch64 not found; install QEMU or pass -Qemu C:\path\qemu-system-aarch64.exe"
}

$ufsDisk = Join-Path $root "shim/e1s/out-qemu/ufs.img"
if (-not (Test-Path -LiteralPath $ufsDisk)) {
    $fs = [System.IO.File]::Create($ufsDisk)
    $fs.SetLength(1MB)
    $fs.Close()
}
$signature = [System.Text.Encoding]::ASCII.GetBytes("E1S-UFS!")
$fs = [System.IO.File]::Open($ufsDisk, "Open", "ReadWrite")
$fs.Position = 0
$fs.Write($signature, 0, $signature.Length)
$fs.Close()

& $qemuPath -M virt,gic-version=3 -cpu cortex-a72 -m 512 `
    -nographic -semihosting-config enable=on,target=native `
    -device ufs `
    -drive "if=none,id=ufsdisk,file=$ufsDisk,format=raw" `
    -device ufs-lu,drive=ufsdisk,lun=0 `
    -kernel $image
exit $LASTEXITCODE
