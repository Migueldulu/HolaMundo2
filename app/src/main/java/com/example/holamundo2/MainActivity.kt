package com.example.holamundo2

import android.app.Activity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.util.Log
import android.view.WindowManager
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
    private var openxrInitialized = false

    private val activityScope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        Log.d(TAG, "=== Iniciando aplicación OpenXR HolaMundo ===")

        try {
            setupVRWindow()
            setupGLSurfaceView()
        } catch (e: Exception) {
            Log.e(TAG, "Error durante onCreate: ${e.message}", e)
            finish()
        }
    }

    private fun setupVRWindow() {
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
    }

    private fun setupGLSurfaceView() {
        glSurfaceView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3)
            setEGLConfigChooser(8, 8, 8, 8, 24, 8)
            setRenderer(object : GLSurfaceView.Renderer {
                override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                    Log.d(TAG, "OpenGL Surface creada")
                }

                override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                    Log.d(TAG, "OpenGL Surface cambiada: ${width}x${height}")

                    // NUEVO: Inicializar OpenXR aquí, cuando el contexto OpenGL está listo
                    if (!openxrInitialized) {
                        Log.d(TAG, "Contexto OpenGL listo, inicializando OpenXR...")
                        initializeOpenXRSync()
                    }
                }

                override fun onDrawFrame(gl: GL10?) {
                    // Solo ejecutar frames si OpenXR está inicializado
                    if (openxrInitialized && isRunning) {
                        nativeRunFrame()
                    }
                }
            })
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        setContentView(glSurfaceView)
    }

    // NUEVO: Inicialización síncrona de OpenXR en el thread OpenGL
    private fun initializeOpenXRSync() {
        try {
            Log.d(TAG, "Inicializando OpenXR en thread OpenGL...")

            if (!nativeInitialize()) {
                Log.e(TAG, "Error al inicializar OpenXR")
                runOnUiThread { finish() }
                return
            }

            Log.d(TAG, "Creando sesión OpenXR...")
            if (!nativeCreateSession()) {
                Log.e(TAG, "Error al crear sesión OpenXR")
                nativeShutdown()
                runOnUiThread { finish() }
                return
            }

            Log.d(TAG, "OpenXR inicializado correctamente")
            openxrInitialized = true
            isRunning = true

        } catch (e: Exception) {
            Log.e(TAG, "Error durante inicialización OpenXR: ${e.message}", e)
            runOnUiThread { finish() }
        }
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
        openxrInitialized = false
        activityScope.cancel()

        try {
            nativeShutdown()
            Log.d(TAG, "Recursos OpenXR liberados")
        } catch (e: Exception) {
            Log.e(TAG, "Error durante shutdown: ${e.message}", e)
        }

        Log.d(TAG, "=== Aplicación terminada ===")
    }

    override fun onBackPressed() {
        Log.d(TAG, "Back pressed - cerrando aplicación")
        finish()
    }
}