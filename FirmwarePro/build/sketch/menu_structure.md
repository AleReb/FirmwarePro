#line 1 "C:\\gitshubs\\HIRIPROBASE01\\FirmwarePro_Roto\\FirmwarePro\\menu_structure.md"
# Estructura del Menú FirmwarePro

Este documento detalla la estructura de menús, íconos y lógica de navegación actual en `FirmwarePro/ui.ino`.

## Resumen de Navegación
- **Botón 1 (PIN 19):** `ui_btn1_click` - Ciclar opción (`menuIndex + 1`).
- **Botón 2 (PIN 23):**
    - **Click Corto (`ui_btn2_click`):** Seleccionar / Entrar a submenú.
    - **Click Largo (`ui_btn2_hold`):**
        - **Menú Principal (Item "Empezar Muestreo"):** Iniciar/Detener Streaming (Toggle).
        - **Submenús:** Volver / Salir (Back).

## Estructura Jerárquica Actual

### 1. Menú Principal (Depth 0)
| Índice | Texto | Icono (Hex) | Descripción / Acción |
|---|---|---|---|
| 0 | `PM2.5` | `0` (null) | Muestra valor grande de PM2.5 |
| 1 | `Temperatura` | `0` (null) | Muestra valor grande de Temperatura |
| 2 | `Humedad` | `0` (null) | Muestra valor grande de Humedad |
| 3 | `Empezar Muestreo` | `0x01A5` (📈) | **Hold:** Toggle Streaming/Logging |
| 4 | `OPCIONES` | `0x0192` (⚙️) | Submenú: Ir a Depth 1 |

### 2. Submenú "Opciones" (Depth 1)
| Índice | Texto | Icono (Hex) | Acción |
|---|---|---|---|
| 0 | `Mensajes` | `0x00EC` (✉️) | Submenú: Ir a Depth 2 (Mensajes) |
| 1 | `Configuracion` | `0x015b` (⚙️) | Submenú: Ir a Depth 3 (Configuración) |
| 2 | `Informacion` | `0x0185` (ℹ️) | Submenú: Ir a Depth 4 (Información) |
| 3 | `Volver` | `0x01A9` (←) | Volver a Depth 0 |

### 3. Submenú "Mensajes" (Depth 2)
| Índice | Texto | Icono (Hex) | Acción |
|---|---|---|---|
| 0 | `Camion` | `0x2A1` (🚚) | Log Serial: "[UI] Mensaje seleccionado: Camion" (Placeholder) |
| 1 | `Humo` | `0x26C` (💨) | Log Serial: "[UI] Mensaje seleccionado: Humo" (Placeholder) |
| 2 | `Construccion` | `0x09E` (🏗️) | Log Serial: "[UI] Mensaje seleccionado: Construccion" (Placeholder) |
| 3 | `Volver` | `0x01A9` (←) | Volver a Depth 1 |

### 4. Submenú "Configuración" (Depth 3)
| Índice | Texto | Icono (Hex) | Acción |
|---|---|---|---|
| 0 | `REDES` | `0x01CC` (🌐) | Log Serial: "[CFG] REDES" (Placeholder) |
| 1 | `GUARDADO` | `0x0176` (💾) | Log Serial: "[CFG] GUARDADO" (Placeholder) |
| 2 | `RTC` | `0x01CB` (🕒) | Log Serial: "[CFG] RTC" (Placeholder) |
| 3 | `Reiniciar` | `0x00D5` (↻) | Acción: `handleRestart()` (Reinicia ESP32) |
| 4 | `Volver` | `0x01A9` (←) | Volver a Depth 1 |

### 5. Submenú "Información" (Depth 4)
| Índice | Texto | Icono (Hex) | Acción |
|---|---|---|---|
| 0 | `Version` | `0x0085` (🆔) | Log Serial: Versión |
| 1 | `Bateria` | `0x00D1` (🔋) | Log Serial: Voltaje Batería |
| 2 | `Memoria` | `0x0093` (💾) | Log Serial: Memoria Libre |
| 3 | `Volver` | `0x01A9` (←) | Volver a Depth 1 |

## Comparación y Estado Actual
- **Estructuralmente idéntico** a `HIRI_PR0_MENU`.
- **Diferencia Funcional:**
    - `HIRI_PR0_MENU` tiene funciones `handle...` implementadas con `delay()` y UI bloqueante para mostrar feedback en pantalla.
    - `FirmwarePro` tiene la lógica de navegación pero las acciones finales (excepto Restart y Streaming) son solo logs por Serial (Placeholders).
    - `FirmwarePro` usa `ui_btn2_hold` en el menú principal para activar/desactivar el modo Streaming, algo que es una evolución respecto al menú base.
