#include "xm_duration_parser.h"
#include "codec/duration_parser.h"
#include "log.h"

int get_file_duration_ms(const char *file_addr)
{
    if (!file_addr) return -1;
    LogInfo("%s file_addr %s.\n", __func__, file_addr);

    return get_audio_file_duration_ms(file_addr);
}
