$ErrorActionPreference='Stop'
@{fixture='request-after-timeout';ok=$true;session_id=(Get-Process -Id $PID).SessionId} | ConvertTo-Json
exit 0
