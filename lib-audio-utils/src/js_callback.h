#ifndef _JS_CALLBACK_
#define _JS_CALLBACK_

#include <stdio.h>
#include <string.h>

#define JS_PROGRESS_CALLBACK "onGeneratorProgress('%s');\n"

static void js_progress_callback(char *callback, int progress) {
#ifdef __EMSCRIPTEN__
    if (NULL == callback) return;

    char pro[10];
    sprintf(pro, "%d", progress);
    int len = strlen(pro) + strlen(callback);
    char callback_buffer[len + 1];
    snprintf(callback_buffer, len, callback, pro);
    callback_buffer[len] = 0;
    emscripten_run_script(callback_buffer);
#endif
}

#endif