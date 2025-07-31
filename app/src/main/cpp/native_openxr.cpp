#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vector>
#include <string>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>

#define LOG_TAG "OpenXRHolaMundo"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// Estructura para manejar el estado de OpenXR de forma más organizada
struct OpenXRState {
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;

    // Variables específicas para Meta Quest
    JavaVM* javaVm = nullptr;
    jobject activityObject = nullptr;
    ANativeWindow* nativeWindow = nullptr;

    // Miembros EGL modificados - SIN superficie de ventana
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLContext eglContext = EGL_NO_CONTEXT;
    EGLConfig eglConfig = nullptr;

    bool isInitialized = false;
    bool isSessionCreated = false;
    bool sessionRunning = false;
    bool loaderInitialized = false;

    std::mutex stateMutex;

    void reset() {
        std::lock_guard<std::mutex> lock(stateMutex);

        // Limpiar recursos EGL - SIN superficie
        if (eglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(eglDisplay, eglContext);
            eglContext = EGL_NO_CONTEXT;
        }
        if (eglDisplay != EGL_NO_DISPLAY) {
            eglTerminate(eglDisplay);
            eglDisplay = EGL_NO_DISPLAY;
        }
        eglConfig = nullptr;

        // Limpiar recursos OpenXR
        instance = XR_NULL_HANDLE;
        session = XR_NULL_HANDLE;
        appSpace = XR_NULL_HANDLE;
        systemId = XR_NULL_SYSTEM_ID;
        sessionState = XR_SESSION_STATE_UNKNOWN;
        javaVm = nullptr;
        activityObject = nullptr;
        nativeWindow = nullptr;
        isInitialized = false;
        isSessionCreated = false;
        sessionRunning = false;
    }
};

// Información de swapchain mejorada
struct SwapchainInfo {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;

    void cleanup() {
        if (swapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(swapchain);
            swapchain = XR_NULL_HANDLE;
        }
        images.clear();
        width = height = 0;
    }
};

// Variables globales
static OpenXRState g_openxrState;
static std::vector<SwapchainInfo> g_swapchains;
static XrViewConfigurationView g_viewConfigs[2];
static GLuint g_shaderProgram = 0;
static GLuint g_VAO = 0;
static GLuint g_VBO = 0;
static bool g_shadersInitialized = false;

// Función mejorada para verificar resultados
bool CheckXrResult(XrResult result, const char* operation) {
    if (XR_FAILED(result)) {
        LOGE("OpenXR Error: %s failed with result %d (0x%08X)", operation, result, result);

        // Proporcionar información más detallada sobre errores comunes
        switch (result) {
            case XR_ERROR_INSTANCE_LOST:
                LOGE("  -> Instance lost - needs recreation");
                break;
            case XR_ERROR_SESSION_LOST:
                LOGE("  -> Session lost - needs recreation");
                break;
            case XR_ERROR_RUNTIME_FAILURE:
                LOGE("  -> Runtime failure - check Oculus service");
                break;
            case XR_ERROR_SYSTEM_INVALID:
                LOGE("  -> System invalid - HMD not found");
                break;
            case XR_ERROR_GRAPHICS_DEVICE_INVALID:
                LOGE("  -> Graphics device invalid - check OpenGL context");
                break;
            default:
                break;
        }
        return false;
    }
    return true;
}

// Función para inicializar el loader OpenXR (versión simplificada sin loader negotiation)
bool initializeOpenXRLoader(JNIEnv* env, jobject activityObject) {
    LOGI("Verificando disponibilidad de OpenXR...");

    if (g_openxrState.loaderInitialized) {
        LOGI("OpenXR ya está disponible");
        return true;
    }

    PFN_xrInitializeLoaderKHR initializeLoaderKHR = nullptr;
    if (xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&initializeLoaderKHR) != XR_SUCCESS || !initializeLoaderKHR) {
        LOGE("No se pudo obtener el puntero a xrInitializeLoaderKHR");
        return false;
    }

    XrLoaderInitInfoAndroidKHR loaderInitInfo = {};
    loaderInitInfo.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
    loaderInitInfo.next = nullptr;
    loaderInitInfo.applicationVM = nullptr;
    loaderInitInfo.applicationContext = nullptr;

    // Obtener VM y context desde el entorno JNI
    if (env->GetJavaVM(&g_openxrState.javaVm) != JNI_OK) {
        LOGE("No se pudo obtener JavaVM desde JNIEnv");
        return false;
    }
    g_openxrState.activityObject = env->NewGlobalRef(activityObject);
    loaderInitInfo.applicationVM = g_openxrState.javaVm;
    loaderInitInfo.applicationContext = g_openxrState.activityObject;

    XrResult result = initializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&loaderInitInfo);
    if (XR_FAILED(result)) {
        LOGE("xrInitializeLoaderKHR falló: %d", result);
        return false;
    }

    g_openxrState.loaderInitialized = true;
    LOGI("✓ OpenXR Loader inicializado correctamente");
    return true;
}

// Función para verificar extensiones con mejor logging
bool verifyRequiredExtensions() {
    LOGI("Verificando extensiones OpenXR disponibles...");

    uint32_t extensionCount = 0;
    if (!CheckXrResult(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr),
                       "xrEnumerateInstanceExtensionProperties (count)")) {
        return false;
    }

    LOGI("Encontradas %d extensiones disponibles", extensionCount);

    std::vector<XrExtensionProperties> availableExtensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
    if (!CheckXrResult(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, availableExtensions.data()),
                       "xrEnumerateInstanceExtensionProperties (data)")) {
        return false;
    }

    // Verificar extensiones requeridas
    bool androidExtensionAvailable = false;
    bool openglExtensionAvailable = false;

    for (const auto& ext : availableExtensions) {
        if (strcmp(ext.extensionName, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) == 0) {
            androidExtensionAvailable = true;
            LOGI("✓ Extensión Android CREATE_INSTANCE encontrada");
        }
        if (strcmp(ext.extensionName, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME) == 0) {
            openglExtensionAvailable = true;
            LOGI("✓ Extensión OpenGL ES encontrada");
        }
    }

    if (!androidExtensionAvailable) {
        LOGE("✗ Extensión Android CREATE_INSTANCE no disponible");
        return false;
    }

    if (!openglExtensionAvailable) {
        LOGE("✗ Extensión OpenGL ES no disponible");
        return false;
    }

    LOGI("✓ Todas las extensiones requeridas están disponibles");
    return true;
}

// Función auxiliar para compilar shaders
bool compileShader(GLuint shader, const char* source) {
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        LOGE("Error compilando shader: %s", infoLog);
        return false;
    }
    return true;
}

// Función para inicializar shaders una sola vez
bool initializeShaders() {
    if (g_shadersInitialized) {
        return true;
    }

    LOGI("Inicializando shaders...");

    const char* vertexShaderSource =
            "#version 300 es\n"
            "in vec3 aPosition;\n"
            "void main() {\n"
            "    gl_Position = vec4(aPosition, 1.0);\n"
            "}\n";

    const char* fragmentShaderSource =
            "#version 300 es\n"
            "precision mediump float;\n"
            "out vec4 fragColor;\n"
            "void main() {\n"
            "    fragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
            "}\n";

    // Compilar vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    if (!compileShader(vertexShader, vertexShaderSource)) {
        LOGE("Error compilando vertex shader");
        return false;
    }

    // Compilar fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    if (!compileShader(fragmentShader, fragmentShaderSource)) {
        LOGE("Error compilando fragment shader");
        glDeleteShader(vertexShader);
        return false;
    }

    // Crear programa de shader
    g_shaderProgram = glCreateProgram();
    glAttachShader(g_shaderProgram, vertexShader);
    glAttachShader(g_shaderProgram, fragmentShader);
    glLinkProgram(g_shaderProgram);

    // Verificar linking
    GLint success;
    glGetProgramiv(g_shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(g_shaderProgram, 512, nullptr, infoLog);
        LOGE("Error linking shader program: %s", infoLog);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(g_shaderProgram);
        return false;
    }

    // Limpiar shaders individuales
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Crear geometría del rectángulo
    float vertices[] = {
            // Rectángulo simple centrado
            -0.5f, -0.3f, 0.0f,  // Inferior izquierdo
            0.5f, -0.3f, 0.0f,  // Inferior derecho
            -0.5f,  0.3f, 0.0f,  // Superior izquierdo

            0.5f, -0.3f, 0.0f,  // Inferior derecho
            0.5f,  0.3f, 0.0f,  // Superior derecho
            -0.5f,  0.3f, 0.0f   // Superior izquierdo
    };

    glGenVertexArrays(1, &g_VAO);
    glGenBuffers(1, &g_VBO);

    glBindVertexArray(g_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    GLint positionAttrib = glGetAttribLocation(g_shaderProgram, "aPosition");
    if (positionAttrib == -1) {
        LOGE("No se pudo encontrar atributo aPosition");
        return false;
    }

    glVertexAttribPointer(positionAttrib, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(positionAttrib);

    glBindVertexArray(0);

    g_shadersInitialized = true;
    LOGI("✓ Shaders inicializados correctamente");
    return true;
}

// Función para limpiar recursos de forma segura
void cleanupSwapchains() {
    LOGI("Limpiando swapchains...");
    for (auto& swapchain : g_swapchains) {
        swapchain.cleanup();
    }
    g_swapchains.clear();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_holamundo2_MainActivity_nativeInitialize(JNIEnv *env, jobject thiz) {
    LOGI("=== Inicializando OpenXR ===");

    // Reset del estado
    g_openxrState.reset();
    cleanupSwapchains();

    try {
        // 1. Inicializar Loader OpenXR
        LOGI("=== VERIFICANDO DISPONIBILIDAD DE OPENXR ===");
        if (!initializeOpenXRLoader(env, thiz)) {
            LOGE("FALLO: OpenXR no está disponible");
            return JNI_FALSE;
        }

        // 2. Verificar runtime
        LOGI("=== VERIFICANDO RUNTIME OPENXR ===");
        uint32_t testCount = 0;
        XrResult testResult = xrEnumerateApiLayerProperties(0, &testCount, nullptr);
        if (testResult != XR_SUCCESS && testResult != XR_ERROR_SIZE_INSUFFICIENT) {
            LOGE("FALLO: Runtime OpenXR no responde (resultado: %d)", testResult);
            return JNI_FALSE;
        }
        LOGI("✓ Runtime OpenXR responde correctamente");

        // 3. Verificar extensiones
        LOGI("=== VERIFICANDO EXTENSIONES ===");
        if (!verifyRequiredExtensions()) {
            LOGE("FALLO: Extensiones requeridas no están disponibles");
            return JNI_FALSE;
        }
        LOGI("✓ Extensiones verificadas correctamente");

        // 4. Configurar extensiones
        std::vector<const char*> extensions = {
                XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME
        };

        // 5. Crear instancia
        XrInstanceCreateInfoAndroidKHR androidCreateInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
        androidCreateInfo.applicationVM = g_openxrState.javaVm;
        androidCreateInfo.applicationActivity = g_openxrState.activityObject;

        XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
        strncpy(instanceInfo.applicationInfo.applicationName, "HolaMundo VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
        instanceInfo.applicationInfo.applicationName[XR_MAX_APPLICATION_NAME_SIZE - 1] = '\0';
        strncpy(instanceInfo.applicationInfo.engineName, "Custom Engine", XR_MAX_ENGINE_NAME_SIZE - 1);
        instanceInfo.applicationInfo.engineName[XR_MAX_ENGINE_NAME_SIZE - 1] = '\0';
        instanceInfo.applicationInfo.applicationVersion = 1;
        instanceInfo.applicationInfo.engineVersion = 1;
        instanceInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instanceInfo.enabledExtensionNames = extensions.data();
        instanceInfo.next = &androidCreateInfo;

        LOGI("Creando instancia OpenXR...");
        if (!CheckXrResult(xrCreateInstance(&instanceInfo, &g_openxrState.instance), "xrCreateInstance")) {
            return JNI_FALSE;
        }
        LOGI("✓ Instancia OpenXR creada correctamente");

        // 6. Obtener propiedades del runtime
        XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
        if (CheckXrResult(xrGetInstanceProperties(g_openxrState.instance, &instanceProperties), "xrGetInstanceProperties")) {
            LOGI("Runtime: %s v%d.%d.%d",
                 instanceProperties.runtimeName,
                 XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
                 XR_VERSION_MINOR(instanceProperties.runtimeVersion),
                 XR_VERSION_PATCH(instanceProperties.runtimeVersion));
        }

        // 7. Obtener sistema HMD
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

        LOGI("Obteniendo sistema HMD...");
        if (!CheckXrResult(xrGetSystem(g_openxrState.instance, &systemInfo, &g_openxrState.systemId), "xrGetSystem")) {
            LOGE("No se pudo encontrar un HMD compatible");
            return JNI_FALSE;
        }
        LOGI("✓ Sistema HMD encontrado (ID: %llu)", (unsigned long long)g_openxrState.systemId);

        // 8. Verificar configuración de vista
        uint32_t viewCount = 0;
        XrViewConfigurationView viewConfigs[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};

        if (!CheckXrResult(xrEnumerateViewConfigurationViews(g_openxrState.instance, g_openxrState.systemId,
                                                             XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, viewConfigs),
                           "xrEnumerateViewConfigurationViews")) {
            return JNI_FALSE;
        }

        if (viewCount != 2) {
            LOGE("Se esperaban 2 vistas, pero se encontraron %d", viewCount);
            return JNI_FALSE;
        }

        // Guardar configuraciones de vista
        memcpy(g_viewConfigs, viewConfigs, sizeof(g_viewConfigs));

        LOGI("✓ Configuración de vista estéreo verificada:");
        for (int i = 0; i < 2; i++) {
            LOGI("  Ojo %d: %dx%d (recomendado), %dx%d (máximo)",
                 i,
                 g_viewConfigs[i].recommendedImageRectWidth, g_viewConfigs[i].recommendedImageRectHeight,
                 g_viewConfigs[i].maxImageRectWidth, g_viewConfigs[i].maxImageRectHeight);
        }

        g_openxrState.isInitialized = true;
        LOGI("=== OpenXR inicializado correctamente ===");
        return JNI_TRUE;

    } catch (const std::exception& e) {
        LOGE("Excepción durante inicialización: %s", e.what());
        return JNI_FALSE;
    } catch (...) {
        LOGE("Excepción desconocida durante inicialización");
        return JNI_FALSE;
    }
}

// FUNCIÓN COMPLETAMENTE REESCRITA - Usando el contexto existente de GLSurfaceView
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_holamundo2_MainActivity_nativeSetupEGL(JNIEnv *env, jobject thiz, jobject surface) {
    LOGI("=== Configurando EGL para OpenXR (usando contexto existente) ===");

    // 1. Obtener el contexto EGL actual que ya está configurado por GLSurfaceView
    EGLDisplay currentDisplay = eglGetCurrentDisplay();
    EGLContext currentContext = eglGetCurrentContext();
    EGLSurface currentSurface = eglGetCurrentSurface(EGL_DRAW);

    if (currentDisplay == EGL_NO_DISPLAY) {
        LOGE("No hay display EGL actual - GLSurfaceView no está configurado");
        return JNI_FALSE;
    }

    if (currentContext == EGL_NO_CONTEXT) {
        LOGE("No hay contexto EGL actual - GLSurfaceView no está configurado");
        return JNI_FALSE;
    }

    LOGI("Usando contexto EGL existente de GLSurfaceView:");
    LOGI("  Display: %p", currentDisplay);
    LOGI("  Context: %p", currentContext);
    LOGI("  Surface: %p", currentSurface);

    // 2. Obtener la configuración EGL del contexto actual
    EGLint configId;
    if (!eglQueryContext(currentDisplay, currentContext, EGL_CONFIG_ID, &configId)) {
        LOGE("Failed to query EGL config ID. Error: 0x%X", eglGetError());
        return JNI_FALSE;
    }

    // Obtener la configuración usando el ID
    const EGLint configAttribs[] = {
            EGL_CONFIG_ID, configId,
            EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(currentDisplay, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        LOGE("Failed to get EGL config by ID. Error: 0x%X", eglGetError());
        return JNI_FALSE;
    }

    // 3. Verificar configuración
    EGLint configRedSize, configGreenSize, configBlueSize, configAlphaSize, configDepthSize;
    eglGetConfigAttrib(currentDisplay, config, EGL_RED_SIZE, &configRedSize);
    eglGetConfigAttrib(currentDisplay, config, EGL_GREEN_SIZE, &configGreenSize);
    eglGetConfigAttrib(currentDisplay, config, EGL_BLUE_SIZE, &configBlueSize);
    eglGetConfigAttrib(currentDisplay, config, EGL_ALPHA_SIZE, &configAlphaSize);
    eglGetConfigAttrib(currentDisplay, config, EGL_DEPTH_SIZE, &configDepthSize);

    LOGI("Configuración EGL actual:");
    LOGI("  R:%d G:%d B:%d A:%d Depth:%d",
         configRedSize, configGreenSize, configBlueSize, configAlphaSize, configDepthSize);

    // 4. Verificar extensiones EGL
    const char* eglExtensions = eglQueryString(currentDisplay, EGL_EXTENSIONS);
    if (eglExtensions && strstr(eglExtensions, "EGL_KHR_surfaceless_context")) {
        LOGI("✓ EGL_KHR_surfaceless_context disponible");
    } else {
        LOGI("✗ EGL_KHR_surfaceless_context NO disponible");
    }

    // 5. Verificar OpenGL ES en el contexto actual
    const char* glVersion = (const char*)glGetString(GL_VERSION);
    const char* glRenderer = (const char*)glGetString(GL_RENDERER);
    const char* glExtensions = (const char*)glGetString(GL_EXTENSIONS);

    LOGI("OpenGL ES Version: %s", glVersion ? glVersion : "unknown");
    LOGI("OpenGL ES Renderer: %s", glRenderer ? glRenderer : "unknown");

    if (!glExtensions || strstr(glExtensions, "GL_OES_EGL_image") == nullptr) {
        LOGE("Required GL_OES_EGL_image extension not found");
        return JNI_FALSE;
    }
    LOGI("✓ GL_OES_EGL_image extension found");

    // 6. Almacenar referencias del contexto existente
    g_openxrState.eglDisplay = currentDisplay;
    g_openxrState.eglContext = currentContext;
    g_openxrState.eglConfig = config;
    // NO almacenamos eglSurface - OpenXR maneja sus propias superficies

    // 7. Obtener ANativeWindow para referencia (pero no crear superficie)
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window) {
        int32_t width = ANativeWindow_getWidth(window);
        int32_t height = ANativeWindow_getHeight(window);
        LOGI("Window dimensions: %dx%d (referencia)", width, height);
        g_openxrState.nativeWindow = window;
        ANativeWindow_release(window); // Liberar inmediatamente
    }

    LOGI("✓ Configuración EGL completada (usando contexto GLSurfaceView):");
    LOGI("  Display: %p", g_openxrState.eglDisplay);
    LOGI("  Config: %p", g_openxrState.eglConfig);
    LOGI("  Context: %p (del GLSurfaceView)", g_openxrState.eglContext);
    LOGI("  Surface: GLSurfaceView maneja la superficie principal");

    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_holamundo2_MainActivity_nativeCreateSession(JNIEnv *env, jobject thiz) {
    LOGI("=== Creando sesión OpenXR (estilo Meta) ===");

    if (!g_openxrState.isInitialized) {
        LOGE("OpenXR no está inicializado");
        return JNI_FALSE;
    }

    try {
        // PASO CRÍTICO 1: Obtener requerimientos gráficos ANTES de crear sesión
        LOGI("Paso 1: Obteniendo requerimientos gráficos OpenXR...");

        PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetGraphicsRequirements = nullptr;
        XrResult result = xrGetInstanceProcAddr(g_openxrState.instance,
                                                "xrGetOpenGLESGraphicsRequirementsKHR",
                                                (PFN_xrVoidFunction*)&pfnGetGraphicsRequirements);

        if (XR_FAILED(result) || !pfnGetGraphicsRequirements) {
            LOGE("No se pudo obtener xrGetOpenGLESGraphicsRequirementsKHR: %d", result);
            return JNI_FALSE;
        }

        XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        result = pfnGetGraphicsRequirements(g_openxrState.instance, g_openxrState.systemId, &graphicsRequirements);
        if (XR_FAILED(result)) {
            LOGE("xrGetOpenGLESGraphicsRequirementsKHR falló: %d", result);
            return JNI_FALSE;
        }

        LOGI("✓ Requerimientos gráficos obtenidos:");
        LOGI("  Min API: %llu.%llu.%llu",
             XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported),
             XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported),
             XR_VERSION_PATCH(graphicsRequirements.minApiVersionSupported));

        // PASO 2: Obtener contexto EGL actual del GLSurfaceView
        LOGI("Paso 2: Obteniendo contexto EGL actual...");

        EGLDisplay currentDisplay = eglGetCurrentDisplay();
        EGLContext currentContext = eglGetCurrentContext();

        if (currentDisplay == EGL_NO_DISPLAY || currentContext == EGL_NO_CONTEXT) {
            LOGE("No hay contexto EGL actual válido");
            return JNI_FALSE;
        }

        // Obtener configuración del contexto actual
        EGLint configId;
        if (!eglQueryContext(currentDisplay, currentContext, EGL_CONFIG_ID, &configId)) {
            LOGE("No se pudo obtener config ID: 0x%X", eglGetError());
            return JNI_FALSE;
        }

        const EGLint configAttribs[] = { EGL_CONFIG_ID, configId, EGL_NONE };
        EGLConfig config;
        EGLint numConfigs;
        if (!eglChooseConfig(currentDisplay, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
            LOGE("No se pudo obtener configuración EGL: 0x%X", eglGetError());
            return JNI_FALSE;
        }

        LOGI("✓ Contexto EGL obtenido:");
        LOGI("  Display: %p", currentDisplay);
        LOGI("  Context: %p", currentContext);
        LOGI("  Config: %p", config);

        // PASO 3: Configurar binding de gráficos
        LOGI("Paso 3: Configurando binding OpenGL ES...");

        XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
        graphicsBinding.display = currentDisplay;
        graphicsBinding.config = config;
        graphicsBinding.context = currentContext;

        // PASO 4: Crear sesión OpenXR
        LOGI("Paso 4: Creando sesión OpenXR...");

        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
        sessionInfo.next = &graphicsBinding;
        sessionInfo.systemId = g_openxrState.systemId;

        result = xrCreateSession(g_openxrState.instance, &sessionInfo, &g_openxrState.session);

        if (XR_FAILED(result)) {
            LOGE("xrCreateSession falló: %d (0x%08X)", result, result);

            switch(result) {
                case XR_ERROR_GRAPHICS_DEVICE_INVALID:
                    LOGE("  -> XR_ERROR_GRAPHICS_DEVICE_INVALID");
                    LOGE("      El contexto OpenGL ES no es válido para OpenXR");
                    break;
                case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING:
                    LOGE("  -> XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING");
                    LOGE("      No se llamó a xrGetOpenGLESGraphicsRequirementsKHR");
                    break;
                default:
                    LOGE("  -> Error desconocido: %d", result);
                    break;
            }
            return JNI_FALSE;
        }

        LOGI("✓ ¡Sesión OpenXR creada exitosamente!");

        // PASO 5: Crear espacio de referencia
        LOGI("Paso 5: Creando espacio de referencia...");

        XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        spaceInfo.poseInReferenceSpace = {{0, 0, 0, 1}, {0, 0, 0}};

        if (!CheckXrResult(xrCreateReferenceSpace(g_openxrState.session, &spaceInfo, &g_openxrState.appSpace),
                           "xrCreateReferenceSpace")) {
            return JNI_FALSE;
        }
        // PASO 6: Crear swapchains para renderizado
        LOGI("Paso 6: Creando swapchains para renderizado...");
        // Verificar formatos de swapchain soportados
        uint32_t formatCount = 0;
        if (!CheckXrResult(xrEnumerateSwapchainFormats(g_openxrState.session, 0, &formatCount, nullptr),
                           "xrEnumerateSwapchainFormats (count)")) {
            return JNI_FALSE;
        }

        std::vector<int64_t> formats(formatCount);
        if (!CheckXrResult(xrEnumerateSwapchainFormats(g_openxrState.session, formatCount, &formatCount, formats.data()),
                           "xrEnumerateSwapchainFormats (data)")) {
            return JNI_FALSE;
        }

        LOGI("Formatos de swapchain soportados (%d):", formatCount);
        for (auto format : formats) {
            LOGD("  - Format: 0x%08llX", (long long)format);
        }

        // Buscar formato apropiado
        int64_t selectedFormat = 0;
        for (auto format : formats) {
            if (format == GL_RGBA8) {
                selectedFormat = format;
                LOGI("✓ Usando formato GL_RGBA8");
                break;
            } else if (format == GL_RGB8 && selectedFormat == 0) {
                selectedFormat = format;
                LOGI("✓ Fallback a formato GL_RGB8");
            }
        }

        if (selectedFormat == 0 && !formats.empty()) {
            selectedFormat = formats[0];
            LOGI("✓ Usando primer formato disponible: 0x%08llX", (long long)selectedFormat);
        }

        if (selectedFormat == 0) {
            LOGE("No hay formatos de swapchain disponibles");
            return JNI_FALSE;
        }

        // Crear swapchains para cada ojo
        g_swapchains.resize(2);
        for (int eye = 0; eye < 2; eye++) {
            XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            swapchainInfo.width = g_viewConfigs[eye].recommendedImageRectWidth;
            swapchainInfo.height = g_viewConfigs[eye].recommendedImageRectHeight;
            swapchainInfo.format = selectedFormat;
            swapchainInfo.mipCount = 1;
            swapchainInfo.faceCount = 1;
            swapchainInfo.arraySize = 1;
            swapchainInfo.sampleCount = g_viewConfigs[eye].recommendedSwapchainSampleCount;
            swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

            LOGI("Creando swapchain para ojo %d (%dx%d, samples: %d)...",
                 eye, swapchainInfo.width, swapchainInfo.height, swapchainInfo.sampleCount);

            if (!CheckXrResult(xrCreateSwapchain(g_openxrState.session, &swapchainInfo, &g_swapchains[eye].swapchain),
                               "xrCreateSwapchain")) {
                return JNI_FALSE;
            }

            g_swapchains[eye].width = swapchainInfo.width;
            g_swapchains[eye].height = swapchainInfo.height;

            // Obtener imágenes del swapchain
            uint32_t imageCount;
            if (!CheckXrResult(xrEnumerateSwapchainImages(g_swapchains[eye].swapchain, 0, &imageCount, nullptr),
                               "xrEnumerateSwapchainImages (count)")) {
                return JNI_FALSE;
            }

            g_swapchains[eye].images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
            if (!CheckXrResult(xrEnumerateSwapchainImages(g_swapchains[eye].swapchain, imageCount, &imageCount,
                                                          reinterpret_cast<XrSwapchainImageBaseHeader*>(g_swapchains[eye].images.data())),
                               "xrEnumerateSwapchainImages (data)")) {
                return JNI_FALSE;
            }

            LOGI("✓ Swapchain %d creado: %dx%d, %d imágenes", eye,
                 g_swapchains[eye].width, g_swapchains[eye].height, imageCount);
        }


        LOGI("✓ Espacio de referencia creado");

        // Almacenar configuración
        g_openxrState.eglDisplay = currentDisplay;
        g_openxrState.eglContext = currentContext;
        g_openxrState.eglConfig = config;
        g_openxrState.isSessionCreated = true;

        LOGI("=== Sesión OpenXR creada correctamente ===");
        return JNI_TRUE;

    } catch (const std::exception& e) {
        LOGE("Excepción en createSession: %s", e.what());
        return JNI_FALSE;
    } catch (...) {
        LOGE("Excepción desconocida en createSession");
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_holamundo2_MainActivity_nativeRunFrame(JNIEnv *env, jobject thiz) {
    if (!g_openxrState.isSessionCreated || g_swapchains.empty()) {
        LOGD("RunFrame: No hay sesión creada o swapchains vacíos");
        return JNI_FALSE;
    }

    try {
        // Poll events (simplificado para logs)
        XrEventDataBuffer eventData{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult result = xrPollEvent(g_openxrState.instance, &eventData);

        while (result == XR_SUCCESS) {
            switch (eventData.type) {
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                    auto stateEvent = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
                    XrSessionState oldState = g_openxrState.sessionState;
                    g_openxrState.sessionState = stateEvent->state;

                    LOGI("Session state: %d -> %d", oldState, g_openxrState.sessionState);

                    switch (g_openxrState.sessionState) {
                        case XR_SESSION_STATE_READY: {
                            XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
                            beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                            if (!CheckXrResult(xrBeginSession(g_openxrState.session, &beginInfo), "xrBeginSession")) {
                                return JNI_FALSE;
                            }
                            g_openxrState.sessionRunning = true;
                            LOGI("✓ Sesión OpenXR iniciada y corriendo");
                            break;
                        }
                        case XR_SESSION_STATE_STOPPING: {
                            if (!CheckXrResult(xrEndSession(g_openxrState.session), "xrEndSession")) {
                                LOGE("Error terminando sesión, pero continuando...");
                            }
                            g_openxrState.sessionRunning = false;
                            LOGI("✓ Sesión OpenXR terminada");
                            break;
                        }
                        case XR_SESSION_STATE_EXITING:
                        case XR_SESSION_STATE_LOSS_PENDING:
                            LOGI("Sesión saliendo o perdida");
                            return JNI_FALSE;
                        default:
                            break;
                    }
                    break;
                }
                case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                    LOGE("Instancia OpenXR perdida");
                    return JNI_FALSE;
                case 40: // XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB
                    break;
                default:
                    LOGD("Evento OpenXR no manejado: %d", eventData.type);
                    break;
            }
            result = xrPollEvent(g_openxrState.instance, &eventData);
        }

        // Verificar estado de sesión
        if (g_openxrState.sessionState != XR_SESSION_STATE_SYNCHRONIZED &&
            g_openxrState.sessionState != XR_SESSION_STATE_VISIBLE &&
            g_openxrState.sessionState != XR_SESSION_STATE_FOCUSED) {
            LOGD("Estado de sesión no permite renderizado: %d", g_openxrState.sessionState);
            return JNI_TRUE;
        }

        LOGD("=== INICIO FRAME ===");

        // Wait frame
        XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        if (!CheckXrResult(xrWaitFrame(g_openxrState.session, &frameWaitInfo, &frameState), "xrWaitFrame")) {
            return JNI_FALSE;
        }
        LOGD("WaitFrame completado, shouldRender: %s", frameState.shouldRender ? "true" : "false");

        // Begin frame
        XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        if (!CheckXrResult(xrBeginFrame(g_openxrState.session, &frameBeginInfo), "xrBeginFrame")) {
            return JNI_FALSE;
        }
        LOGD("BeginFrame completado");

        // Preparar layers
        std::vector<XrCompositionLayerBaseHeader*> layers;
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        std::vector<XrCompositionLayerProjectionView> projectionViews(2);

        if (frameState.shouldRender) {
            LOGD("Iniciando renderizado...");

            // Inicializar shaders si es necesario
            if (!initializeShaders()) {
                LOGE("Error inicializando shaders");
                // End frame sin layers
                XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
                frameEndInfo.displayTime = frameState.predictedDisplayTime;
                frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
                frameEndInfo.layerCount = 0;
                frameEndInfo.layers = nullptr;
                xrEndFrame(g_openxrState.session, &frameEndInfo);
                return JNI_FALSE;
            }

            // Obtener poses de las vistas
            XrViewState viewState{XR_TYPE_VIEW_STATE};
            uint32_t viewCount = 2;
            XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};

            XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
            locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            locateInfo.displayTime = frameState.predictedDisplayTime;
            locateInfo.space = g_openxrState.appSpace;

            if (!CheckXrResult(xrLocateViews(g_openxrState.session, &locateInfo, &viewState, viewCount, &viewCount, views),
                               "xrLocateViews")) {
                return JNI_FALSE;
            }

            LOGD("Views localizadas. ViewState flags: 0x%X", viewState.viewStateFlags);

            // Verificar validez de las vistas
            if (!(viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) ||
                !(viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
                LOGD("Vistas no válidas, saltando renderizado");

                XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
                frameEndInfo.displayTime = frameState.predictedDisplayTime;
                frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
                frameEndInfo.layerCount = 0;
                frameEndInfo.layers = nullptr;
                return CheckXrResult(xrEndFrame(g_openxrState.session, &frameEndInfo), "xrEndFrame (no render)") ? JNI_TRUE : JNI_FALSE;
            }

            // Renderizar cada ojo
            for (int eye = 0; eye < 2; eye++) {
                LOGD("Renderizando ojo %d", eye);

                // Adquirir imagen del swapchain
                uint32_t imageIndex;
                XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                if (!CheckXrResult(xrAcquireSwapchainImage(g_swapchains[eye].swapchain, &acquireInfo, &imageIndex),
                                   "xrAcquireSwapchainImage")) {
                    LOGE("Error adquiriendo imagen swapchain ojo %d", eye);
                    return JNI_FALSE;
                }
                LOGD("Imagen swapchain adquirida: %d", imageIndex);

                // Esperar imagen
                XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                waitInfo.timeout = XR_INFINITE_DURATION;
                if (!CheckXrResult(xrWaitSwapchainImage(g_swapchains[eye].swapchain, &waitInfo),
                                   "xrWaitSwapchainImage")) {
                    LOGE("Error esperando imagen swapchain ojo %d", eye);
                    return JNI_FALSE;
                }

                // Configurar framebuffer
                GLuint framebuffer;
                glGenFramebuffers(1, &framebuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

                GLuint texture = g_swapchains[eye].images[imageIndex].image;
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

                GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (status != GL_FRAMEBUFFER_COMPLETE) {
                    LOGE("Framebuffer incompleto para ojo %d: 0x%x", eye, status);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glDeleteFramebuffers(1, &framebuffer);
                    return JNI_FALSE;
                }
                LOGD("Framebuffer configurado correctamente para ojo %d", eye);

                // Configurar viewport
                glViewport(0, 0, g_swapchains[eye].width, g_swapchains[eye].height);
                LOGD("Viewport configurado: %dx%d", g_swapchains[eye].width, g_swapchains[eye].height);

                // ===== RENDERIZADO MUY SIMPLE =====

                // Limpiar con color distintivo para cada ojo
                if (eye == 0) {
                    glClearColor(0.1f, 0.0f, 0.0f, 1.0f); // Rojo oscuro para ojo izquierdo
                } else {
                    glClearColor(0.0f, 0.0f, 0.1f, 1.0f); // Azul oscuro para ojo derecho
                }
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                LOGD("Clear completado para ojo %d", eye);

                // Usar shader program
                glUseProgram(g_shaderProgram);
                glBindVertexArray(g_VAO);

                // Renderizar rectángulo
                glDrawArrays(GL_TRIANGLES, 0, 6);

                glBindVertexArray(0);
                glUseProgram(0);

                // Verificar errores OpenGL
                GLenum glError = glGetError();
                if (glError != GL_NO_ERROR) {
                    LOGE("Error OpenGL en ojo %d: 0x%x", eye, glError);
                } else {
                    LOGD("Renderizado completado sin errores para ojo %d", eye);
                }

                // Limpiar framebuffer
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glDeleteFramebuffers(1, &framebuffer);

                // Liberar imagen del swapchain
                XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                if (!CheckXrResult(xrReleaseSwapchainImage(g_swapchains[eye].swapchain, &releaseInfo),
                                   "xrReleaseSwapchainImage")) {
                    LOGE("Error liberando imagen swapchain ojo %d", eye);
                    return JNI_FALSE;
                }
                LOGD("Imagen swapchain liberada para ojo %d", eye);

                // Configurar projection view
                projectionViews[eye] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                projectionViews[eye].pose = views[eye].pose;
                projectionViews[eye].fov = views[eye].fov;
                projectionViews[eye].subImage.swapchain = g_swapchains[eye].swapchain;
                projectionViews[eye].subImage.imageRect.offset = {0, 0};
                projectionViews[eye].subImage.imageRect.extent = {
                        static_cast<int32_t>(g_swapchains[eye].width),
                        static_cast<int32_t>(g_swapchains[eye].height)
                };
            }

            // Configurar layer de proyección
            layer.space = g_openxrState.appSpace;
            layer.viewCount = 2;
            layer.views = projectionViews.data();
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));

            LOGD("Layer de proyección configurado con %d views", layer.viewCount);
        } else {
            LOGD("FrameState.shouldRender = false, saltando renderizado");
        }

        // End frame
        XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
        frameEndInfo.displayTime = frameState.predictedDisplayTime;
        frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        frameEndInfo.layerCount = static_cast<uint32_t>(layers.size());
        frameEndInfo.layers = layers.data();

        bool endFrameResult = CheckXrResult(xrEndFrame(g_openxrState.session, &frameEndInfo), "xrEndFrame");
        LOGD("EndFrame completado con %d layers, resultado: %s", frameEndInfo.layerCount, endFrameResult ? "éxito" : "error");
        LOGD("=== FIN FRAME ===");

        return endFrameResult ? JNI_TRUE : JNI_FALSE;

    } catch (const std::exception& e) {
        LOGE("Excepción en runFrame: %s", e.what());
        return JNI_FALSE;
    } catch (...) {
        LOGE("Excepción desconocida en runFrame");
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_holamundo2_MainActivity_nativeShutdown(JNIEnv *env, jobject thiz) {
    LOGI("=== Cerrando OpenXR ===");

    try {
        std::lock_guard<std::mutex> lock(g_openxrState.stateMutex);

        // Terminar sesión si está corriendo
        if (g_openxrState.sessionRunning && g_openxrState.session != XR_NULL_HANDLE) {
            LOGI("Terminando sesión activa...");
            XrResult result = xrEndSession(g_openxrState.session);
            if (XR_FAILED(result)) {
                LOGE("Error terminando sesión: %d", result);
            }
            g_openxrState.sessionRunning = false;
        }

        // Limpiar swapchains
        cleanupSwapchains();

        // Limpiar espacio de referencia
        if (g_openxrState.appSpace != XR_NULL_HANDLE) {
            XrResult result = xrDestroySpace(g_openxrState.appSpace);
            if (XR_FAILED(result)) {
                LOGE("Error destruyendo space: %d", result);
            }
            g_openxrState.appSpace = XR_NULL_HANDLE;
        }

        // Limpiar sesión
        if (g_openxrState.session != XR_NULL_HANDLE) {
            XrResult result = xrDestroySession(g_openxrState.session);
            if (XR_FAILED(result)) {
                LOGE("Error destruyendo session: %d", result);
            }
            g_openxrState.session = XR_NULL_HANDLE;
        }

        // Limpiar instancia
        if (g_openxrState.instance != XR_NULL_HANDLE) {
            XrResult result = xrDestroyInstance(g_openxrState.instance);
            if (XR_FAILED(result)) {
                LOGE("Error destruyendo instance: %d", result);
            }
            g_openxrState.instance = XR_NULL_HANDLE;
        }

        // Limpiar recursos EGL (SIN superficie)
        if (g_openxrState.eglContext != EGL_NO_CONTEXT) {
            eglMakeCurrent(g_openxrState.eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(g_openxrState.eglDisplay, g_openxrState.eglContext);
            g_openxrState.eglContext = EGL_NO_CONTEXT;
        }
        if (g_openxrState.eglDisplay != EGL_NO_DISPLAY) {
            eglTerminate(g_openxrState.eglDisplay);
            g_openxrState.eglDisplay = EGL_NO_DISPLAY;
        }
        g_openxrState.eglConfig = nullptr;

        // Limpiar OpenXR loader
        if (g_openxrState.loaderInitialized) {
            LOGI("Limpiando recursos OpenXR...");
            g_openxrState.loaderInitialized = false;
            LOGI("✓ Recursos OpenXR limpiados");
        }

        // Reset completo del estado
        g_openxrState.reset();

        LOGI("=== OpenXR cerrado correctamente ===");

    } catch (const std::exception& e) {
        LOGE("Excepción durante shutdown: %s", e.what());
    } catch (...) {
        LOGE("Excepción desconocida durante shutdown");
    }
}