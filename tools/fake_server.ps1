param([int]$Port = 12801, [int]$Backlog = 300, [int]$PerSecond = 4, [int]$DropAfter = 0)

function AddVarint($buf, [uint64]$v) {
    while ($v -ge 0x80) { $buf.Add([byte](($v -band 0x7f) -bor 0x80)); $v = $v -shr 7 }
    $buf.Add([byte]$v)
}
function AddVarField($buf, [int]$field, [uint64]$v) { AddVarint $buf ([uint64]($field -shl 3)); AddVarint $buf $v }
function AddBytesField($buf, [int]$field, [byte[]]$data) {
    AddVarint $buf ([uint64](($field -shl 3) -bor 2)); AddVarint $buf ([uint64]$data.Length); $buf.AddRange($data)
}
function AddStrField($buf, [int]$field, [string]$s) { AddBytesField $buf $field ([System.Text.Encoding]::UTF8.GetBytes($s)) }

function EncodeLog([uint64]$ts, [int]$sev, [string]$file, [int]$line, [string]$sink, [string]$msg, [string]$details, [string]$bt) {
    $b = New-Object System.Collections.Generic.List[byte]
    AddVarField $b 1 $ts
    if ($sev -ne 0) { AddVarField $b 2 ([uint64]$sev) }
    AddStrField $b 3 $file; AddVarField $b 4 ([uint64]$line)
    AddStrField $b 5 $sink; AddStrField $b 6 $msg
    if ($details) { AddStrField $b 7 $details }
    if ($bt) { AddStrField $b 8 $bt }
    return , $b.ToArray()
}
function EncodeFrame($logs) {
    $body = New-Object System.Collections.Generic.List[byte]
    foreach ($l in $logs) { AddBytesField $body 1 $l }
    $len = $body.Count
    $out = New-Object System.Collections.Generic.List[byte]
    $out.Add([byte](($len -shr 24) -band 255)); $out.Add([byte](($len -shr 16) -band 255))
    $out.Add([byte](($len -shr 8) -band 255)); $out.Add([byte]($len -band 255))
    $out.AddRange($body)
    return , $out.ToArray()
}

$sinks = "Render", "Audio", "Physics", "Assets", "Events", "Lua", "UI"
$files = "vulkan_core.cpp", "audio_system.cpp", "simulator.cpp", "asset_manager.cpp", "event.hpp", "lua_state.cpp", "ui.cpp"
$bt = "  #0 renderer::compile (shader_compiler.cpp:210)`n  #1 renderer::Pipeline::build (pipeline.cpp:88)`n  #2 Engine::start (engine.cpp:55)"
$rng = [System.Random]::new(7)
function RandomLog([uint64]$ts) {
    $i = $rng.Next(0, $sinks.Count)
    $r = $rng.Next(0, 100)
    $sev = if ($r -lt 55) { 0 } elseif ($r -lt 85) { 1 } elseif ($r -lt 94) { 2 } elseif ($r -lt 99) { 3 } else { 4 }
    $details = ""; $trace = ""
    if ($sev -ge 3) { $details = "details line one`nline two with more context"; $trace = $bt }
    return , (EncodeLog $ts $sev $files[$i] ($rng.Next(10, 900)) $sinks[$i] "Message $($rng.Next()) from $($sinks[$i]) severity $sev" $details $trace)
}
function NowNs() { return [uint64]([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) * 1000000 }

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, $Port)
$listener.Start()
Write-Host "fake log server on :$Port"
while ($true) {
    $client = $listener.AcceptTcpClient()
    Write-Host "client connected"
    $stream = $client.GetStream()
    try {
        $logs = New-Object System.Collections.Generic.List[object]
        for ($i = 0; $i -lt $Backlog; $i++) { $logs.Add((RandomLog ((NowNs) - [uint64](($Backlog - $i) * 5000000)))) }
        $f = EncodeFrame $logs; $stream.Write($f, 0, $f.Length)
        $sent = 0
        while ($true) {
            Start-Sleep -Milliseconds ([int](1000 / $PerSecond))
            $f = EncodeFrame @(, (RandomLog (NowNs))); $stream.Write($f, 0, $f.Length); $sent++
            if ($DropAfter -gt 0 -and $sent -ge $DropAfter) { Write-Host "dropping"; break }
        }
    } catch { Write-Host "client gone: $($_.Exception.Message)" }
    $client.Close()
}
