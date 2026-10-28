# Trusted requests are self-contained: ordinary descendants stay in the owned Job.
# Service, COM or scheduled-task activation and persistent external processes are
# outside this protocol. Stop on unproven cleanup instead of advancing the queue.
param([string]$Root='C:\TermForgeHarness')
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$Root=[IO.Path]::GetFullPath($Root)
if($Root.TrimEnd('\') -eq [IO.Path]::GetPathRoot($Root).TrimEnd('\')){throw 'A dedicated owned harness directory is required'}
Add-Type -Path (Join-Path $PSScriptRoot 'owned_vm_request.cs')
if(Test-Path "$Root\State\queue-quarantined.json"){throw 'Earlier cleanup is unverified; inspect the owned record before resuming'}
@{protocol='termforge-owned-vm-request-v2';ready_utc=[DateTime]::UtcNow.ToString('o');pid=$PID;session_id=(Get-Process -Id $PID).SessionId;user=[Security.Principal.WindowsIdentity]::GetCurrent().Name} | ConvertTo-Json | Set-Content "$Root\State\interactive-ready.json" -Encoding UTF8
while($true){
 foreach($Req in @(Get-ChildItem "$Root\Requests" -Filter '*.ps1' -File | Sort-Object Name)){
  $Id=$Req.BaseName
  if($Id -notmatch '^[A-Za-z0-9_-]{1,80}$'){continue}
  $Claim="$Root\State\$Id.running.ps1"
  try{Move-Item -LiteralPath $Req.FullName -Destination $Claim -ErrorAction Stop}catch{continue}
  $Record=@{id=$Id;protocol='termforge-owned-vm-request-v2';start_utc=[DateTime]::UtcNow.ToString('o');session_id=(Get-Process -Id $PID).SessionId;status='invalid_request';cleanup_verified=$true}
  $Invoked=$false
  try{
   $Metadata=Get-Content "$Root\Requests\$Id.request.json" -Raw | ConvertFrom-Json
   if($Metadata.protocol -ne 'termforge-owned-vm-request-v2' -or -not($Metadata.execution_timeout_seconds -is [int] -or $Metadata.execution_timeout_seconds -is [long]) -or $Metadata.execution_timeout_seconds -lt 1 -or $Metadata.execution_timeout_seconds -gt 300){throw 'Request requires an integer execution deadline of 1..300 seconds'}
   if((Get-FileHash -Algorithm SHA256 $Claim).Hash -ne $Metadata.script_sha256){throw 'Request source hash differs from its atomic metadata'}
   Copy-Item "$Root\Requests\$Id.request.json" "$Root\State\$Id.request.json"
   $Invoked=$true
   $Result=[TermForgeVmRequest.Supervisor]::Run("$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe",$Claim,"$Root\Results\$Id.stdout.txt","$Root\Results\$Id.stderr.txt",([int]$Metadata.execution_timeout_seconds*1000))
   $Record.status=$Result.Status;$Record.cleanup_verified=$Result.CleanupVerified;$Record.execution=$Result
   $Record.exit_code=$Result.RootExitCode;$Record.root_reaped=$Result.RootReaped
  }catch{$Record.error=$_.Exception.Message;if($Invoked){$Record.status='cleanup_failed';$Record.cleanup_verified=$false}}
  $Record.end_utc=[DateTime]::UtcNow.ToString('o')
  $Temp="$Root\Results\$Id.complete.upload"
  $Record | ConvertTo-Json -Depth 12 | Set-Content $Temp -Encoding UTF8
  Move-Item -LiteralPath $Temp -Destination "$Root\Results\$Id.complete.json"
  if(-not $Record.cleanup_verified){$Record | ConvertTo-Json -Depth 12 | Set-Content "$Root\State\queue-quarantined.json" -Encoding UTF8;throw 'Owned request cleanup is unverified; queue quarantined'}
 }
 Start-Sleep -Milliseconds 200
}
