TupiEngine

Uma engine 2D purista, veloz e minimalista para jogos estilo 16-bits.

A TupiEngine nasceu de uma paixão doentia por programação de baixo nível, movida a hiperfoco e muita vontade de criar algo do zero. Escrita inteiramente em C/C++ "raiz" e controlada via LuaJIT FFI, ela é uma engine brasileira pensada para duas coisas: ajudar quem está começando a dar os primeiros passos sem se frustrar, e dar aos veteranos o controle absoluto do hardware sem o peso das engines comerciais gigantescas.

Se você quer fazer um jogo com estética retro (SNES, GBC) com uma performance absurda, você está no lugar certo.
🧠 Como a TupiEngine Funciona?

A mágica da TupiEngine está na sua arquitetura dividida em duas camadas:

    O Coração de C/C++ (O Motor): Toda a lógica pesada, renderização em lote (batching), física de colisão (AABB), gerenciamento de memória e processamento de áudio (via Miniaudio) são feitos em C++. Os dados são orientados a arrays fixos (Data-Oriented Design), garantindo que tudo caiba na cache do processador.

    O Cérebro de LuaJIT (O Volante): A interface do usuário é 100% Lua. Mas não é um binding lento. Usamos o LuaJIT FFI, o que significa que o código Lua compila para código de máquina em tempo de execução e acessa as estruturas em C diretamente na memória ram, com custo de chamada (overhead) praticamente zero.

🟢 Para Programadores Iniciantes

Aprender a programar jogos pode ser assustador quando você tem que lidar com ponteiros, vazamento de memória e compilações de 30 minutos. Com a TupiEngine:

    Você programa em Lua: Uma linguagem simples, amigável e perdoadora.

    Sem dor de cabeça: A engine já gerencia a janela, os gráficos, o input e o áudio. Você só foca em fazer o seu jogo.

    Feedback Instantâneo: Como o jogo roda via script Lua, você não precisa recompilar código C++ toda vez que mudar a cor de um sprite. É só salvar o arquivo e rodar.

🔴 Para Programadores Experientes

Você já sabe programar e está cansado da lentidão e das caixas pretas das engines modernas?

    Performance Absurda: Renderizador em batch escrito à mão. Suporta até 4096 quads por draw call.

    Baixo Nível: Usa OpenGL (via X11) nativo no Linux para tirar o máximo da máquina (ótimo para quem roda sistemas enxutos via terminal) e DirectX 11 direto no Windows.

    Memória Previsível: Sem alocações dinâmicas malucas durante o gameplay. Os limites são definidos por macros C-style (ENGINE_MAX_OBJECTS 256, ENGINE_MAX_PARTICLES 512), garantindo zero garbage collection na engine base.

🚀 Features Principais

    Renderização: Batching dinâmico, suporte a Tilemaps otimizados, fade de tela, suporte a texturas RGBA, e blending invertido.

    Sistemas Prontos: Emissores de partículas embutidos, animadores de sprite configuráveis e sistema de câmera com suporte a tracking suave (lerp) e screen shake.

    Física: Colisão AABB rápida e matemática vetorial embutida.

    Áudio: Mixagem de som de alta performance usando a biblioteca Miniaudio.

    Cross-Platform: Compila liso tanto no Linux (OpenGL 2.1 + X11) quanto no Windows (DX11).

🎮 Exemplo Mínimo: Como é fácil?

Com poucas linhas de Lua, você já tem uma janela e um objeto desenhado na tela:
Lua

local E = require("engine")

-- Cria uma janela de 800x600, chamada "Meu Jogo"
local jogo = E.nova(800, 600, "Meu Jogo", 1)

-- Inicializa o áudio e a cor de fundo (Azul escuro)
jogo:audio_init()
jogo:fundo(10, 10, 40)

-- Loop principal
while jogo:rodando() do
    jogo:eventos()
    jogo:atualizar(0) -- 0 para usar o delta time automático
    
    jogo:limpar()
    
    -- Desenha um retângulo vermelho e um círculo no meio da tela
    jogo:ret(350, 250, 100, 100, 255, 0, 0)
    jogo:circulo(400, 300, 20, 0, 255, 0, true)
    
    jogo:desenhar()
    jogo:apresentar()
    
    jogo:fps(60) -- Crava em 60 frames por segundo
end

jogo:destruir()

🛠️ Como Compilar a Engine (Source)

Se você quiser modificar o coração em C++ da engine, usamos CMake. A compilação é limpa e sem dependências bizarras.

No Linux (X11 / OpenGL):
Você precisará do CMake, GCC/Clang, e das bibliotecas de desenvolvimento do X11 e libpng.
Bash

mkdir build && cd build
cmake -DBACKEND=GL ..
make

No Windows (DirectX 11):
DOS

mkdir build && cd build
cmake -DBACKEND=DX11 ..
cmake --build .

Nota: Para rodar os jogos, basta garantir que o libengine.so (ou .dll) esteja na mesma pasta que seus scripts Lua e o LuaJIT instalado.
