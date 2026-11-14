@echo off

set CGO_LDFLAGS=-Wl,-z,max-page-size=16384

gomobile bind -v -target=android/arm64 -androidapi 21 "github.com/owenewans/libowenclavecore"
if errorlevel 1 (
    exit /b 1
)

set "proj=..\..\app\libs"

if exist "%proj%" (
    copy /Y libexclavecore.aar "%proj%"
)
