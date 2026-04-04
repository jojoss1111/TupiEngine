@echo off
:: compile_shaders.bat
::
:: Compila os shaders GLSL padrao do backend Vulkan para SPIR-V e gera
:: o header C++ "shaders_embedded.hpp" com os arrays embutidos no codigo.
::
:: Equivalente ao compile_shaders.sh, mas para Windows nativo (sem bash).
::
:: Dependencia: glslc  (vem com o Vulkan SDK do LunarG)
::   Instale em: https://vulkan.lunarg.com
::   O glslc.exe fica em:  %VULKAN_SDK%\Bin\glslc.exe
::   Apos instalar, reabra o terminal para a variavel VULKAN_SDK ser detectada.
::
:: Uso:
::   compile_shaders.bat
::
:: O arquivo gerado (shaders_embedded.hpp) deve ser commitado no repositorio
:: para que outros contribuidores nao precisem rodar este script.

setlocal enabledelayedexpansion

:: ---------------------------------------------------------------------------
:: Diretorio do script e caminho de saida
:: ---------------------------------------------------------------------------
set "SCRIPT_DIR=%~dp0"
:: Remove a barra final do caminho
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

set "OUT_HEADER=%SCRIPT_DIR%\src\Renderizador\shaders_embedded.hpp"
set "TMP_DIR=%TEMP%\compile_shaders_%RANDOM%"

mkdir "%TMP_DIR%" 2>nul

:: ---------------------------------------------------------------------------
:: Localiza o glslc
:: ---------------------------------------------------------------------------
set "GLSLC="

:: 1. Tenta o PATH do sistema
where glslc >nul 2>&1
if %errorlevel%==0 (
    set "GLSLC=glslc"
    goto :found_glslc
)

:: 2. Tenta a variavel de ambiente VULKAN_SDK (definida pelo instalador LunarG)
if defined VULKAN_SDK (
    if exist "%VULKAN_SDK%\Bin\glslc.exe" (
        set "GLSLC=%VULKAN_SDK%\Bin\glslc.exe"
        goto :found_glslc
    )
)

:: 3. Tenta o diretorio padrao do LunarG SDK em C:\VulkanSDK
:: Pega a versao mais recente listada no diretorio
if exist "C:\VulkanSDK\" (
    for /f "delims=" %%V in ('dir /b /ad /o-n "C:\VulkanSDK" 2^>nul') do (
        if exist "C:\VulkanSDK\%%V\Bin\glslc.exe" (
            set "GLSLC=C:\VulkanSDK\%%V\Bin\glslc.exe"
            goto :found_glslc
        )
    )
)

:: Nao encontrou
echo.
echo ERRO: 'glslc' nao encontrado.
echo   Instale o Vulkan SDK em: https://vulkan.lunarg.com
echo   Apos instalar, reabra o terminal e tente novamente.
echo.
goto :cleanup_error

:found_glslc
echo Usando glslc: %GLSLC%
echo.

:: ---------------------------------------------------------------------------
:: Escreve quad.vert
:: ---------------------------------------------------------------------------
set "VERT_FILE=%TMP_DIR%\quad.vert"
(
echo #version 450
echo layout^(push_constant^) uniform PC { mat4 proj; } pc;
echo layout^(location=0^) in vec2  inPos;
echo layout^(location=1^) in vec2  inUV;
echo layout^(location=2^) in vec4  inColor;
echo layout^(location=3^) in uint  inTexIdx;
echo layout^(location=0^) out vec2      outUV;
echo layout^(location=1^) out vec4      outColor;
echo layout^(location=2^) out flat uint outTexIdx;
echo void main^(^) {
echo     gl_Position = pc.proj * vec4^(inPos, 0.0, 1.0^);
echo     outUV      = inUV;
echo     outColor   = inColor;
echo     outTexIdx  = inTexIdx;
echo }
) > "%VERT_FILE%"

:: ---------------------------------------------------------------------------
:: Escreve quad.frag
:: ---------------------------------------------------------------------------
set "FRAG_FILE=%TMP_DIR%\quad.frag"
(
echo #version 450
echo #extension GL_EXT_nonuniform_qualifier : require
echo layout^(set=0, binding=0^) uniform sampler2D textures[128];
echo layout^(location=0^) in vec2      inUV;
echo layout^(location=1^) in vec4      inColor;
echo layout^(location=2^) in flat uint inTexIdx;
echo layout^(location=0^) out vec4 fragColor;
echo void main^(^) {
echo     fragColor = texture^(textures[nonuniformEXT^(inTexIdx^)], inUV^) * inColor;
echo }
) > "%FRAG_FILE%"

:: ---------------------------------------------------------------------------
:: Compila os shaders para SPIR-V
:: ---------------------------------------------------------------------------
echo [1/4] Compilando quad.vert -^> SPIR-V...
"%GLSLC%" --target-env=vulkan1.1 -o "%TMP_DIR%\quad.vert.spv" "%VERT_FILE%"
if %errorlevel% neq 0 (
    echo ERRO: falha ao compilar quad.vert
    goto :cleanup_error
)

echo [2/4] Compilando quad.frag -^> SPIR-V...
"%GLSLC%" --target-env=vulkan1.1 -o "%TMP_DIR%\quad.frag.spv" "%FRAG_FILE%"
if %errorlevel% neq 0 (
    echo ERRO: falha ao compilar quad.frag
    goto :cleanup_error
)

:: ---------------------------------------------------------------------------
:: Converte .spv -> array uint32_t C++ via PowerShell embutido
:: Usa PowerShell inline para nao depender de xxd nem de Python.
:: ---------------------------------------------------------------------------
echo [3/4] Gerando %OUT_HEADER%...

:: Garante que o diretorio de saida existe
if not exist "%SCRIPT_DIR%\src\Renderizador" mkdir "%SCRIPT_DIR%\src\Renderizador"

:: Script PowerShell que faz a conversao e escreve o header
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$vertSpv = [System.IO.File]::ReadAllBytes('%TMP_DIR%\quad.vert.spv');" ^
    "$fragSpv = [System.IO.File]::ReadAllBytes('%TMP_DIR%\quad.frag.spv');" ^
    "function To-CppArray($bytes, $name) {" ^
    "    if ($bytes.Length %% 4 -ne 0) { throw \"SPIR-V de '$name' nao e multiplo de 4 bytes.\" }" ^
    "    $words = for ($i = 0; $i -lt $bytes.Length; $i += 4) {" ^
    "        [System.BitConverter]::ToUInt32($bytes, $i)" ^
    "    };" ^
    "    $items = $words | ForEach-Object { '0x{0:x8}u' -f $_ };" ^
    "    $lines = @();" ^
    "    $chunk = @();" ^
    "    $lineLen = 4;" ^
    "    foreach ($item in $items) {" ^
    "        if ($lineLen + $item.Length + 2 -gt 80 -and $chunk.Count -gt 0) {" ^
    "            $lines += '    ' + ($chunk -join ', ') + ',';" ^
    "            $chunk = @(); $lineLen = 4" ^
    "        };" ^
    "        $chunk += $item; $lineLen += $item.Length + 2" ^
    "    };" ^
    "    if ($chunk.Count -gt 0) { $lines += '    ' + ($chunk -join ', ') };" ^
    "    return ('static const uint32_t ' + $name + '[] = {`n' +" ^
    "            ($lines -join \"`n\") + \"`n};`n\" +" ^
    "            'static const size_t ' + $name + '_size = ' + $bytes.Length + 'u;')" ^
    "};" ^
    "$header = @(" ^
    "    '// shaders_embedded.hpp'," ^
    "    '// GERADO AUTOMATICAMENTE por compile_shaders.bat -- nao edite manualmente.'," ^
    "    '// Execute compile_shaders.bat para regenerar apos alterar os shaders.'," ^
    "    '//'," ^
    "    '// Contem os shaders padrao do backend Vulkan pre-compilados em SPIR-V.'," ^
    "    '// Embutir os arrays aqui elimina a dependencia de glslang para o pipeline'," ^
    "    '// padrao; glslang continua sendo usado apenas por shader_create() em runtime.'," ^
    "    '#pragma once'," ^
    "    '#ifndef SHADERS_EMBEDDED_HPP'," ^
    "    '#define SHADERS_EMBEDDED_HPP'," ^
    "    '#include <cstdint>'," ^
    "    '#include <cstddef>'," ^
    "    ''," ^
    "    (To-CppArray $vertSpv 'k_vert_spv_embedded')," ^
    "    ''," ^
    "    (To-CppArray $fragSpv 'k_frag_spv_embedded')," ^
    "    ''," ^
    "    '#endif /* SHADERS_EMBEDDED_HPP */'" ^
    ") -join \"`n\";" ^
    "[System.IO.File]::WriteAllText('%OUT_HEADER%', $header, [System.Text.Encoding]::UTF8);"

if %errorlevel% neq 0 (
    echo ERRO: falha ao gerar o header via PowerShell
    goto :cleanup_error
)

:: ---------------------------------------------------------------------------
:: Limpeza e mensagem final
:: ---------------------------------------------------------------------------
rmdir /s /q "%TMP_DIR%" 2>nul

echo [4/4] Pronto!
echo.
echo Arquivo gerado: %OUT_HEADER%
echo Commite este arquivo junto com o codigo-fonte para que
echo outros contribuidores nao precisem rodar este script.
echo.
goto :eof

:cleanup_error
rmdir /s /q "%TMP_DIR%" 2>nul
exit /b 1
