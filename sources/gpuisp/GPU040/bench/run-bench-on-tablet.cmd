@echo off
rem Run the GPUISP2 bench on the Xiaomi Pad 5 (Adreno 640).
rem Copy this whole folder to the device, then put two files into .\data :
rem   data\frame-raw.bin      the sensor RAW plane, 16-bit per pixel, tightly packed (Android plane0.bin)
rem   data\frame.params.txt   the IQ values (bench\tools\result2params.py output, optionally with a
rem                           derived tone curve from bench\tools\derive_tone.py)
rem Run from an ARM64 command prompt on the tablet. Everything is written next to this script.
setlocal
pushd "%~dp0"

set "RAW=data\frame-raw.bin"
set "P=data\frame.params.txt"
set "W=3264"
set "H=2448"
set "CROP=0 306 3264 1836"
set "PATTERN=0"
rem PATTERN 0 = BGGR, which is what both front and rear DNGs declare (tag 33422 CFAPattern 2,1,1,0).
rem A params file that carries a "pattern" key already knows its mosaic - this switch overrides it,
rem so keep them consistent. Rear frame as currently calibrated by its owner: --width 4208 --height
rem 3120, crop 0 376 4208 2367, pattern 3 (that IQ was fitted at RGGB; see NOTES.md).

if not exist "%RAW%" echo ERROR: no RAW frame at %RAW% & goto :end
if not exist "%P%"   echo ERROR: no params file at %P%  & goto :end

echo ==== 1. correctness gate: GPU vs CPU reference, full pipeline ====
GpuIspBench.exe --raw "%RAW%" --width %W% --height %H% --params "%P%" --crop %CROP% --pattern %PATTERN% ^
  --yuv 0 --out-w 1920 --out-h 1080 --both --check-front --debug > gate.txt 2>&1
type gate.txt

echo.
echo ==== 2. stage timings at the driver target size 1280x960 (10 frames) ====
GpuIspBench.exe --raw "%RAW%" --width %W% --height %H% --params "%P%" --crop 0 0 %W% %H% --pattern %PATTERN% ^
  --yuv 0 --out-w 1280 --out-h 960 --repeat 10 > timing-1280x960.txt 2>&1
type timing-1280x960.txt

echo.
echo ==== 3. stage timings at 1920x1080, 16:9 crop (10 frames) ====
GpuIspBench.exe --raw "%RAW%" --width %W% --height %H% --params "%P%" --crop %CROP% --pattern %PATTERN% ^
  --yuv 0 --out-w 1920 --out-h 1080 --repeat 10 > timing-1920x1080.txt 2>&1
type timing-1920x1080.txt

echo.
echo ==== 4. YUV matrix sweep against the Android stream of the same frame ====
if exist "data\frame-i420.yuv" (
  for %%M in (0 1 2) do (
    echo -- yuv mode %%M
    GpuIspBench.exe --raw "%RAW%" --width %W% --height %H% --params "%P%" --crop %CROP% --pattern %PATTERN% ^
      --yuv %%M --out-w 1920 --out-h 1080 --ref-yuv "data\frame-i420.yuv" --ref-w 1920 --ref-h 1080 2>&1 | findstr /C:"MAE" /C:"luma mean" /C:"sharpness"
  )
) else (
  echo (data\frame-i420.yuv missing - skipping the Android comparison)
)

echo.
echo ==== 5. picture dumps for a visual check ====
GpuIspBench.exe --raw "%RAW%" --width %W% --height %H% --params "%P%" --crop %CROP% --pattern %PATTERN% ^
  --yuv 0 --out-w 1280 --out-h 720 --out-bmp out-1280x720.bmp --out-nv12 out-1280x720.nv12 >nul 2>&1
if exist out-1280x720.bmp echo   wrote out-1280x720.bmp

:end
popd
endlocal
