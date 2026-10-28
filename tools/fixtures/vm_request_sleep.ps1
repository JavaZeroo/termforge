# Ordinary descendants must inherit this request's Job and share its deadline.
$ErrorActionPreference='Stop'
$Stem=[IO.Path]::GetFileNameWithoutExtension($MyInvocation.MyCommand.Path)
$ChildMarker=Join-Path $PSScriptRoot ($Stem+'.child-started.json')
$ChildCode="@{pid=`$PID;fixture='owned-sleep-child'} | ConvertTo-Json | Set-Content '$ChildMarker'; Start-Sleep -Seconds 60"
$Encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($ChildCode))
$Child=Start-Process "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList @('-NoLogo','-NoProfile','-NonInteractive','-EncodedCommand',$Encoded) -WindowStyle Hidden -PassThru
@{fixture='owned-sleep-root';root_pid=$PID;child_pid=$Child.Id;sleep_seconds=60} | ConvertTo-Json
Start-Sleep -Seconds 60
throw 'The execution deadline should have stopped this request'
