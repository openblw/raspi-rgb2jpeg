#include "globals.h"
#include "jpeg_encoder.h"

#define FILENAME    "/tmp/test_1.raw"

int main() {
    openlog("yuv2jpeg", LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_USER);

    if (!jpegEncoder::init()) {
        return EXIT_FAILURE;
    }

    unsigned char *imgBuf = NULL;
    unsigned char *jpegBuf = NULL;
    size_t len = 0;
    size_t jpegLen = 0;
    FILE *fp = NULL;

    fp = fopen(FILENAME, "rb");
    if (!fp) {
        syslog(LOG_ERR, "Could not open file.");
        return EXIT_FAILURE;
    }

    fseek(fp, 0, SEEK_END);
    len = jpegLen = ftell(fp);
    rewind(fp);
    imgBuf = (unsigned char *)malloc(len);
    jpegBuf = (unsigned char *)malloc(len);
    fread(imgBuf, 1, len, fp);
    fclose(fp);

    if (jpegEncoder::encode(imgBuf, len, jpegBuf, &jpegLen) && jpegLen > 0) {
        fp = fopen("/home/pi/git/raspi-rgb2jpeg/frame.jpg", "wb");
        if (fp) {
            fwrite(jpegBuf, 1, jpegLen, fp);
            fflush(fp);
            fclose(fp);
        }
        syslog(LOG_NOTICE, "Success!");
    } else {
            syslog(LOG_ERR, "Could not encode frame.");
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

