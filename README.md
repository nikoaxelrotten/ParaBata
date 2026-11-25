# ParaBata

VST3 **solo para Windows** que separa una pista de bateria (estereo o dual mono) en piezas: **Kick, Snare, Toms, Hi-Hat, Cymbals y Other**. Cada pieza sale por su propia salida y tambien por la mezcla, con nivel, mute y solo. Ademas puede **reemplazar o reforzar cada pieza con tus propios golpes** (hasta 4 sonidos por pieza, como capas de velocidad) y **enviar los golpes detectados como MIDI**.

## Como funciona (y limites)

No usa IA: la separacion es por DSP.
1. La senal se parte en 6 bandas (cruces Linkwitz-Riley).
2. Se detecta cada golpe y se clasifica segun como reparte su energia entre las bandas.
3. El audio va con una latencia fija (40 ms, la reporta al DAW) y cada golpe abre su pieza justo cuando llega; el **Tail** de cada pieza dice cuanto tiempo queda abierta.

Funciona mejor con baterias grabadas o programadas con poco sangrado. Limites conocidos: golpes simultaneos (por ejemplo kick + hi-hat) se asignan a una sola pieza; ride y crash comparten Cymbals; con mezclas completas (con bajo, guitarras) la separacion no es confiable.

Controles globales: **SENS** (sensibilidad), **FOCUS** (cuanto se aparta lo que no es golpe), **KICK<>TOM** y **HAT<>CYM** (ajustan la frontera entre esas piezas segun tu bateria) y **VELOCITY** (cuanto influye la fuerza del golpe en el sample). Compilacion: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` y `cmake --build build --config Release`; GitHub Actions compila el VST3 de Windows en cada push.
