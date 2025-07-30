package com.example.holamundo2

import android.app.Activity
import android.content.Context
import android.opengl.GLES30
import android.opengl.Matrix
import android.os.Bundle
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.egl.EGLContext
import javax.microedition.khronos.egl.EGLDisplay

class MainActivity : Activity() {

    companion object {
        const val TAG = "OpenXRHolaMundo"

        // CORREGIDO: Cargar solo la librería nativa (no openxr_loader por separado)
        init {
            try {
                System.loadLibrary("holamundo_native")
                Log.d(TAG, "Librería nativa cargada correctamente")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Error cargando librería nativa: ${e.message}")
            }
        }
    }

    // Declaraciones de funciones nativas
    private external fun nativeInitialize(): Boolean
    private external fun nativeCreateSession(): Boolean
    private external fun nativeRunFrame(): Boolean
    private external fun nativeShutdown()

    private var isRunning = false
    private var renderThread: Thread? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        Log.d(TAG, "Iniciando aplicación OpenXR HolaMundo")

        // MEJORADO: Inicialización con mejor manejo de errores
        try {
            // Inicializar OpenXR
            if (!nativeInitialize()) {
                Log.e(TAG, "Error al inicializar OpenXR")
                finish()
                return
            }

            // Crear sesión OpenXR
            if (!nativeCreateSession()) {
                Log.e(TAG, "Error al crear sesión OpenXR")
                nativeShutdown()
                finish()
                return
            }

            isRunning = true
            startRenderLoop()

        } catch (e: Exception) {
            Log.e(TAG, "Error durante la inicialización: ${e.message}")
            finish()
        }
    }

    private fun startRenderLoop() {
        renderThread = Thread {
            Log.d(TAG, "Iniciando loop de renderizado")
            while (isRunning) {
                try {
                    if (!nativeRunFrame()) {
                        Log.e(TAG, "Error en el frame de renderizado")
                        break
                    }

                    // MEJORADO: Control más preciso de FPS
                    Thread.sleep(11) // ~90 FPS para Quest
                } catch (e: InterruptedException) {
                    Log.d(TAG, "Loop de renderizado interrumpido")
                    break
                } catch (e: Exception) {
                    Log.e(TAG, "Error en loop de renderizado: ${e.message}")
                    break
                }
            }
            Log.d(TAG, "Loop de renderizado terminado")
        }
        renderThread?.start()
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.d(TAG, "Cerrando aplicación")

        isRunning = false

        // Esperar a que termine el thread de renderizado
        renderThread?.let { thread ->
            try {
                thread.join(1000) // Esperar máximo 1 segundo
            } catch (e: InterruptedException) {
                Log.w(TAG, "Timeout esperando thread de renderizado")
            }
        }

        // Limpiar recursos OpenXR
        try {
            nativeShutdown()
        } catch (e: Exception) {
            Log.e(TAG, "Error durante shutdown: ${e.message}")
        }

        Log.d(TAG, "Aplicación terminada")
    }

    override fun onPause() {
        super.onPause()
        // En una app VR real, aquí pausarías la sesión OpenXR
        Log.d(TAG, "Aplicación pausada")
    }

    override fun onResume() {
        super.onResume()
        // En una app VR real, aquí reanudarías la sesión OpenXR
        Log.d(TAG, "Aplicación reanudada")
    }
}

// OpenXRRenderer.kt - Clase para manejar el renderizado OpenGL
class OpenXRRenderer {

    private var shaderProgram = 0
    private var vertexBuffer: FloatBuffer? = null
    private var projectionMatrix = FloatArray(16)
    private var viewMatrix = FloatArray(16)
    private var modelMatrix = FloatArray(16)
    private var mvpMatrix = FloatArray(16)

    // Vértices para un quad simple donde mostraremos el texto
    private val quadVertices = floatArrayOf(
        // Posiciones      // Coordenadas de textura
        -1.0f, -0.5f, -2.0f,  0.0f, 0.0f,
        1.0f, -0.5f, -2.0f,  1.0f, 0.0f,
        -1.0f,  0.5f, -2.0f,  0.0f, 1.0f,
        1.0f,  0.5f, -2.0f,  1.0f, 1.0f
    )

    fun initialize(): Boolean {
        return try {
            // Configurar vértices
            val bb = ByteBuffer.allocateDirect(quadVertices.size * 4)
            bb.order(ByteOrder.nativeOrder())
            vertexBuffer = bb.asFloatBuffer()
            vertexBuffer?.put(quadVertices)
            vertexBuffer?.position(0)

            // Crear shaders
            val vertexShader = loadShader(GLES30.GL_VERTEX_SHADER, vertexShaderCode)
            val fragmentShader = loadShader(GLES30.GL_FRAGMENT_SHADER, fragmentShaderCode)

            if (vertexShader == 0 || fragmentShader == 0) {
                Log.e("OpenXRRenderer", "Error creando shaders")
                return false
            }

            shaderProgram = GLES30.glCreateProgram()
            GLES30.glAttachShader(shaderProgram, vertexShader)
            GLES30.glAttachShader(shaderProgram, fragmentShader)
            GLES30.glLinkProgram(shaderProgram)

            // Verificar que el programa se enlazó correctamente
            val linkStatus = IntArray(1)
            GLES30.glGetProgramiv(shaderProgram, GLES30.GL_LINK_STATUS, linkStatus, 0)
            if (linkStatus[0] == GLES30.GL_FALSE) {
                Log.e("OpenXRRenderer", "Error enlazando programa: ${GLES30.glGetProgramInfoLog(shaderProgram)}")
                return false
            }

            // Inicializar matrices
            Matrix.setIdentityM(modelMatrix, 0)
            true
        } catch (e: Exception) {
            Log.e("OpenXRRenderer", "Error en initialize: ${e.message}")
            false
        }
    }

    fun render(eyeProjection: FloatArray, eyeView: FloatArray): Boolean {
        return try {
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT or GLES30.GL_DEPTH_BUFFER_BIT)
            GLES30.glUseProgram(shaderProgram)

            // Calcular matriz MVP
            Matrix.multiplyMM(viewMatrix, 0, eyeView, 0, modelMatrix, 0)
            Matrix.multiplyMM(mvpMatrix, 0, eyeProjection, 0, viewMatrix, 0)

            // Obtener handles de los uniforms
            val mvpMatrixHandle = GLES30.glGetUniformLocation(shaderProgram, "uMVPMatrix")
            val positionHandle = GLES30.glGetAttribLocation(shaderProgram, "aPosition")

            if (mvpMatrixHandle == -1 || positionHandle == -1) {
                Log.e("OpenXRRenderer", "Error obteniendo handles de shader")
                return false
            }

            // Pasar la matriz MVP al shader
            GLES30.glUniformMatrix4fv(mvpMatrixHandle, 1, false, mvpMatrix, 0)

            // Configurar atributos de vértices
            GLES30.glEnableVertexAttribArray(positionHandle)
            GLES30.glVertexAttribPointer(positionHandle, 3, GLES30.GL_FLOAT, false, 5 * 4, vertexBuffer)

            // Dibujar el quad
            GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)

            GLES30.glDisableVertexAttribArray(positionHandle)

            // Verificar errores OpenGL
            val error = GLES30.glGetError()
            if (error != GLES30.GL_NO_ERROR) {
                Log.e("OpenXRRenderer", "Error OpenGL durante renderizado: $error")
                return false
            }

            true
        } catch (e: Exception) {
            Log.e("OpenXRRenderer", "Error en render: ${e.message}")
            false
        }
    }

    private fun loadShader(type: Int, shaderCode: String): Int {
        val shader = GLES30.glCreateShader(type)
        GLES30.glShaderSource(shader, shaderCode)
        GLES30.glCompileShader(shader)

        // Verificar errores de compilación
        val compileStatus = IntArray(1)
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, compileStatus, 0)
        if (compileStatus[0] == GLES30.GL_FALSE) {
            Log.e("OpenXRRenderer", "Error compilando shader: ${GLES30.glGetShaderInfoLog(shader)}")
            GLES30.glDeleteShader(shader)
            return 0
        }

        return shader
    }

    companion object {
        private const val vertexShaderCode = """
            #version 300 es
            layout(location = 0) in vec4 aPosition;
            uniform mat4 uMVPMatrix;
            void main() {
                gl_Position = uMVPMatrix * aPosition;
            }
        """

        private const val fragmentShaderCode = """
            #version 300 es
            precision mediump float;
            out vec4 fragColor;
            void main() {
                // Color verde para simular texto "HolaMundo"
                fragColor = vec4(0.0, 1.0, 0.0, 1.0);
            }
        """
    }
}