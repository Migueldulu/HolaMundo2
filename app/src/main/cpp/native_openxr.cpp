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

    bool isInitialized = false;
    bool isSessionCreated = false;
    bool sessionRunning = false;

    std::mutex stateMutex;

    void reset() {
        std::lock_guard<std::mutex> lock(stateMutex);
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

    // Log de todas las extensiones disponibles
    LOGD("Extensiones disponibles:");
    for (const auto& ext : availableExtensions) {
        LOGD("  - %s (v%d)", ext.extensionName, ext.extensionVersion);
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

    // Verificar contexto OpenGL - DEBE estar disponible ahora
    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();

    LOGI("Verificando contexto OpenGL...");
    LOGI("EGL Display: %p", display);
    LOGI("EGL Context: %p", context);

    if (display == EGL_NO_DISPLAY) {
        LOGE("CRÍTICO: No hay display EGL válido para OpenXR");
        return JNI_FALSE;
    }

    if (context == EGL_NO_CONTEXT) {
        LOGE("CRÍTICO: No hay contexto EGL válido para OpenXR");
        return JNI_FALSE;
    }

    // Verificar que el contexto está activo
    EGLDisplay currentDisplay = eglGetCurrentDisplay();
    EGLContext currentContext = eglGetCurrentContext();

    if (currentDisplay != display || currentContext != context) {
        LOGI("Contexto no está activo, activándolo...");
        EGLSurface currentSurface = eglGetCurrentSurface(EGL_DRAW);
        if (!eglMakeCurrent(display, currentSurface, currentSurface, context)) {
            LOGE("No se pudo activar contexto OpenGL");
            return JNI_FALSE;
        }
    }

    // Verificar versión OpenGL
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);

    LOGI("✓ Contexto OpenGL verificado:");
    LOGI("  Version: %s", version ? version : "unknown");
    LOGI("  Vendor: %s", vendor ? vendor : "unknown");
    LOGI("  Renderer: %s", renderer ? renderer : "unknown");

    // Reset del estado
    g_openxrState.reset();
    cleanupSwapchains();
    try {
        // Verificar extensiones disponibles
        if (!verifyRequiredExtensions()) {
            LOGE("Extensiones requeridas no están disponibles");
            return JNI_FALSE;
        }

        // Configurar extensiones requeridas
        std::vector<const char*> extensions = {
                XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME
        };

        // Crear información de instancia
        XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
        strncpy(instanceInfo.applicationInfo.applicationName, "HolaMundo VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
        instanceInfo.applicationInfo.applicationName[XR_MAX_APPLICATION_NAME_SIZE - 1] = '\0';
        instanceInfo.applicationInfo.applicationVersion = 1;
        strncpy(instanceInfo.applicationInfo.engineName, "Custom Engine", XR_MAX_ENGINE_NAME_SIZE - 1);
        instanceInfo.applicationInfo.engineName[XR_MAX_ENGINE_NAME_SIZE - 1] = '\0';
        instanceInfo.applicationInfo.engineVersion = 1;
        instanceInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instanceInfo.enabledExtensionNames = extensions.data();

        LOGI("Creando instancia OpenXR...");
        if (!CheckXrResult(xrCreateInstance(&instanceInfo, &g_openxrState.instance), "xrCreateInstance")) {
            return JNI_FALSE;
        }

        LOGI("✓ Instancia OpenXR creada correctamente");

        // Obtener propiedades de la instancia
        XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
        if (CheckXrResult(xrGetInstanceProperties(g_openxrState.instance, &instanceProperties), "xrGetInstanceProperties")) {
            LOGI("Runtime: %s v%d.%d.%d",
                 instanceProperties.runtimeName,
                 XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
                 XR_VERSION_MINOR(instanceProperties.runtimeVersion),
                 XR_VERSION_PATCH(instanceProperties.runtimeVersion));
        }

        // Obtener sistema HMD
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

        LOGI("Obteniendo sistema HMD...");
        if (!CheckXrResult(xrGetSystem(g_openxrState.instance, &systemInfo, &g_openxrState.systemId), "xrGetSystem")) {
            LOGE("No se pudo encontrar un HMD compatible");
            return JNI_FALSE;
        }

        LOGI("✓ Sistema HMD encontrado (ID: %llu)", (unsigned long long)g_openxrState.systemId);

        // Obtener propiedades del sistema
        XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
        if (CheckXrResult(xrGetSystemProperties(g_openxrState.instance, g_openxrState.systemId, &systemProperties), "xrGetSystemProperties")) {
            LOGI("Sistema: %s", systemProperties.systemName);
            LOGI("Vendor ID: %d", systemProperties.vendorId);
            LOGI("Tracking: %s", systemProperties.trackingProperties.orientationTracking ? "✓" : "✗");
            LOGI("Position Tracking: %s", systemProperties.trackingProperties.positionTracking ? "✓" : "✗");
        }

        // Verificar configuración de vista
        uint32_t viewCount = 0;
        if (!CheckXrResult(xrEnumerateViewConfigurationViews(g_openxrState.instance, g_openxrState.systemId,
                                                             XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, g_viewConfigs),
                           "xrEnumerateViewConfigurationViews")) {
            return JNI_FALSE;
        }

        if (viewCount != 2) {
            LOGE("Se esperaban 2 vistas, pero se encontraron %d", viewCount);
            return JNI_FALSE;
        }

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

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_holamundo2_MainActivity_nativeCreateSession(JNIEnv *env, jobject thiz) {
    LOGI("=== Creando sesión OpenXR ===");

    if (!g_openxrState.isInitialized) {
        LOGE("OpenXR no está inicializado");
        return JNI_FALSE;
    }

    try {
        // Verificar y obtener contexto EGL actual
        EGLDisplay display = eglGetCurrentDisplay();
        EGLContext context = eglGetCurrentContext();

        if (display == EGL_NO_DISPLAY) {
            LOGE("No hay display EGL válido");
            return JNI_FALSE;
        }

        if (context == EGL_NO_CONTEXT) {
            LOGE("No hay contexto EGL válido");
            return JNI_FALSE;
        }

        LOGI("✓ Contexto EGL válido encontrado");

        // Obtener configuración EGL
        EGLConfig config;
        EGLint numConfigs;
        EGLint configAttribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_DEPTH_SIZE, 24,
                EGL_STENCIL_SIZE, 8,
                EGL_NONE
        };

        if (!eglGetConfigs(display, &config, 1, &numConfigs) || numConfigs == 0) {
            LOGE("No se pudo obtener configuración EGL");
            return JNI_FALSE;
        }

        LOGI("✓ Configuración EGL obtenida");

        // Configurar binding OpenGL ES
        XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
        graphicsBinding.display = display;
        graphicsBinding.config = config;
        graphicsBinding.context = context;

        // Crear sesión
        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
        sessionInfo.next = &graphicsBinding;
        sessionInfo.systemId = g_openxrState.systemId;

        LOGI("Creando sesión...");
        if (!CheckXrResult(xrCreateSession(g_openxrState.instance, &sessionInfo, &g_openxrState.session), "xrCreateSession")) {
            return JNI_FALSE;
        }

        LOGI("✓ Sesión OpenXR creada correctamente");

        // Crear espacio de referencia local
        XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        spaceInfo.poseInReferenceSpace = {{0, 0, 0, 1}, {0, 0, 0}};

        if (!CheckXrResult(xrCreateReferenceSpace(g_openxrState.session, &spaceInfo, &g_openxrState.appSpace), "xrCreateReferenceSpace")) {
            return JNI_FALSE;
        }

        LOGI("✓ Espacio de referencia creado");

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

        // Buscar formato apropiado (preferir GL_RGBA8, fallback a GL_RGB8)
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

        g_openxrState.isSessionCreated = true;
        LOGI("=== Sesión OpenXR creada correctamente ===");
        return JNI_TRUE;

    } catch (const std::exception& e) {
        LOGE("Excepción durante creación de sesión: %s", e.what());
        return JNI_FALSE;
    } catch (...) {
        LOGE("Excepción desconocida durante creación de sesión");
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_holamundo2_MainActivity_nativeRunFrame(JNIEnv *env, jobject thiz) {
    if (!g_openxrState.isSessionCreated) {
        return JNI_FALSE;
    }

    try {
        // Poll events con mejor manejo
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
                default:
                    LOGD("Evento OpenXR no manejado: %d", eventData.type);
                    break;
            }

            result = xrPollEvent(g_openxrState.instance, &eventData);
        }

        // Si no estamos en un estado que permita renderizado, salir
        if (g_openxrState.sessionState != XR_SESSION_STATE_SYNCHRONIZED &&
            g_openxrState.sessionState != XR_SESSION_STATE_VISIBLE &&
            g_openxrState.sessionState != XR_SESSION_STATE_FOCUSED) {
            return JNI_TRUE; // No es error, solo esperamos
        }

        // Wait frame
        XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        if (!CheckXrResult(xrWaitFrame(g_openxrState.session, &frameWaitInfo, &frameState), "xrWaitFrame")) {
            return JNI_FALSE;
        }

        // Begin frame
        XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        if (!CheckXrResult(xrBeginFrame(g_openxrState.session, &frameBeginInfo), "xrBeginFrame")) {
            return JNI_FALSE;
        }

        // Preparar layers de composición
        std::vector<XrCompositionLayerBaseHeader*> layers;
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        std::vector<XrCompositionLayerProjectionView> projectionViews(2);

        if (frameState.shouldRender) {
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

            // Verificar validez de las vistas
            if (!(viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) ||
                !(viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
                LOGD("Vistas no válidas, saltando renderizado");

                // End frame sin layers
                XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
                frameEndInfo.displayTime = frameState.predictedDisplayTime;
                frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
                frameEndInfo.layerCount = 0;
                frameEndInfo.layers = nullptr;
                return CheckXrResult(xrEndFrame(g_openxrState.session, &frameEndInfo), "xrEndFrame (no render)") ? JNI_TRUE : JNI_FALSE;
            }

            // Renderizar cada ojo
            for (int eye = 0; eye < 2; eye++) {
                // Adquirir imagen del swapchain
                uint32_t imageIndex;
                XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                if (!CheckXrResult(xrAcquireSwapchainImage(g_swapchains[eye].swapchain, &acquireInfo, &imageIndex),
                                   "xrAcquireSwapchainImage")) {
                    return JNI_FALSE;
                }

                // Esperar imagen
                XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                waitInfo.timeout = XR_INFINITE_DURATION;
                if (!CheckXrResult(xrWaitSwapchainImage(g_swapchains[eye].swapchain, &waitInfo),
                                   "xrWaitSwapchainImage")) {
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

                // Configurar viewport y renderizado
                glViewport(0, 0, g_swapchains[eye].width, g_swapchains[eye].height);

                // Colores diferentes para cada ojo para debug
                if (eye == 0) {
                    glClearColor(0.1f, 0.3f, 0.1f, 1.0f); // Verde oscuro para ojo izquierdo
                } else {
                    glClearColor(0.1f, 0.1f, 0.3f, 1.0f); // Azul oscuro para ojo derecho
                }

                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST);

                // AQUÍ RENDERIZARÍAS TU CONTENIDO 3D
                // Por ejemplo: renderText3D("¡Hola Mundo VR!", views[eye]);

                // Verificar errores OpenGL
                GLenum glError = glGetError();
                if (glError != GL_NO_ERROR) {
                    LOGE("Error OpenGL en ojo %d: 0x%x", eye, glError);
                }

                // Limpiar
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glDeleteFramebuffers(1, &framebuffer);

                // Liberar imagen
                XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                if (!CheckXrResult(xrReleaseSwapchainImage(g_swapchains[eye].swapchain, &releaseInfo),
                                   "xrReleaseSwapchainImage")) {
                    return JNI_FALSE;
                }

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
        }

        // End frame
        XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
        frameEndInfo.displayTime = frameState.predictedDisplayTime;
        frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        frameEndInfo.layerCount = static_cast<uint32_t>(layers.size());
        frameEndInfo.layers = layers.data();

        return CheckXrResult(xrEndFrame(g_openxrState.session, &frameEndInfo), "xrEndFrame") ? JNI_TRUE : JNI_FALSE;

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

// Reset completo del estado
g_openxrState.reset();

LOGI("=== OpenXR cerrado correctamente ===");

} catch (const std::exception& e) {
LOGE("Excepción durante shutdown: %s", e.what());
} catch (...) {
LOGE("Excepción desconocida durante shutdown");
}
}