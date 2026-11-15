# tools/ci-runner/windows.ps1: the Windows test VM's GitHub Actions runner of OpenH1/goath1 (label goath1-windows),
# run as an administrator inside the VM by tools/ci-runner/install-windows.sh. Only while the VM runs: the
# workflow asks the VM's host first (host.sh status) and sends the windows job to GitHub's runner otherwise.
#   windows.ps1 tools     Git (its bash), Python, LLVM (the host's clang) and the Build Tools' MSVC and Windows SDK
#   windows.ps1 add       the runner registered (its registration token on stdin) as a service of its own
#                         unprivileged user, goath1-ci (not an administrator), its jobs in C:\goath1-ci\work
#   windows.ps1 remove    the runner unregistered (its removal token on stdin)
#   windows.ps1 status    idle, busy or off: the runner's service and whether a job runs
param([Parameter(Mandatory)] [ValidateSet('tools', 'add', 'remove', 'status')] [string] $Mode)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = 'SilentlyContinue'
$Root = 'C:\goath1-ci'
$Runner = "$Root\runner"
$User = 'goath1-ci'
$RunnerVersion = '2.338.2'
$RunnerSha = 'a1f17aa987fc8158bc7521d284912267a0d01b7c2c4684b33c86783b5e8221cc'

function Fetch($url, $file, $sha) {  # a download, checked by its SHA-256, or else by its publisher's signature
  $path = "$Root\downloads\$file"
  if (-not (Test-Path $path)) {
    New-Item -ItemType Directory -Force "$Root\downloads" | Out-Null
    # (Windows PowerShell 5.1: no -MaximumRetryCount)
    for ($try = 1; ; $try++) {
      try { Invoke-WebRequest -UseBasicParsing -OutFile "$path.part" $url; break }
      catch { if ($try -ge 5) { throw }; Start-Sleep 5 }
    }
    if ($sha) {
      if ((Get-FileHash "$path.part" -Algorithm SHA256).Hash -ne $sha) { throw "${file}: unexpected SHA-256" }
    } elseif ((Get-AuthenticodeSignature "$path.part").Status -ne 'Valid') { throw "${file}: no valid signature" }
    Move-Item "$path.part" $path
  }
  $path
}

function Install($name, $test, $path, $arguments) {
  if (Test-Path $test) { "$name is installed"; return }
  "installing $name"
  $p = Start-Process -FilePath $path -ArgumentList $arguments -Wait -PassThru
  if ($p.ExitCode -notin 0, 3010) { throw "$name's installer failed: $($p.ExitCode)" }
}

switch ($Mode) {
  'tools' {
    Install 'Git' 'C:\Program Files\Git\bin\bash.exe' `
      (Fetch 'https://github.com/git-for-windows/git/releases/download/v2.56.1.windows.1/Git-2.56.1-64-bit.exe' `
        'Git-2.56.1-64-bit.exe' 'C330C72E10B408CC39994BFEFDB63542B0D93D21699325C3E0BD874AF8A5A559') `
      '/VERYSILENT /NORESTART /NOCANCEL /SP- /COMPONENTS=gitlfs /o:PathOption=CmdTools'
    Install 'Python' 'C:\Program Files\Python312\python.exe' `
      (Fetch 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-amd64.exe' 'python-3.12.10-amd64.exe' $null) `
      '/quiet InstallAllUsers=1 PrependPath=1 Include_test=0 Include_launcher=0'
    Install 'LLVM' 'C:\Program Files\LLVM\bin\clang.exe' `
      (Fetch 'https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/LLVM-22.1.8-win64.exe' `
        'LLVM-22.1.8-win64.exe' '16E5709785FEF73C854646241C4A92C5CD574318D1B33C63330DD7721903E55C') '/S'
    Install 'the Build Tools' 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC' `
      (Fetch 'https://aka.ms/vs/17/release/vs_BuildTools.exe' 'vs_BuildTools.exe' $null) `
      '--quiet --wait --norestart --nocache --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100'
    # PowerShell 7, pwsh (the workflow's shell: pwsh steps)
    if (-not (Test-Path 'C:\Program Files\PowerShell\7\pwsh.exe')) {
      'installing PowerShell 7'
      $zip = Fetch 'https://github.com/PowerShell/PowerShell/releases/download/v7.7.0/PowerShell-7.7.0-win-x64.zip' `
        'PowerShell-7.7.0-win-x64.zip' '7C74E03B20A75B59715847439D046A6214B1863EE79B187872980F139BB4A05F'
      Expand-Archive $zip -DestinationPath 'C:\Program Files\PowerShell\7'
    }
    # (the system PATH: pwsh, python and python3, clang, and Git's bin\bash.exe, which puts Git's tools first on
    # its PATH as GitHub's runners' bash does: its usr\bin itself would lose to System32's, timeout.exe's say)
    $path = (([Environment]::GetEnvironmentVariable('Path', 'Machine') -split ';') |
      Where-Object { $_ -and $_ -ne 'C:\Program Files\Git\usr\bin' }) -join ';'
    foreach ($dir in 'C:\Program Files\PowerShell\7', 'C:\Program Files\LLVM\bin', 'C:\Program Files\Git\cmd', 'C:\Program Files\Git\bin') {
      if ($path -notlike "*$dir*") { $path = "$path;$dir" }
    }
    [Environment]::SetEnvironmentVariable('Path', $path, 'Machine')
    if (-not (Test-Path 'C:\Program Files\Python312\python3.exe')) {
      Copy-Item 'C:\Program Files\Python312\python.exe' 'C:\Program Files\Python312\python3.exe'
    }
  }
  'add' {
    $token = [Console]::In.ReadLine()
    # the runner's user: no administrator, a random password nobody keeps (the service's alone)
    if (-not (Get-LocalUser $User -ErrorAction SilentlyContinue)) {
      $bytes = New-Object byte[] 24; [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
      $password = [Convert]::ToBase64String($bytes) + 'a1!'
      New-LocalUser $User -Password (ConvertTo-SecureString $password -AsPlainText -Force) `
        -PasswordNeverExpires -AccountNeverExpires -Description 'GitHub Actions runner (tools/ci-runner)' | Out-Null
    } else {
      $bytes = New-Object byte[] 24; [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
      $password = [Convert]::ToBase64String($bytes) + 'a1!'
      Set-LocalUser $User -Password (ConvertTo-SecureString $password -AsPlainText -Force)
    }
    foreach ($dir in "$Root\work", "$Root\cache") {
      New-Item -ItemType Directory -Force $dir | Out-Null
      icacls $dir /grant "${User}:(OI)(CI)M" | Out-Null
    }
    if (-not (Test-Path "$Runner\config.cmd")) {
      $zip = Fetch "https://github.com/actions/runner/releases/download/v$RunnerVersion/actions-runner-win-x64-$RunnerVersion.zip" `
        "actions-runner-win-x64-$RunnerVersion.zip" $RunnerSha.ToUpper()
      Expand-Archive $zip -DestinationPath $Runner
    }
    # (the token and the password in the environment, never on a command line)
    $env:ACTIONS_RUNNER_INPUT_TOKEN = $token
    $env:ACTIONS_RUNNER_INPUT_WINDOWSLOGONPASSWORD = $password
    Push-Location $Runner
    & .\config.cmd --unattended --replace --url https://github.com/OpenH1/goath1 --name bosgame-windows `
      --labels goath1-windows --work "$Root\work" --runasservice --windowslogonaccount ".\$User"
    Pop-Location
    if ($LASTEXITCODE) { throw "config.cmd failed: $LASTEXITCODE" }
  }
  'remove' {
    $env:ACTIONS_RUNNER_INPUT_TOKEN = [Console]::In.ReadLine()
    Push-Location $Runner; & .\config.cmd remove; Pop-Location
  }
  'status' {
    $service = Get-Service 'actions.runner.*' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $service -or $service.Status -ne 'Running') { 'off' }
    elseif (Get-Process Runner.Worker -ErrorAction SilentlyContinue) { 'busy' }
    else { 'idle' }
  }
}
