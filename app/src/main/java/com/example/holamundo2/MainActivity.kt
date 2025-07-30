package com.example.holamundo2

import android.app.Activity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.util.Log
import android.view.WindowManager
// ELIMINADO: import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.*
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class MainActivity : Activity() {

    companion object {
        const val TAG = "OpenXRHolaMundo"

        init {
            try {
                System.loadLibrary("holamundo_native")
                Log.d(TAG, "Librería nativa cargada correctamente")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Error cargando librería nativa: ${e.message}")
                throw e
            }
        }
    }

    // Declaraciones de funciones nativas
    private external fun nativeInitialize(): Boolean
    private external fun nativeCreateSession(): Boolean
    private external fun nativeRunFrame(): Boolean
    private external fun nativeShutdown()

    private var glSurfaceView: GLSurfaceView? = null
    private var isRunning = false
    private var renderThread: Thread? = null

    // Crear nuestro propio CoroutineScope para reemplazar lifecycleScope
    private val activityScope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        Log.d(TAG, "=== Iniciando aplicación OpenXR HolaMundo ===")

        try {
            // Configurar la ventana para VR
            setupVRWindow()

            // Crear GLSurfaceView para inicializar contexto OpenGL
            setupGLSurfaceView()

            // Inicializar OpenXR de forma asíncrona
            activityScope.launch {  // ← Usando nuestro scope en lugar de lifecycleScope
                initializeOpenXR()
            }

        } catch (e: Exception) {
            Log.e(TAG, "Error durante onCreate: ${e.message}", e)
            finish()
        }
    }

    private fun setupVRWindow() {
        // Configurar ventana para VR
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
        window.addFlags(WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED)

        // Ocultar navegación del sistema
        window.decorView.systemUiVisibility = (
                android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                        android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                        android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                        android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                        android.view.View.SYSTEM_UI_FLAG_FULLSCREEN or
                        android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                )
    }

    private fun setupGLSurfaceView() {
        glSurfaceView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3) // OpenGL ES 3.0
            setEGLConfigChooser(8, 8, 8, 8, 24, 8)
            setRenderer(object : GLSurfaceView.Renderer {
                override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                    Log.d(TAG, "OpenGL Surface creada")
                }

                override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                    Log.d(TAG, "OpenGL Surface cambiada: ${width}x${height}")
                }

                override fun onDrawFrame(gl: GL10?) {
                    // El renderizado real se hace en el thread nativo
                }
            })
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        setContentView(glSurfaceView)
    }

    private suspend fun initializeOpenXR() = withContext(Dispatchers.Default) {
        try {
            Log.d(TAG, "Inicializando OpenXR...")

            // Esperar un poco para que el contexto OpenGL esté listo
            Thread.sleep(500)

            if (!nativeInitialize()) {
                Log.e(TAG, "Error al inicializar OpenXR")
                withContext(Dispatchers.Main) {
                    finish()
                }
                return@withContext
            }

            Log.d(TAG, "Creando sesión OpenXR...")
            if (!nativeCreateSession()) {
                Log.e(TAG, "Error al crear sesión OpenXR")
                nativeShutdown()
                withContext(Dispatchers.Main) {
                    finish()
                }
                return@withContext
            }

            Log.d(TAG, "OpenXR inicializado correctamente, iniciando loop de renderizado")
            isRunning = true
            startRenderLoop()

        } catch (e: Exception) {
            Log.e(TAG, "Error durante inicialización OpenXR: ${e.message}", e)
            withContext(Dispatchers.Main) {
                finish()
            }
        }
    }

    private fun startRenderLoop() {
        renderThread = Thread({
            Log.d(TAG, "=== Iniciando loop de renderizado ===")

            // Configurar prioridad del thread
            android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_URGENT_DISPLAY)

            var frameCount = 0
            var lastFpsTime = System.currentTimeMillis()

            while (isRunning) {
                try {
                    val frameStart = System.nanoTime()

                    if (!nativeRunFrame()) {
                        Log.e(TAG, "Error en frame de renderizado")
                        break
                    }

                    frameCount++

                    // Log FPS cada 90 frames (1 segundo aprox)
                    if (frameCount % 90 == 0) {
                        val currentTime = System.currentTimeMillis()
                        val deltaTime = currentTime - lastFpsTime
                        val fps = (90 * 1000) / deltaTime.toFloat()
                        Log.d(TAG, "FPS: ${"%.1f".format(fps)}")
                        lastFpsTime = currentTime
                    }

                    // Control de FPS: 90 FPS = ~11.1ms por frame
                    val frameTime = (System.nanoTime() - frameStart) / 1000000 // ms
                    val targetFrameTime = 11L // ms
                    val sleepTime = targetFrameTime - frameTime

                    if (sleepTime > 0) {
                        Thread.sleep(sleepTime)
                    }

                } catch (e: InterruptedException) {
                    Log.d(TAG, "Loop de renderizado interrumpido")
                    break
                } catch (e: Exception) {
                    Log.e(TAG, "Error en loop de renderizado: ${e.message}", e)
                    break
                }
            }

            Log.d(TAG, "=== Loop de renderizado terminado ===")
        }, "OpenXR-RenderThread")

        renderThread?.start()
    }

    override fun onResume() {
        super.onResume()
        glSurfaceView?.onResume()
        Log.d(TAG, "Aplicación reanudada")
    }

    override fun onPause() {
        super.onPause()
        glSurfaceView?.onPause()
        Log.d(TAG, "Aplicación pausada")
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.d(TAG, "=== Cerrando aplicación ===")

        isRunning = false

        // Cancelar corrutinas
        activityScope.cancel()

        // Esperar a que termine el thread de renderizado
        renderThread?.let { thread ->
            try {
                thread.join(2000) // Esperar máximo 2 segundos
                if (thread.isAlive) {
                    Log.w(TAG, "Forzando interrupción del thread de renderizado")
                    thread.interrupt()
                }
            } catch (e: InterruptedException) {
                Log.w(TAG, "Timeout esperando thread de renderizado")
            }
        }

        // Limpiar recursos OpenXR
        try {
            nativeShutdown()
            Log.d(TAG, "Recursos OpenXR liberados")
        } catch (e: Exception) {
            Log.e(TAG, "Error durante shutdown: ${e.message}", e)
        }

        Log.d(TAG, "=== Aplicación terminada ===")
    }

    override fun onBackPressed() {
        // En VR, el botón back debería manejarse de manera especial
        // Por ahora, simplemente cerramos la app
        Log.d(TAG, "Back pressed - cerrando aplicación")
        finish()
    }
}