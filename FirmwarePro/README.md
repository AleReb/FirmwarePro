# FirmwarePro (estado actual)

Este folder es el **main de trabajo** actual.

## Estado
- Compila para `esp32:esp32:esp32` (ESP32 Dev Module).
- BTN2 long click está deshabilitado y reservado para una función futura.
- Se está usando estructura modular por archivos (`ui.ino`, `gps.ino`, `http.ino`, etc.).

## Pendiente (prioridad)

### 1) Menú y UX
- [ ] Definir e implementar nueva función para **long click** (BTN2).
- [ ] Revisar matriz final de botones:
  - BTN1 corto: navegar
  - BTN2 corto: seleccionar/entrar
  - BTN2 largo: nueva acción (por definir)
- [ ] Ajustar comportamiento de "Volver" en submenús para que sea consistente.

### 2) Header OLED
- [ ] Pulir layout para evitar solapes (hora, señal, satélites, batería, estado TX/SD).
- [ ] Validar legibilidad real en hardware (no solo en lógica).

### 3) Estabilidad de runtime
- [ ] Revisar watchdog + timeouts en HTTP/GNSS para evitar bloqueos intermitentes.
- [ ] Mejorar manejo de errores de SD/HTTP con mensajes más claros y reintentos controlados.
- [ ] Unificar criterios de estado (`OK/FAIL`) entre módulos.

### 4) Calidad de código
- [ ] Refactor de globals (reducir acoplamiento entre módulos).
- [ ] Centralizar constantes de tiempo/umbrales en un solo archivo de configuración.
- [ ] Estandarizar logs seriales por prefijo de módulo (`[UI]`, `[GPS]`, `[SD]`, `[HTTP]`, etc.).

### 5) Repo y build
- [ ] Limpiar artefactos de compilación del control de versiones (`build/`, binarios, objetos).
- [ ] Agregar/ajustar `.gitignore` para evitar subir archivos generados por Arduino.

## Nota
Si algo se rompe, usar commits pequeños por bloque (1 cambio lógico = 1 commit) y subir cada avance verificando compilación.
