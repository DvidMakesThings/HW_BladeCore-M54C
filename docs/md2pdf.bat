@echo off
REM ============================================================
REM  md2pdf.bat - Convert Markdown to PDF with gridlined tables
REM  Usage: md2pdf.bat input.md [output.pdf]
REM  Requires: pandoc, xelatex (MiKTeX or TeX Live)
REM ============================================================

setlocal enabledelayedexpansion

if "%~1"=="" (
    echo Usage: md2pdf.bat input.md [output.pdf]
    exit /b 1
)

set "INPUT=%~1"
set "OUTPUT=%~2"
if "%OUTPUT%"=="" set "OUTPUT=%~dpn1.pdf"
set "SCRIPTDIR=%~dp0"
set "TEXHEADER=%SCRIPTDIR%pdf-header.tex"
set "TEMP_TEX=%~dpn1_temp.tex"
set "TEMP_GRID=%~dpn1_temp_grid.tex"
set "TEMP_PDF=%~dpn1_temp_grid.pdf"

REM Step 1: Generate intermediate LaTeX
echo [1/4] Generating LaTeX...
pandoc "%INPUT%" -o "%TEMP_TEX%" --pdf-engine=xelatex -V fontsize=9pt -V geometry="margin=1.5cm" -V documentclass=article -H "%TEXHEADER%" --columns=300 --standalone 2>nul

if not exist "%TEMP_TEX%" (
    echo ERROR: pandoc failed to generate LaTeX.
    exit /b 1
)

REM Step 2: Post-process tables (add grid lines)
echo [2/4] Adding table grid lines...
powershell -NoProfile -Command ^
  "$tex = Get-Content '%TEMP_TEX%' -Raw;" ^
  "$tex = [regex]::Replace($tex, '\\begin\{longtable\}\[\]\{@\{\}([a-lr]+)@\{\}\}', { param($m); $cols = $m.Groups[1].Value; $new = '|' + ($cols.ToCharArray() -join '|') + '|'; \"\begin{longtable}[]{$new}\" });" ^
  "$tex = $tex -replace '\\toprule\\noalign\{\}', '\hline';" ^
  "$tex = $tex -replace '\\midrule\\noalign\{\}', '\hline';" ^
  "$tex = $tex -replace '\\bottomrule\\noalign\{\}', '\hline';" ^
  "$lines = $tex -split \"`n\"; $result = @();" ^
  "for ($i = 0; $i -lt $lines.Count; $i++) { $result += $lines[$i]; $line = $lines[$i].Trim();" ^
  "  if ($line -match '\\\\$' -or $line -match '\\\\\s*$') {" ^
  "    if ($i + 1 -lt $lines.Count) { $next = $lines[$i + 1].Trim();" ^
  "      if ($next -ne '' -and $next -notmatch '^\\(end|toprule|midrule|bottomrule|endhead|endfirsthead|endfoot|endlastfoot|hline|caption|begin)') { $result += '\hline' } } } };" ^
  "$tex = $result -join \"`n\";" ^
  "Set-Content '%TEMP_GRID%' $tex -NoNewline"

if not exist "%TEMP_GRID%" (
    echo ERROR: Table post-processing failed.
    exit /b 1
)

REM Step 3: Compile with xelatex (two passes for cross-references)
echo [3/4] Compiling PDF (pass 1)...
pushd "%~dp1"
xelatex -interaction=nonstopmode "%TEMP_GRID%" >nul 2>nul
echo [3/4] Compiling PDF (pass 2)...
xelatex -interaction=nonstopmode "%TEMP_GRID%" >nul 2>nul
popd

if not exist "%TEMP_PDF%" (
    echo ERROR: xelatex compilation failed.
    exit /b 1
)

REM Step 4: Move to final output and clean up
echo [4/4] Cleaning up...
move /y "%TEMP_PDF%" "%OUTPUT%" >nul
del /q "%TEMP_TEX%" 2>nul
del /q "%TEMP_GRID%" 2>nul
del /q "%~dpn1_temp_grid.aux" 2>nul
del /q "%~dpn1_temp_grid.log" 2>nul
del /q "%~dpn1_temp_grid.out" 2>nul

echo Done: %OUTPUT%
