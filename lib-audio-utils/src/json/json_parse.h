#ifndef JSON_PARSE_H_
#define JSON_PARSE_H_
#include "../voice_mixer_struct.h"

int mixer_parse(MixerEffcets *mixer_effects, const char *json_file_addr);
int effects_parse(VoiceEffcets *voice_effects, const char *json_file_addr);
#endif
