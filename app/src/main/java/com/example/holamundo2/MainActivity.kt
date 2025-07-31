package com.example.holamundo2

import android.app.Activity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.util.Log
import android.view.Surface
import android.view.WindowManager
import kotlinx.coroutines.*
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class MainActivity : Activity() {

    companion object {
        const val TAG = "OpenXRHolaMundo"

        init {
            try {
                Log.d(TAG, "Intentando cargar librería nativa...")
                System.loadLibrary("holamundo_native")
                Log.d(TAG, "Librería nativa cargada correctamente")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Error cargando librería nativa: ${e.message}")
                throw e
            } catch (e: Exception) {
                Log.e(TAG, "Error inesperado al cargar librería: ${e.message}")
                throw e
            }
        }
    }

    // Declaraciones de funciones nativas
    private external fun nativeInitialize(): Boolean
    private external fun nativeSetupEGL(surface: Surface): Boolean
    private external fun nativeCreateSession(): Boolean
    private external fun nativeRunFrame(): Boolean
    private external fun nativeShutdown()

    private var glSurfaceView: GLSurfaceView? = null
    private var isRunning = false
    private var openxrInitialized = false
    private var surfaceReady = false

    private val activityScope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.d(TAG, "onCreate() iniciado")

        try {
            Log.d(TAG, "Configurando ventana VR...")
            setupVRWindow()

            Log.d(TAG, "Configurando GLSurfaceView...")
            setupGLSurfaceView()

            Log.d(TAG, "onCreate() completado")
        } catch (e: Exception) {
            Log.e(TAG, "Error durante onCreate: ${e.message}", e)
            finish()
        }
    }

    private fun setupVRWindow() {
        Log.d(TAG, "Configurando flags de ventana...")
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
        window.addFlags(WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED)

        window.decorView.systemUiVisibility = (
                android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                        android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                        android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                        android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                        android.view.View.SYSTEM_UI_FLAG_FULLSCREEN or
                        android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                )
        Log.d(TAG, "Configuración de ventana completada")
    }

    private fun setupGLSurfaceView() {
        Log.d(TAG, "Creando GLSurfaceView...")
        glSurfaceView = GLSurfaceView(this).apply {
            Log.d(TAG, "Configurando versión de contexto OpenGL ES...")
            setEGLContextClientVersion(3)

            Log.d(TAG, "Configurando selector de configuración EGL...")
            setEGLConfigChooser(8, 8, 8, 8, 24, 8)

            setRenderer(object : GLSurfaceView.Renderer {
                override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                    Log.d(TAG, "onSurfaceCreated() - OpenGL Surface creada")
                }

                override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                    Log.d(TAG, "onSurfaceChanged() - OpenGL Surface cambiada: ${width}x${height}")

                    val surface = holder?.surface
                    if (surface != null && surface.isValid) {
                        Log.d(TAG, "Surface válido obtenido")
                        if (!surfaceReady) {
                            surfaceReady = true
                            Log.d(TAG, "Iniciando inicialización OpenXR en hilo OpenGL...")
                            // IMPORTANTE: Ejecutar en el hilo actual (hilo de renderizado OpenGL)
                            initializeOpenXRInGLThread(surface)
                        }
                    } else {
                        Log.e(TAG, "Surface es null o inválido en onSurfaceChanged")
                    }
                }

                override fun onDrawFrame(gl: GL10?) {
                    if (openxrInitialized && isRunning) {
                        // Solo llamar a nativeRunFrame si OpenXR está completamente inicializado
                        nativeRunFrame()
                    } else {
                        // Renderizar un color de fondo mientras esperamos la inicialización
                        gl?.glClear(GL10.GL_COLOR_BUFFER_BIT)
                    }
                }
            })

            Log.d(TAG, "Configurando modo de renderizado...")
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY

            holder?.addCallback(object : android.view.SurfaceHolder.Callback2 {
                override fun surfaceCreated(holder: android.view.SurfaceHolder) {
                    Log.d(TAG, "surfaceCreated() - SurfaceHolder creado")
                }

                override fun surfaceChanged(
                    holder: android.view.SurfaceHolder,
                    format: Int,
                    width: Int,
                    height: Int
                ) {
                    Log.d(TAG, "surfaceChanged() - Nuevas dimensiones: ${width}x$height")
                }

                override fun surfaceDestroyed(holder: android.view.SurfaceHolder) {
                    Log.d(TAG, "surfaceDestroyed() - SurfaceHolder destruido")
                    surfaceReady = false
                    isRunning = false
                    openxrInitialized = false
                }

                override fun surfaceRedrawNeeded(holder: android.view.SurfaceHolder) {
                    Log.d(TAG, "surfaceRedrawNeeded()")
                }
            })
        }

        Log.d(TAG, "Estableciendo GLSurfaceView como contenido...")
        setContentView(glSurfaceView)
        Log.d(TAG, "GLSurfaceView configurado correctamente")
    }

    private fun initializeOpenXRInGLThread(surface: Surface) {
        Log.d(TAG, "initializeOpenXRInGLThread() - Ejecutando en hilo OpenGL")
        Log.d(TAG, "Hilo actual: ${Thread.currentThread().name}")

        try {
            // 1. Inicializar OpenXR (puede ejecutarse en cualquier hilo)
            Log.d(TAG, "Paso 1/3: Llamando a nativeInitialize()...")
            if (!nativeInitialize()) {
                Log.e(TAG, "nativeInitialize() falló")
                runOnUiThread {
                    Log.e(TAG, "Cerrando aplicación por fallo en inicialización")
                    finish()
                }
                return
            }
            Log.d(TAG, "✓ nativeInitialize() completado con éxito")

            // 2. Configurar EGL (DEBE ejecutarse en el hilo OpenGL)
            Log.d(TAG, "Paso 2/3: Llamando a nativeSetupEGL() en hilo OpenGL...")
            if (!nativeSetupEGL(surface)) {
                Log.e(TAG, "nativeSetupEGL() falló")
                nativeShutdown()
                runOnUiThread {
                    Log.e(TAG, "Cerrando aplicación por fallo en configuración EGL")
                    finish()
                }
                return
            }
            Log.d(TAG, "✓ nativeSetupEGL() completado con éxito")

            // 3. Crear sesión OpenXR (DEBE ejecutarse en el hilo OpenGL)
            Log.d(TAG, "Paso 3/3: Llamando a nativeCreateSession() en hilo OpenGL...")
            if (!nativeCreateSession()) {
                Log.e(TAG, "nativeCreateSession() falló")
                nativeShutdown()
                runOnUiThread {
                    Log.e(TAG, "Cerrando aplicación por fallo en creación de sesión")
                    finish()
                }
                return
            }
            Log.d(TAG, "✓ nativeCreateSession() completado con éxito")

            // Todo listo!
            openxrInitialized = true
            isRunning = true

            runOnUiThread {
                Log.i(TAG, "🎉 OpenXR completamente inicializado y listo para renderizar!")
            }

        } catch (e: Exception) {
            Log.e(TAG, "Excepción en initializeOpenXRInGLThread: ${e.message}", e)
            runOnUiThread {
                Log.e(TAG, "Cerrando aplicación por excepción en inicialización")
                finish()
            }
        }
    }

    override fun onResume() {
        super.onResume()
        Log.d(TAG, "onResume() - Reanudando actividad")

        glSurfaceView?.onResume()
        Log.d(TAG, "GLSurfaceView reanudado")

        if (openxrInitialized) {
            isRunning = true
            Log.d(TAG, "Renderizado reanudado")
        }
    }

    override fun onPause() {
        super.onPause()
        Log.d(TAG, "onPause() - Pausando actividad")

        isRunning = false
        glSurfaceView?.onPause()
        Log.d(TAG, "GLSurfaceView pausado")
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.d(TAG, "onDestroy() - Destruyendo actividad")

        isRunning = false
        openxrInitialized = false
        surfaceReady = false

        Log.d(TAG, "Cancelando corrutinas...")
        activityScope.cancel()

        try {
            Log.d(TAG, "Llamando a nativeShutdown()...")
            nativeShutdown()
            Log.d(TAG, "Recursos OpenXR liberados")
        } catch (e: Exception) {
            Log.e(TAG, "Error durante shutdown: ${e.message}", e)
        }

        Log.d(TAG, "Actividad destruida")
    }

    override fun onBackPressed() {
        Log.d(TAG, "onBackPressed() - Cerrando aplicación")
        finish()
    }
}