# Maruja Engine

**Maruja** es un motor de videojuegos desarrollado en C++ para la asignatura *Motores de Videojuegos* (3º año, Universidad CITM). En esta segunda versión (**A2 - Editor del motor**) el motor ya no depende de los FBX para ejecutar: todos los recursos se importan a un **formato propio** en la carpeta `Library`, se gestionan con **conteo de referencias**, la escena se puede **guardar y cargar**, hay modo **Play / Pause / Stop** y el render usa **frustum culling acelerado con un octree**.

🔗 **Repositorio Oficial:** [MotorVideojuego en GitHub](https://github.com/JanCorredor/MotorVideojuego)

## 👥 Colaboradores
- **Rubén Mateo (Rubenmg0):** [https://github.com/Rubenmg9](https://github.com/Rubenmg9)
- **Marc Jiménez (MarcJM-00):** [https://github.com/MarcJM-00](https://github.com/MarcJM-00)
- **Jan Corredor (JanCorredor):** [https://github.com/JanCorredor](https://github.com/JanCorredor)

---

## 🚀 Funcionalidades de la Entrega 2

### Escena inicial
- Al arrancar se carga automáticamente la escena **`Assets/Street/StreetEnvironment.fbx`** (con sus texturas) y una **Main Camera**.
- `File > Load Street Environment` vuelve a cargarla en cualquier momento.

### GameObjects e Inspector
- **Jerarquía:** crear vacío, crear hijos (vacío, cámara, primitivas), duplicar, borrar y **reparent** arrastrando un GameObject encima de otro (o a la zona vacía para llevarlo a la raíz).
- **Transform:** posición, rotación y escala editables.
- **Mesh:** selector con todas las mallas importadas (y las primitivas *built-in*) o **drag & drop** de una malla desde la ventana Assets. Muestra UID, referencias, vértices, triángulos y AABB local/mundo.
- **Material:** color, selector de textura o **drag & drop** de una textura desde Assets, checkerboard y vista previa.
- **Camera:** es un componente (FOV, near, far, frustum culling, "Usar como Main Camera").
- **Add Component** (Mesh, Material, Camera) y **Quitar componente**.

### Cámara, selección y culling
- **Mouse picking:** clic izquierdo en la vista Scene. El rayo se prueba primero contra el **octree**, después contra las AABB (ordenadas por distancia) y al final contra los triángulos.
- Todas las mallas tienen **AABB** y se descartan con **frustum culling** (vista Scene con la cámara del editor y vista Game con la Main Camera).
- **Octree** como estructura de aceleración (culling y picking). Se reconstruye solo cuando algo cambia.
- Visualización en el editor (botón **Gizmos** de la vista Scene o `Configuration > Renderer`):
  - AABB de todas las mallas: **verde** = la Main Camera la ve, **rojo** = descartada.
  - Frustum de la Main Camera, nodos del octree y el **rayo del picking**.
  - "Vista previa del culling": la vista Scene solo dibuja lo que ve la Main Camera.
  - Estadísticas en tiempo real: objetos dibujados, nodos del octree visitados, AABB probadas, triángulos probados en el picking...

### Formato propio y Library
- Al arrancar se revisa `Assets/`: todo lo que no esté importado se importa a `Library/`.
- **Texturas** → `Library/Textures/<UID>.tex` (cabecera + píxeles RGB/RGBA listos para OpenGL).
- **Mallas** → `Library/Meshes/<UID>.mesh` (binario: cabecera + vértices + índices).
- **Modelos** → `Library/Models/<UID>.model` (JSON con la jerarquía de nodos y los UID de mallas y texturas).
- Cada asset tiene un **`.meta`** (JSON) con sus UID y sus opciones de importación. Si se borra la carpeta `Library` se **regenera idéntica** a partir de `Assets/` + los `.meta` (mismos UID, así que las escenas guardadas siguen funcionando).
- Las texturas que vienen **dentro** de un FBX (por ejemplo el Heavy) se extraen a una carpeta junto al modelo y se importan como cualquier textura.

### Escenas
- `File > Save Scene` (**Ctrl+S**) / `Save Scene As...` guardan la escena en `Assets/Scenes/<nombre>.scene` (JSON).
- `File > Open Scene`, `File > New Scene`, doble clic en un `.scene` de la ventana Assets o arrastrarlo a la vista Scene.

### Play / Pause / Stop
- Barra superior con **Play**, **Pause/Resume**, **Step** (avanza un frame en pausa) y **Stop**, tiempo de juego y escala de tiempo.
- Al pulsar Play se guarda el estado de la escena; al pulsar **Stop se recupera exactamente** (aunque se hayan movido o borrado objetos). La barra cambia de color en modo Play.

### Gestión de recursos
- **Ventana Assets:** árbol de carpetas + contenido de la carpeta (texturas, modelos y sus mallas, escenas), crear carpetas, refrescar, borrar (con confirmación), reimportar. Se actualiza sola si cambian los ficheros.
- **Importar:** arrastra ficheros desde el explorador de Windows a la ventana del motor; se copian a la carpeta abierta en Assets y se importan. Si es un modelo, también se copian las texturas que tenga al lado.
- **Borrar** un asset borra también su `.meta` y sus ficheros de `Library`. Los GameObjects que lo usaban se quedan sin malla o con la textura *missing* (rosa).
- **Conteo de referencias:** una textura o malla está **una sola vez en memoria** aunque la usen muchos GameObjects. Se carga con la primera referencia y se descarga cuando llega a 0. La ventana **Resources** muestra en tiempo real las referencias, si está en memoria y cuánta ocupa.
- **Opciones de importación** (seleccionando el asset en la ventana Assets, se ven en el Inspector y se guardan en el `.meta`):
  - Texturas: Flip X / Flip Y, filtrado (nearest, linear, trilinear con mipmaps), wrapping (repeat, clamp, mirrored) y tamaño máximo.
  - Modelos: escala global, convertir unidades a metros, eje vertical del fichero (±X, ±Y, ±Z), ignorar cámaras e ignorar luces.

---

## 🎮 Controles de Uso

### Cámara del editor (vista Scene)
- **Click Derecho (Mantener) + WASD / Q / E:** movimiento libre (FPS-like) y mirar alrededor.
- **Rueda del Ratón:** acercar / alejar la cámara.
- **Alt + Click Izquierdo (Mantener):** orbitar alrededor del objeto seleccionado.
- **Tecla `F`:** encuadrar el objeto seleccionado.
- **Shift (Mantener):** duplica la velocidad de la cámara.
- **Click Izquierdo:** seleccionar un GameObject (mouse picking).

### Atajos
- **Ctrl+S:** guardar escena. **Ctrl+P:** Play / Stop. **Ctrl+Shift+P:** Pause.
- **Ctrl+D:** duplicar el GameObject seleccionado. **Supr:** borrar el GameObject (o el asset seleccionado si la ventana Assets tiene el foco).

### Drag & Drop
- **Desde el explorador del sistema** a la ventana del motor: `.fbx/.obj/.dae` (se importa y se instancia), `.png/.jpg/.tga/.dds/.bmp` (se importa y se aplica al GameObject seleccionado), `.scene` (se abre).
- **Desde la ventana Assets:**
  - Modelo → a la vista Scene (aparece donde apunta el ratón) o a la Hierarchy (como hijo).
  - Malla (despliega un modelo con la flecha) → al campo *Malla* del Inspector o a la vista Scene.
  - Textura → al campo *Textura* del Inspector, a un objeto de la vista Scene o a un nodo de la Hierarchy.
- **Dentro de la Hierarchy:** arrastra un GameObject sobre otro para hacerlo hijo.

---

## ⭐ Funcionalidades Extra
- Opciones de importado de la entrega aplazada (texturas y mallas) guardadas en los `.meta`, con *Apply / Revert / Reimportar*.
- Explorador de assets con navegación por carpetas, borrado de carpetas y vista de las mallas de cada modelo.
- Extracción automática de texturas embebidas en los FBX.
- Al importar un modelo desde fuera del proyecto se copian también sus texturas.
- Comparativa octree / fuerza bruta (opción "Usar octree") con estadísticas.
- Descarga diferida de recursos (si un recurso se suelta y se vuelve a pedir en el mismo frame, por ejemplo al pulsar Stop, no se recarga).
- Escala de tiempo y modo *Step* en la simulación.
- De la entrega 1: jerarquía padre-hijo, grid y ejes, mouse picking con outline, wireframe, *missing texture*, visor de Z-buffer, opciones de render, normales por vértice y por cara.

---

## 📝 Notas para el Profesor
- La carpeta `Library/` no se sube al repositorio: se genera al arrancar a partir de `Assets/` y de los `.meta` (que sí se suben).
- Las texturas muy grandes (por ejemplo `water.jpg`, 8368x5584) se reducen al importar a 2048 px (opción *Tamaño máximo* del `.meta`).
- `BakerHouse.fbx` trae en el propio fichero una rotación de -90º en X que lo dejaría tumbado; su `.meta` usa la opción de importación **"Eje vertical del fichero: -Z"** para importarlo de pie.
- El panel de Z-buffer aparece si lo activas en el apartado View.
- Si un GameObject usa una textura que ya no existe se dibuja la *missing texture*.
