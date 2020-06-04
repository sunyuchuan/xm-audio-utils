#ifndef XM_DURATION_PARSER_H
#define XM_DURATION_PARSER_H
#include <stdbool.h>
#include "em_macro_def.h"

/**
 * @brief Get the duration of an audio file
 *
 * @param file_addr Input audio file path.
 * @return the duration of an audio file.
 */
EM_PORT_API(int) get_file_duration_ms(const char *file_addr);

#endif //XM_DURATION_PARSER_H
