Write-Host "Checking COM ports..." -ForegroundColor Green
$ports = [System.IO.Ports.SerialPort]::getportnames()
if ($ports) {
    Write-Host "Found ports:" -ForegroundColor Cyan
    $ports | ForEach-Object { Write-Host "  $_" }
} else {
    Write-Host "No COM ports found!" -ForegroundColor Red
}
