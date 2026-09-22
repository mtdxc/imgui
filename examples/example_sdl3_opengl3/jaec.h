#include <stdint.h>
extern "C" {

typedef struct jaec_frontend JAEC_FRONTEND;

JAEC_FRONTEND *jaec_frontend_create(const char *model_path);
void jaec_frontend_destroy(JAEC_FRONTEND *ctx);
const char *jaec_frontend_last_error();
const char *jaec_frontend_last_error2(JAEC_FRONTEND *ctx);
int jaec_frontend_reset(JAEC_FRONTEND *ctx);
int jaec_frontend_process(
    JAEC_FRONTEND *ctx,
    const int16_t *nearend_mic,
    const int16_t *farend_speech,
    int frame_len,
    int16_t *output
);

}