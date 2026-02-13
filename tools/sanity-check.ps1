param(
  [string]$Sketch = "FirmwarePro.ino"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

Write-Host "[1/3] Buscando literales rotos (\`r\`n) en .ino/.h..."
$bad = Select-String -Path "*.ino","*.h" -Pattern "`r`n" -SimpleMatch -ErrorAction SilentlyContinue
if ($bad) {
  $bad | ForEach-Object { Write-Host "  ERROR: $($_.Path):$($_.LineNumber)" -ForegroundColor Red }
  throw "Se detectaron literales \`r\`n incrustados en archivos fuente."
}

Write-Host "[2/3] Verificando balance básico de llaves..."
$src = Get-Content "*.ino","*.h" -Raw
$open = ($src.ToCharArray() | Where-Object { $_ -eq '{' }).Count
$close = ($src.ToCharArray() | Where-Object { $_ -eq '}' }).Count
if ($open -ne $close) {
  throw "Llaves desbalanceadas: '{'=$open, '}'=$close"
}

Write-Host "[3/3] Sanity básico OK."
Write-Host "Sugerencia: correr compilación en Arduino/CLI antes de merge." -ForegroundColor Yellow
