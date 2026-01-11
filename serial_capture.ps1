$ErrorActionPreference = 'Continue'
$port = New-Object System.IO.Ports.SerialPort
$port.PortName = 'COM5'
$port.BaudRate = 115200
$port.ReadTimeout = 500

try {
    $port.Open()
    Write-Output "Monitoring COM5 for 30 seconds..."
    $startTime = Get-Date
    $output = @()

    while ((New-TimeSpan -Start $startTime -End (Get-Date)).TotalSeconds -lt 30) {
        try {
            $line = $port.ReadLine()
            if ($line) {
                Write-Output $line
                $output += $line
            }
        } catch [System.TimeoutException] {
            # Timeout is normal, continue
        }
    }

    Write-Output "`n--- Capture complete ---"
} catch {
    Write-Output "Error: $_"
} finally {
    if ($port.IsOpen) {
        $port.Close()
    }
}
