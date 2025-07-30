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

        // Cargar la librería nativa de OpenXR
        init {
            System.loadLibrary("openxr_loader")
            System.loadLibrary("holamundo_native")
        }
    }

    private external fun nativeInitialize(): Boolean
    private external fun nativeCreateSession(): Boolean
    private external fun nativeRunFrame(): Boolean
    private external fun nativeShutdown()



    private var isRunning = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        Log.d(TAG, "Iniciando aplicación OpenXR HolaMundo")

        // Inicializar OpenXR
        if (!nativeInitialize()) {
            Log.e(TAG, "Error al inicializar OpenXR")
            finish()
            return
        }

        // Crear sesión OpenXR
        if (!nativeCreateSession()) {
            Log.e(TAG, "Error al crear sesión OpenXR")
            finish()
            return
        }

        isRunning = true
        startRenderLoop()
    }

    private fun startRenderLoop() {
        Thread {
            while (isRunning) {
                if (!nativeRunFrame()) {
                    Log.e(TAG, "Error en el frame de renderizado")
                    break
                }

                // Controlar FPS (90 FPS para Quest 2)
                try {
                    Thread.sleep(11) // ~90 FPS
                } catch (e: InterruptedException) {
                    break
                }
            }
        }.start()
    }

    override fun onDestroy() {
        super.onDestroy()
        isRunning = false
        nativeShutdown()
        Log.d(TAG, "Aplicación terminada")
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

    fun initialize() {
        // Configurar vértices
        val bb = ByteBuffer.allocateDirect(quadVertices.size * 4)
        bb.order(ByteOrder.nativeOrder())
        vertexBuffer = bb.asFloatBuffer()
        vertexBuffer?.put(quadVertices)
        vertexBuffer?.position(0)

        // Crear shaders
        val vertexShader = loadShader(GLES30.GL_VERTEX_SHADER, vertexShaderCode)
        val fragmentShader = loadShader(GLES30.GL_FRAGMENT_SHADER, fragmentShaderCode)

        shaderProgram = GLES30.glCreateProgram()
        GLES30.glAttachShader(shaderProgram, vertexShader)
        GLES30.glAttachShader(shaderProgram, fragmentShader)
        GLES30.glLinkProgram(shaderProgram)

        // Inicializar matrices
        Matrix.setIdentityM(modelMatrix, 0)
    }

    fun render(eyeProjection: FloatArray, eyeView: FloatArray) {
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT or GLES30.GL_DEPTH_BUFFER_BIT)
        GLES30.glUseProgram(shaderProgram)

        // Calcular matriz MVP
        Matrix.multiplyMM(viewMatrix, 0, eyeView, 0, modelMatrix, 0)
        Matrix.multiplyMM(mvpMatrix, 0, eyeProjection, 0, viewMatrix, 0)

        // Obtener handles de los uniforms
        val mvpMatrixHandle = GLES30.glGetUniformLocation(shaderProgram, "uMVPMatrix")
        val positionHandle = GLES30.glGetAttribLocation(shaderProgram, "aPosition")

        // Pasar la matriz MVP al shader
        GLES30.glUniformMatrix4fv(mvpMatrixHandle, 1, false, mvpMatrix, 0)

        // Configurar atributos de vértices
        GLES30.glEnableVertexAttribArray(positionHandle)
        GLES30.glVertexAttribPointer(positionHandle, 3, GLES30.GL_FLOAT, false, 5 * 4, vertexBuffer)

        // Dibujar el quad
        GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)

        GLES30.glDisableVertexAttribArray(positionHandle)
    }

    private fun loadShader(type: Int, shaderCode: String): Int {
        val shader = GLES30.glCreateShader(type)
        GLES30.glShaderSource(shader, shaderCode)
        GLES30.glCompileShader(shader)
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