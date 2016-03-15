#include "jpeg_encoder.h"
extern "C" {
#include "ilclient.h"
#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
#include <bcm_host.h>
}
using namespace jpegEncoder;

// globals
static ILCLIENT_T *client = NULL;
static COMPONENT_T *imageEncode = NULL;
static COMPONENT_T *list[5];
static OMX_HANDLETYPE compHandle = NULL;

// module's definitions
#define ALIGN(x, y) (((x) + ((y) - 1)) & ~((y) - 1))

#ifdef DEBUG_JPEG_ENCODER
static inline void print_def(OMX_PARAM_PORTDEFINITIONTYPE def) {
    syslog(LOG_DEBUG, 
        "Port %lu: %s %lu %s %s %lux%lu %lux%lu @%lu %u",
        (long unsigned)def.nPortIndex,
        def.eDir == OMX_DirInput ? "in" : "out",
        (long unsigned)def.nBufferSize,
        def.bEnabled ? "enabled" : "disabled",
        def.bPopulated ? "populated" : "not populated.",
        (long unsigned)def.format.image.nFrameWidth,
        (long unsigned)def.format.image.nFrameHeight,
        (long unsigned)def.format.image.nStride,
        (long unsigned)def.format.image.nSliceHeight,
        (long unsigned)def.format.image.eCompressionFormat, 
        (unsigned)def.format.image.eColorFormat);
}

static inline void print_state(OMX_HANDLETYPE handle) {
    OMX_STATETYPE state;
    OMX_ERRORTYPE err;
    err = OMX_GetState(handle, &state);
    if (err != OMX_ErrorNone) {
        syslog(LOG_ERR, "Error on getting state");
        return;
    }

    switch (state) {
    case OMX_StateLoaded:           
        syslog(LOG_DEBUG, "StateLoaded"); break; 
    case OMX_StateIdle:             
        syslog(LOG_DEBUG, "StateIdle"); break; 
    case OMX_StateExecuting:        
        syslog(LOG_DEBUG, "StateExecuting"); break; 
    case OMX_StatePause:
        syslog(LOG_DEBUG, "StatePause"); break; 
    case OMX_StateWaitForResources: 
        syslog(LOG_DEBUG, "StateWait"); break; 
    case OMX_StateInvalid:          
        syslog(LOG_DEBUG, "StateInvalid"); break; 
    default:                        
        syslog(LOG_DEBUG, "State unknown"); break; 
    }
}

static inline void print_format(OMX_IMAGE_PARAM_PORTFORMATTYPE format) {
    syslog(LOG_DEBUG, 
        "Index: %u; CompressionFormat: %u; ColorFormat: %u",
        format.nIndex,
        format.eCompressionFormat,
        format.eColorFormat);
}
#endif

//
// Public routines
//

bool jpegEncoder::init() {
    OMX_ERRORTYPE r;
    int iRet;

    memset(list, 0, sizeof(list));

    // initializes bcm host
    bcm_host_init();

    // initializes IL and get handle 
    if ((client = ilclient_init()) == NULL) {
        syslog(LOG_ERR, "Could not init IL client.");
        return false;
    }

    // initializes OMX
    if (OMX_Init() != OMX_ErrorNone) {
        syslog(LOG_ERR, "OMX init failed.");
        return false;
    }

    // creating the image_encode component into GPU
    int flags = 
        (int)ILCLIENT_DISABLE_ALL_PORTS | 
                (int)ILCLIENT_ENABLE_INPUT_BUFFERS |
                    (int)ILCLIENT_ENABLE_OUTPUT_BUFFERS;

    iRet = ilclient_create_component(client, &imageEncode,
                (char *)"image_encode", 
                    (ILCLIENT_CREATE_FLAGS_T)flags);
    if (iRet == -1) {
        syslog(LOG_ERR, "Could not create image_encode_component.");
        return false;
    }
    list[0] = imageEncode;

    compHandle = ilclient_get_handle(imageEncode); // getting handle from videoEncode component

    // OMX.broadcom.image_encode
    // ref: http://home.nouwen.name/RaspberryPi/documentation/ilcomponents/image_encode.html

    // getting information from port 340, input, in order to
    // fill it with new options

    OMX_PARAM_PORTDEFINITIONTYPE def;
    size_t len = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);

    memset(&def, 0, len);
    def.nSize = len;
    def.nVersion.nVersion = OMX_VERSION;
    def.nPortIndex = 340;

    if (OMX_GetParameter(compHandle, OMX_IndexParamPortDefinition, &def) != OMX_ErrorNone) { // getting OMX_IndexParamPortDefinition from port 340

        syslog(LOG_ERR, "Could not get OMX_IndexParamPortDefinition on OMX.broadcom.image_encode:340.");

        return false;
    }
#ifdef DEBUG_JPEG_ENCODER
    print_def(def);
#endif

    // setting new attributes to the port 340
    def.format.image.nFrameWidth = FRAME_WIDTH;
    def.format.image.nFrameHeight = FRAME_HEIGHT;
    int width32 = ALIGN(FRAME_WIDTH, 32);
    int height16 = ALIGN(FRAME_HEIGHT, 16);
    def.format.image.nStride = width32;
    def.format.image.nSliceHeight = height16;
    def.format.image.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;
    def.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
    def.nBufferSize = 614400;

    r = OMX_SetParameter(compHandle, OMX_IndexParamPortDefinition, &def);
    if (r != OMX_ErrorNone) {
        syslog(LOG_ERR, "Could not set OMX_IndexParamPortDefinition on OMX.broadcom.image_encode:340.");

        return false;
    }

#ifdef DEBUG_JPEG_ENCODER
    // verify settings of image_encode's input port (340)
    if (OMX_GetParameter(compHandle, OMX_IndexParamPortDefinition, &def) != OMX_ErrorNone) {
        syslog(LOG_ERR, "Could not get OMX_IndexParamPortDefinition on OMX.broadcom.image_encode:340.");

        return false;
    }

    print_def(def);
#endif

    // setting format on image_encode's output port (341)
    OMX_IMAGE_PARAM_PORTFORMATTYPE format;
    len = sizeof(OMX_IMAGE_PARAM_PORTFORMATTYPE);

    memset(&format, 0, len);

    format.nSize = len;
    format.nVersion.nVersion = OMX_VERSION;
    format.nPortIndex = 341;
    format.eCompressionFormat = OMX_IMAGE_CodingJPEG;
    format.eColorFormat = OMX_COLOR_FormatUnused;

    // updating attributes from port 341 (output)
    r = OMX_SetParameter(compHandle, OMX_IndexParamImagePortFormat, &format);
    if (r != OMX_ErrorNone) {
        syslog(LOG_ERR, "Could not set OMX_IndexParamImagePortFormat on OMX.broadcom.image_encode:341.");

        return false;
    }

#ifdef DEBUG_JPEG_ENCODER
    // verify format of image_encode output port
    if (OMX_GetParameter(compHandle, OMX_IndexParamImagePortFormat, &format) != OMX_ErrorNone) {
        syslog(LOG_ERR, "Could not get OMX_IndexParamImagePortFormat OMX.broadcom.image_encode:341.");

        return false;
    }

    print_format(format);
#endif

    // putting the imageEncode in IDLE state
    iRet = ilclient_change_component_state(imageEncode, OMX_StateIdle);
    if (iRet == -1) {
        syslog(LOG_ERR, "Could not change OMX.broadcom.image_encode's state to OMX_StateIdle.");

        return false;
    }
#ifdef DEBUG_JPEG_ENCODER
    print_state(compHandle);
#endif

    // enabling port buffers to the port 340
    iRet = ilclient_enable_port_buffers(imageEncode, 340, NULL, NULL, NULL);
    if (iRet != 0) {
        syslog(LOG_ERR, "Could not enable port buffers for OMX.broadcom.image_encode:340.");

        return false;
    }

    // enabling port buffers to the port 341
    iRet = ilclient_enable_port_buffers(imageEncode, 341, NULL, NULL, NULL);
    if (iRet != 0) {
        syslog(LOG_ERR, "Could not enable port buffers for OMX.broadcom.image_encode:341.");

        return false;
    }

    // putting the image_encode in Executing state
    iRet = ilclient_change_component_state(imageEncode, OMX_StateExecuting);

    if (iRet != 0) {
        syslog(LOG_ERR, "Could not put OMX.broadcom.image_encode into OMX_StateExecuting mode.");

        return false;
    }
#ifdef DEBUG_JPEG_ENCODER
    print_state(compHandle);
#endif

    return true;
}

bool jpegEncoder::encode(const unsigned char *bufIn, size_t bufLenIn, unsigned char *bufOut, size_t *bufLenOut) {

    // getting some buffer from imageEncode on port 340
    OMX_BUFFERHEADERTYPE *buf;
    buf = ilclient_get_input_buffer(imageEncode, 340, 1);   
    if (buf == NULL) {
        syslog(LOG_ERR, "No buffer available for convert frame in OMX.broadcom.image_encode:340.");

        return false;
    }

    size_t len = min(buf->nAllocLen, bufLenIn);

    memcpy(buf->pBuffer, bufIn, len); // copying from bufIn to buf->pBuffer

    buf->nFilledLen = len;

    OMX_EmptyThisBuffer(compHandle, buf); // freeing used buffer in the port 340

    // getting jpeg image chunk back from image_encode
    OMX_BUFFERHEADERTYPE *out;
    OMX_ERRORTYPE r;

    out = ilclient_get_output_buffer(imageEncode, 341, 1);
    if (out == NULL) {
        syslog(LOG_ERR, "Could not get out JPEG buffer.");

        return false;
    }

    usleep(1 * 1000);   // some weird hack goes here: we need to wait some miliseconds (not microseconds) in order to get that fucking crap back

    r = OMX_FillThisBuffer(compHandle, out);
    if (r != OMX_ErrorNone) {
        syslog(LOG_ERR, "Error filling out JPEG buffer.");

        return false;
    }

    // copying jpeg chunk into bufOut
    if (out->nFilledLen > 0) {
        len = min(*bufLenOut, out->nFilledLen);
        *bufLenOut = len;
        memcpy(bufOut, out->pBuffer, len);
    }

    out->nFilledLen = 0;

    return true;
}

void jpegEncoder::close() {
    if (client) {   // closing handle if was opened
        ilclient_destroy(client);
    }
}

