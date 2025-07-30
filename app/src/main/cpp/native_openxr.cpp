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
#include <cstring>  // AGREGADO: Para strcpy

#define LOG_TAG "OpenXRHolaMundo"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Variables globales para OpenXR
XrInstance instance = XR_NULL_HANDLE;
XrSession session = XR_NULL_HANDLE;
XrSpace appSpace = XR_NULL_HANDLE;
XrSystemId systemId = XR_NULL_SYSTEM_ID;
XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;

// Variables específicas para Meta Quest
JavaVM* javaVm = nullptr;
jobject activityObject = nullptr;
ANativeWindow* nativeWindow = nullptr;

// Información de swapchain
struct SwapchainInfo {
    XrSwapchain swapchain;
    uint32_t width;
    uint32_t height;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
};

std::vector<SwapchainInfo> swapchains;
XrViewConfigurationView viewConfigs[2];

// AGREGADO: Función para inicializar variables globales
void initializeGlobalVariables() {
    instance = XR_NULL_HANDLE;
    session = XR_NULL_HANDLE;
    appSpace = XR_NULL_HANDLE;
    systemId = XR_NULL_SYSTEM_ID;
    sessionState = XR_SESSION_STATE_UNKNOWN;
    javaVm = nullptr;
    activityObject = nullptr;
    nativeWindow = nullptr;
    swapchains.clear();
}

bool CheckXrResult(XrResult result, const char* operation) {
    if (XR_FAILED(result)) {
        LOGE("OpenXR Error: %s failed with result %d", operation, result);
        return false;
    }
    return true;
}

// AGREGADO: Función para verificar extensiones disponibles
bool verifyRequiredExtensions() {
    uint32_t extensionCount = 0;
    if (!CheckXrResult(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr),
                       "xrEnumerateInstanceExtensionProperties")) {
        return false;
    }

    std::vector<XrExtensionProperties> availableExtensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
    if (!CheckXrResult(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, availableExtensions.data()),
                       "xrEnumerateInstanceExtensionProperties")) {
        return false;
    }

    // Verificar que las extensiones requeridas estén disponibles
    bool androidExtensionAvailable = false;
    bool openglExtensionAvailable = false;

    for (const auto& ext : availableExtensions) {
        if (strcmp(ext.extensionName, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) == 0) {
            androidExtensionAvailable = true;
        }
        if (strcmp(ext.extensionName, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME) == 0) {
            openglExtensionAvailable = true;
        }
    }

    if (!androidExtensionAvailable) {
        LOGE("Extensión Android CREATE_INSTANCE no disponible");
        return false;
    }

    if (!openglExtensionAvailable) {
        LOGE("Extensión OpenGL ES no disponible");
        return false;
    }

    LOGI("Extensiones requeridas verificadas correctamente");
    return true;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_holamundo2_MainActivity_nativeInitialize(JNIEnv *env, jobject thiz) {
    LOGI("Inicializando OpenXR...");

    // AGREGADO: Inicializar variables globales
    initializeGlobalVariables();

    // AGREGADO: Verificar extensiones disponibles
    if (!verifyRequiredExtensions()) {
        LOGE("Extensiones requeridas no están disponibles");
        return JNI_FALSE;
    }

    // Crear instancia OpenXR
    std::vector<const char*> extensions = {
            XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME
    };

    XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy(instanceInfo.applicationInfo.applicationName, "HolaMundo VR");
    instanceInfo.applicationInfo.applicationVersion = 1;
    strcpy(instanceInfo.applicationInfo.engineName, "Custom Engine");
    instanceInfo.applicationInfo.engineVersion = 1;
    instanceInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.enabledExtensionNames = extensions.data();

    if (!CheckXrResult(xrCreateInstance(&instanceInfo, &instance), "xrCreateInstance")) {
        return JNI_FALSE;
    }

    // Obtener sistema
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    if (!CheckXrResult(xrGetSystem(instance, &systemInfo, &systemId), "xrGetSystem")) {
        return JNI_FALSE;
    }

    // Verificar configuración de vista
    uint32_t viewCount = 0;
    if (!CheckXrResult(xrEnumerateViewConfigurationViews(instance, systemId,
                                                         XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, viewConfigs),
                       "xrEnumerateViewConfigurationViews")) {
        return JNI_FALSE;
    }

    if (viewCount != 2) {
        LOGE("Se esperaban 2 vistas, pero se encontraron %d", viewCount);
        return JNI_FALSE;
    }

    LOGI("OpenXR inicializado correctamente. Views: %d", viewCount);
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_holamundo2_MainActivity_nativeCreateSession(JNIEnv *env, jobject thiz) {
    LOGI("Creando sesión OpenXR...");

    // MEJORADO: Configurar OpenGL ES binding con verificación EGL
    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();

    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        LOGE("No hay contexto EGL válido");
        return JNI_FALSE;
    }

    // CORREGIDO: Obtener configuración EGL actual
    EGLConfig config;
    EGLint numConfigs;
    if (!eglGetConfigs(display, &config, 1, &numConfigs) || numConfigs == 0) {
        LOGE("No se pudo obtener configuración EGL");
        return JNI_FALSE;
    }

    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    graphicsBinding.display = display;
    graphicsBinding.config = config;  // CORREGIDO: Ya no es nullptr
    graphicsBinding.context = context;

    // Crear sesión
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &graphicsBinding;
    sessionInfo.systemId = systemId;

    if (!CheckXrResult(xrCreateSession(instance, &sessionInfo, &session), "xrCreateSession")) {
        return JNI_FALSE;
    }

    // Crear espacio de referencia
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace = {{0, 0, 0, 1}, {0, 0, 0}};

    if (!CheckXrResult(xrCreateReferenceSpace(session, &spaceInfo, &appSpace), "xrCreateReferenceSpace")) {
        return JNI_FALSE;
    }

    // AGREGADO: Verificar formatos de swapchain soportados
    uint32_t formatCount = 0;
    if (!CheckXrResult(xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr),
                       "xrEnumerateSwapchainFormats")) {
        return JNI_FALSE;
    }

    std::vector<int64_t> formats(formatCount);
    if (!CheckXrResult(xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data()),
                       "xrEnumerateSwapchainFormats")) {
        return JNI_FALSE;
    }

    // Buscar GL_RGBA8 en los formatos soportados
    bool formatSupported = false;
    for (auto format : formats) {
        if (format == GL_RGBA8) {
            formatSupported = true;
            break;
        }
    }

    if (!formatSupported) {
        LOGE("Formato GL_RGBA8 no soportado");
        return JNI_FALSE;
    }

    // Crear swapchains para cada ojo
    swapchains.resize(2);
    for (int eye = 0; eye < 2; eye++) {
        XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swapchainInfo.width = viewConfigs[eye].recommendedImageRectWidth;
        swapchainInfo.height = viewConfigs[eye].recommendedImageRectHeight;
        swapchainInfo.format = GL_RGBA8;
        swapchainInfo.mipCount = 1;
        swapchainInfo.faceCount = 1;
        swapchainInfo.arraySize = 1;
        swapchainInfo.sampleCount = 1;
        swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

        if (!CheckXrResult(xrCreateSwapchain(session, &swapchainInfo, &swapchains[eye].swapchain),
                           "xrCreateSwapchain")) {
            return JNI_FALSE;
        }

        swapchains[eye].width = swapchainInfo.width;
        swapchains[eye].height = swapchainInfo.height;

        // Obtener imágenes del swapchain
        uint32_t imageCount;
        if (!CheckXrResult(xrEnumerateSwapchainImages(swapchains[eye].swapchain, 0, &imageCount, nullptr),
                           "xrEnumerateSwapchainImages")) {
            return JNI_FALSE;
        }

        swapchains[eye].images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        if (!CheckXrResult(xrEnumerateSwapchainImages(swapchains[eye].swapchain, imageCount, &imageCount,
                                                      reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchains[eye].images.data())),
                           "xrEnumerateSwapchainImages")) {
            return JNI_FALSE;
        }

        LOGI("Swapchain %d creado: %dx%d, %d imágenes", eye, swapchains[eye].width, swapchains[eye].height, imageCount);
    }

    LOGI("Sesión OpenXR creada correctamente");
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_holamundo2_MainActivity_nativeRunFrame(JNIEnv *env, jobject thiz) {
    // Poll events
    XrEventDataBuffer eventData{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult result = xrPollEvent(instance, &eventData);
    while (result == XR_SUCCESS) {
        if (eventData.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto stateEvent = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
            sessionState = stateEvent->state;

            LOGI("Session state changed to: %d", sessionState);

            if (sessionState == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
                beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (!CheckXrResult(xrBeginSession(session, &beginInfo), "xrBeginSession")) {
                    return JNI_FALSE;
                }
                LOGI("Sesión OpenXR iniciada");
            } else if (sessionState == XR_SESSION_STATE_STOPPING) {
                if (!CheckXrResult(xrEndSession(session), "xrEndSession")) {
                    return JNI_FALSE;
                }
                LOGI("Sesión OpenXR terminada");
            }
        }
        result = xrPollEvent(instance, &eventData);
    }

    // Si la sesión no está en estado activo, no renderizar
    if (sessionState != XR_SESSION_STATE_SYNCHRONIZED &&
        sessionState != XR_SESSION_STATE_VISIBLE &&
        sessionState != XR_SESSION_STATE_FOCUSED) {
        return JNI_TRUE;
    }

    // Wait frame
    XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    if (!CheckXrResult(xrWaitFrame(session, &frameWaitInfo, &frameState), "xrWaitFrame")) {
        return JNI_FALSE;
    }

    // Begin frame
    XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (!CheckXrResult(xrBeginFrame(session, &frameBeginInfo), "xrBeginFrame")) {
        return JNI_FALSE;
    }

    // Preparar capas de composición
    std::vector<XrCompositionLayerBaseHeader*> layers;
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::vector<XrCompositionLayerProjectionView> projectionViews(2);

    if (frameState.shouldRender) {
        // Obtener poses de las vistas (posición y orientación de cada ojo)
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCount = 2;
        XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};

        XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = frameState.predictedDisplayTime;
        locateInfo.space = appSpace;

        if (!CheckXrResult(xrLocateViews(session, &locateInfo, &viewState, viewCount, &viewCount, views),
                           "xrLocateViews")) {
            return JNI_FALSE;
        }

        // MEJORADO: Verificar que las vistas son válidas
        if (!(viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) ||
            !(viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
            LOGE("Vistas no válidas, saltando frame");
            // End frame sin capas
            XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
            frameEndInfo.displayTime = frameState.predictedDisplayTime;
            frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            frameEndInfo.layerCount = 0;
            frameEndInfo.layers = nullptr;
            xrEndFrame(session, &frameEndInfo);
            return JNI_TRUE;
        }

        // Renderizar cada ojo
        for (int eye = 0; eye < 2; eye++) {
            // Adquirir imagen del swapchain
            uint32_t imageIndex;
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (!CheckXrResult(xrAcquireSwapchainImage(swapchains[eye].swapchain, &acquireInfo, &imageIndex),
                               "xrAcquireSwapchainImage")) {
                return JNI_FALSE;
            }

            // Esperar a que la imagen esté lista
            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitInfo.timeout = XR_INFINITE_DURATION;
            if (!CheckXrResult(xrWaitSwapchainImage(swapchains[eye].swapchain, &waitInfo),
                               "xrWaitSwapchainImage")) {
                return JNI_FALSE;
            }

            // MEJORADO: Crear y configurar framebuffer con verificación de errores
            GLuint framebuffer;
            glGenFramebuffers(1, &framebuffer);
            if (glGetError() != GL_NO_ERROR) {
                LOGE("Error generando framebuffer");
                return JNI_FALSE;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

            // Attachar la textura del swapchain al framebuffer
            GLuint texture = swapchains[eye].images[imageIndex].image;
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

            // Verificar que el framebuffer esté completo
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                LOGE("Framebuffer no está completo para el ojo %d: 0x%x", eye, status);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glDeleteFramebuffers(1, &framebuffer);
                return JNI_FALSE;
            }

            // Configurar viewport
            glViewport(0, 0, static_cast<GLsizei>(swapchains[eye].width), static_cast<GLsizei>(swapchains[eye].height));

            // Limpiar buffer
            glClearColor(0.0f, 0.2f, 0.0f, 1.0f); // Fondo verde oscuro
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Habilitar depth testing
            glEnable(GL_DEPTH_TEST);

            // AQUÍ ES DONDE RENDERIZARÍAS TU CONTENIDO 3D
            // Por ejemplo, podrías llamar a una función de renderizado:
            // renderScene(views[eye], eye);

            // Por ahora, solo renderizamos un color sólido como placeholder
            // En una implementación real, aquí renderizarías tu geometría 3D,
            // texto "Hola Mundo", etc., usando las matrices de vista y proyección
            // de views[eye].pose y views[eye].fov

            // AGREGADO: Verificar errores OpenGL después del renderizado
            GLenum glError = glGetError();
            if (glError != GL_NO_ERROR) {
                LOGE("Error OpenGL durante renderizado del ojo %d: 0x%x", eye, glError);
            }

            // Limpiar framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &framebuffer);

            // Liberar imagen del swapchain
            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            if (!CheckXrResult(xrReleaseSwapchainImage(swapchains[eye].swapchain, &releaseInfo),
                               "xrReleaseSwapchainImage")) {
                return JNI_FALSE;
            }

            // Configurar vista de proyección para este ojo
            projectionViews[eye] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
            projectionViews[eye].pose = views[eye].pose;
            projectionViews[eye].fov = views[eye].fov;
            projectionViews[eye].subImage.swapchain = swapchains[eye].swapchain;
            projectionViews[eye].subImage.imageRect.offset = {0, 0};
            projectionViews[eye].subImage.imageRect.extent = {
                    static_cast<int32_t>(swapchains[eye].width),
                    static_cast<int32_t>(swapchains[eye].height)
            };
        }

        // Configurar capa de proyección
        layer.space = appSpace;
        layer.viewCount = 2;
        layer.views = projectionViews.data();
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
    }

    // End frame con las capas configuradas
    XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
    frameEndInfo.displayTime = frameState.predictedDisplayTime;
    frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    frameEndInfo.layerCount = static_cast<uint32_t>(layers.size());
    frameEndInfo.layers = layers.data();

    if (!CheckXrResult(xrEndFrame(session, &frameEndInfo), "xrEndFrame")) {
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_holamundo2_MainActivity_nativeShutdown(JNIEnv *env, jobject thiz) {
    LOGI("Cerrando OpenXR...");

    // MEJORADO: Limpiar swapchains con verificación
    for (auto& swapchainInfo : swapchains) {
        if (swapchainInfo.swapchain != XR_NULL_HANDLE) {
            XrResult result = xrDestroySwapchain(swapchainInfo.swapchain);
            if (XR_FAILED(result)) {
                LOGE("Error destruyendo swapchain: %d", result);
            }
        }
    }
    swapchains.clear();

    // Limpiar espacios y sesión
    if (appSpace != XR_NULL_HANDLE) {
        XrResult result = xrDestroySpace(appSpace);
        if (XR_FAILED(result)) {
            LOGE("Error destruyendo space: %d", result);
        }
        appSpace = XR_NULL_HANDLE;
    }

    if (session != XR_NULL_HANDLE) {
        XrResult result = xrDestroySession(session);
        if (XR_FAILED(result)) {
            LOGE("Error destruyendo session: %d", result);
        }
        session = XR_NULL_HANDLE;
    }

    if (instance != XR_NULL_HANDLE) {
        XrResult result = xrDestroyInstance(instance);
        if (XR_FAILED(result)) {
            LOGE("Error destruyendo instance: %d", result);
        }
        instance = XR_NULL_HANDLE;
    }

    // AGREGADO: Reinicializar variables globales
    initializeGlobalVariables();

    LOGI("OpenXR cerrado correctamente");
}